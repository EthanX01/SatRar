#include "satrar/app/CommandShell.hpp"

#include "satrar/aig/AagParser.hpp"
#include "satrar/rar/ExportAag.hpp"
#include "satrar/rar/RarVerifier.hpp"
#include "satrar/rar/SatRarEngine.hpp"
#include "satrar/sat/CadicalBackend.hpp"
#include "satrar/solver/SolverSession.hpp"

#include <array>
#include <charconv>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <variant>

namespace satrar::app {
namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

analysis::StuckAt parseStuck(const std::string& token) {
    if (token == "sa0") {
        return analysis::StuckAt::Zero;
    }
    if (token == "sa1") {
        return analysis::StuckAt::One;
    }
    throw std::invalid_argument("stuck-at must be sa0 or sa1");
}

template <typename Integer>
Integer parseInteger(std::string_view token, const char* name) {
    const auto original = token;
    if (token.size() > 1 && token.front() == '+' && token[1] >= '0' && token[1] <= '9') {
        token.remove_prefix(1);
    }
    Integer result{};
    const auto end = token.data() + token.size();
    const auto parsed = std::from_chars(token.data(), end, result);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        throw std::invalid_argument(std::string("invalid ") + name + ": " + std::string(original));
    }
    return result;
}

void requireEnd(std::istringstream& rest) {
    std::string extra;
    if (rest >> extra) {
        throw std::invalid_argument("unexpected argument: " + extra);
    }
}

std::uint32_t readTimeout(std::istringstream& rest) {
    std::string token;
    if (!(rest >> token)) {
        return 10;
    }
    const auto seconds = parseInteger<std::uint32_t>(token, "timeout in seconds");
    requireEnd(rest);
    return seconds;
}

aig::NodeId andNode(const aig::Circuit& circuit, std::uint32_t var) {
    const auto id = circuit.findNodeByAigVar(var);
    if (id == aig::kInvalidNodeId || !circuit.nodes()[id].isAnd()) {
        throw std::invalid_argument("AIG variable " + std::to_string(var) + " is not an AND gate");
    }
    return id;
}

analysis::DominatorOutputFault readOutputFault(const aig::Circuit& circuit, std::istringstream& rest) {
    std::string var, stuck;
    if (!(rest >> var >> stuck)) {
        throw std::invalid_argument("usage: dom <var> <sa0|sa1>");
    }
    return {andNode(circuit, parseInteger<std::uint32_t>(var, "AIG variable")), parseStuck(stuck)};
}

void printTarget(std::ostream& out, const aig::Circuit& circuit, analysis::PinFault fault) {
    const auto source = circuit.driverLiteral(fault.wire);
    out << "target fault=sa" << static_cast<unsigned>(fault.stuckAt)
        << " receiver=v" << circuit.nodes()[fault.wire.destination].aagVar
        << " pin=" << static_cast<unsigned>(fault.wire.inputPin)
        << " source=" << trim(source.toString()) << " source_raw=" << source.raw() << '\n';
}

void printTarget(std::ostream& out, const aig::Circuit& circuit, analysis::DominatorOutputFault fault) {
    out << "target fault=sa" << static_cast<unsigned>(fault.stuckAt)
        << " output=v" << circuit.nodes()[fault.dominatorNode].aagVar << '\n';
}

enum class CandidateKind : std::size_t { Wire, Gate, SubCircuit };
constexpr std::array<const char*, 3> candidateKinds{"wire", "gate", "subcircuit"};

CandidateKind candidateKind(const rar::Alternative& alternative) {
    if (const auto* gate = std::get_if<rar::GateAlternative>(&alternative)) {
        return gate->disjunction.empty() ? CandidateKind::Gate : CandidateKind::SubCircuit;
    }
    return CandidateKind::Wire;
}

std::string joinLiterals(const std::vector<aig::Literal>& literals, const char* op) {
    std::ostringstream joined;
    for (std::size_t i = 0; i < literals.size(); ++i) {
        if (i > 0) {
            joined << op;
        }
        joined << trim(literals[i].toString());
    }
    return joined.str();
}

void printCandidate(std::ostream& out, const aig::Circuit& circuit, const rar::Alternative& alternative,
                    std::size_t id) {
    std::visit([&](const auto& candidate) {
        const auto& attachment = candidate.attachment;
        const auto dom = circuit.nodes()[attachment.dominator].aagVar;
        const auto kind = static_cast<std::size_t>(candidateKind(alternative));
        out << "  add id=" << id << " kind=" << candidateKinds[kind] << " at_output=v" << dom
            << " replacement=(v" << dom << " AND ";
        if constexpr (std::is_same_v<std::decay_t<decltype(candidate)>, rar::WireAlternative>) {
            out << trim(candidate.source.toString());
        } else {
            out << "NOT(" << joinLiterals(candidate.conjunction, " AND ");
            if (!candidate.disjunction.empty()) {
                out << " AND (" << joinLiterals(candidate.disjunction, " OR ") << ')';
            }
            out << ')';
        }
        out << ") evidence=" << attachment.evidence << '\n';
    }, alternative);
}

rar::SatRarOptions parseMode(const std::string& name) {
    rar::SatRarOptions options;
    if (name == "l0") {
        options.mode = rar::SatRarMode::TwoWayRarL0;
    } else if (name == "l1") {
        options.mode = rar::SatRarMode::TwoWayRarL1;
    } else if (name == "l2") {
        options.mode = rar::SatRarMode::TwoWayRarL2;
    } else if (name == "wire") {
        options.mode = rar::SatRarMode::SrarWire;
    } else if (name == "gate" || name == "gate_nolearn" || name == "gate_nofilter") {
        options.mode = rar::SatRarMode::SrarGate;
        options.enableLearning = name != "gate_nolearn";
        options.enableImplicationFilter = name != "gate_nofilter";
    } else if (name == "sub") {
        options.mode = rar::SatRarMode::SrarSubCircuit;
    } else {
        throw std::invalid_argument("unknown mode " + name);
    }
    return options;
}

}  // namespace

CommandShell::CommandShell(std::istream& in, std::ostream& out, std::ostream& err)
    : in_(in), out_(out), err_(err) {}

void CommandShell::printHelp() const {
    out_ << "read <aag>                         Load a circuit; quote paths with spaces.\n"
            "fault_ma pin <var> <pin> <sa0|sa1> [signed trials...]\n"
            "fault_ma dom <var> <sa0|sa1> [signed trials...]\n"
            "rar_search <var> <pin> <sa0|sa1> [mode]  List alternatives.\n"
            "rar_verify <var> <pin> <sa0|sa1> [mode] [seconds]\n"
            "rar_apply <id> <aag> [seconds]\n"
            "sat_solve [signed assumptions...]\n"
            "fault_exact pin <var> <pin> <sa0|sa1> | dom <var> <sa0|sa1>\n"
            "ma_check pin <var> <pin> <sa0|sa1> <signed literal>\n"
            "help\n"
            "quit\n"
            "\n"
            "Modes: l0, l1, l2, wire (default), gate, gate_nolearn, gate_nofilter, sub.\n"
            "Pins are 0 or 1. vN is positive AIG variable N and ~vN is its complement.\n"
            "fault_ma lists assignments; fault_exact checks detectability.\n"
            "Use rar_search or rar_verify on the same pin fault to see replacement candidates.\n";
}

void CommandShell::run(bool interactive) {
    std::string line;
    for (;;) {
        if (interactive) {
            out_ << "satrar_v2> " << std::flush;
        }
        if (!std::getline(in_, line) || !executeLine(line)) {
            return;
        }
    }
}

bool CommandShell::executeLine(const std::string& line) {
    const auto text = trim(line);
    if (text.empty() || text[0] == '#') {
        return true;
    }
    std::istringstream input(text);
    std::string command;
    input >> command;
    try {
        if (command == "quit" || command == "exit") {
            return false;
        }
        if (command == "help") {
            requireEnd(input);
            printHelp();
        } else if (command == "read") {
            std::string path;
            if (!(input >> std::quoted(path, '"', '\0')) || path.empty()) {
                throw std::invalid_argument("usage: read <aag>");
            }
            requireEnd(input);
            return commandRead(path);
        } else if (command == "fault_ma") {
            commandFaultMa(input);
        } else if (command == "rar_verify" || command == "rar_search") {
            commandRar(command, input);
        } else if (command == "rar_apply") {
            commandApply(input);
        } else if (command == "sat_solve") {
            commandSat(input);
        } else if (command == "fault_exact") {
            commandExact(input);
        } else if (command == "ma_check") {
            commandMaCheck(input);
        } else {
            throw std::invalid_argument("unknown command " + command);
        }
    } catch (const std::exception& ex) {
        err_ << "error: " << ex.what() << '\n';
    }
    return true;
}

bool CommandShell::commandRead(const std::string& path) {
    context_ = std::make_unique<analysis::FaultContext>(aig::AagParser::parseFile(path));
    candidates_.clear();
    const auto stats = context_->circuit().stats();
    out_ << "read pi=" << stats.primaryInputs << " po=" << stats.primaryOutputs
         << " and=" << stats.andGates << " wires=" << stats.wires << '\n';
    return true;
}

aig::Literal CommandShell::signedVariable(const std::string& token) {
    const auto value = parseInteger<std::int64_t>(token, "signed AIG variable");
    constexpr auto maxVar = std::numeric_limits<std::int32_t>::max();
    if (value == 0 || value < -maxVar || value > maxVar) {
        throw std::invalid_argument("signed AIG variable out of range: " + token);
    }
    return aig::Literal::fromVariable(static_cast<std::uint32_t>(value < 0 ? -value : value), value < 0);
}

analysis::PinFault CommandShell::parsePin(std::istringstream& rest) const {
    if (!context_) {
        throw std::invalid_argument("no circuit loaded");
    }
    std::string var, pin, stuck;
    if (!(rest >> var) || (var == "pin" && !(rest >> var)) || !(rest >> pin >> stuck)) {
        throw std::invalid_argument("usage: pin <var> <pin> <sa0|sa1>");
    }
    const auto index = parseInteger<unsigned>(pin, "pin");
    if (index > 1) {
        throw std::invalid_argument("pin must be 0 or 1");
    }
    const auto node = andNode(context_->circuit(), parseInteger<std::uint32_t>(var, "AIG variable"));
    return analysis::PinFault{{node, static_cast<std::uint8_t>(index)}, parseStuck(stuck)};
}

void CommandShell::commandFaultMa(std::istringstream& rest) {
    if (!context_) {
        throw std::invalid_argument("no circuit loaded");
    }
    std::string kind;
    rest >> kind;
    const auto& circuit = context_->circuit();
    std::ostringstream target;
    if (kind == "pin") {
        const auto fault = parsePin(rest);
        context_->setPinFault(fault);
        printTarget(target, circuit, fault);
    } else if (kind == "dom") {
        const auto fault = readOutputFault(circuit, rest);
        context_->setDominatorOutputFault(fault);
        printTarget(target, circuit, fault);
    } else {
        throw std::invalid_argument("fault_ma expects pin or dom");
    }
    std::vector<aig::Literal> trials;
    std::string token;
    if (rest >> token) {
        if (token == "trial" && !(rest >> token)) {
            throw std::invalid_argument("trial requires a signed AIG variable");
        }
        trials.push_back(signedVariable(token));
    }
    while (rest >> token) {
        trials.push_back(signedVariable(token));
    }
    const auto ma = context_->computeMandatoryAssignments(trials);
    const char* status = "none";
    switch (context_->status()) {
        case analysis::FaultStatus::Consistent: status = "consistent"; break;
        case analysis::FaultStatus::Conflict: status = "conflict"; break;
        case analysis::FaultStatus::NeedsExactTest: status = "needs_exact"; break;
        case analysis::FaultStatus::NoFault: status = "none"; break;
    }
    out_ << target.str() << "status=" << status << " assignments=" << ma.items().size() << '\n';
    for (const auto& assignment : ma.items()) {
        out_ << "raw=" << assignment.lit.raw() << " level=" << assignment.contextLevel
             << " decision=" << (assignment.fromPropagation ? 0 : 1)
             << " literal=" << trim(assignment.lit.toString()) << '\n';
    }
}

void CommandShell::commandRar(const std::string& command, std::istringstream& rest) {
    const auto target = parsePin(rest);
    std::string modeName = "wire";
    (void)(rest >> modeName);
    const auto seconds = readTimeout(rest);
    const auto options = parseMode(modeName);
    const auto& circuit = context_->circuit();
    rar::SatRarEngine engine(*context_);
    engine.setOptions(options);
    engine.setPinTarget(target);
    candidates_ = engine.run();
    const auto& stats = engine.statistics();
    out_ << "mode=" << modeName << " candidates=" << candidates_.size() << " trials=" << stats.trials
         << " learned=" << stats.learned << " gate_visits=" << stats.incidentGateVisits
         << " clause_visits=" << stats.learnedClauseVisits << " limited=" << (stats.limitReached ? 1 : 0);
    if (stats.limitReached) {
        out_ << " limit_step=" << stats.limitStep << " limit_scope=" << stats.limitScope;
    }
    out_ << '\n';
    printTarget(out_, circuit, target);
    std::array<std::size_t, candidateKinds.size()> counts{};
    for (const auto& candidate : candidates_) {
        ++counts[static_cast<std::size_t>(candidateKind(candidate))];
    }
    out_ << "alternatives";
    for (std::size_t i = 0; i < counts.size(); ++i) {
        out_ << ' ' << candidateKinds[i] << '=' << counts[i];
    }
    out_ << " search_status="
         << (stats.limitReached ? "limited" : stats.targetConflict ? "target_conflict" : "complete")
         << '\n';
    const bool verify = command == "rar_verify";
    const sat::CadicalBackend backend;
    const rar::RarVerifier verifier(backend);
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    for (std::size_t i = 0; i < candidates_.size(); ++i) {
        out_ << "cand id=" << i << " key=" << rar::canonicalKey(circuit, candidates_[i]);
        if (verify) {
            const auto result = verifier.verify(circuit, candidates_[i], seconds);
            out_ << " cec=" << rar::rarAcceptanceName(result.acceptance);
            accepted += result.acceptance == rar::RarAcceptance::Accepted;
            rejected += result.acceptance == rar::RarAcceptance::Rejected;
            if (result.acceptance != rar::RarAcceptance::Accepted) {
                out_ << '\n' << "  cec_detail id=" << i << " stage=" << rar::rarStageName(result.failedStage)
                     << " " << result.message;
            }
        }
        out_ << '\n';
        printCandidate(out_, circuit, candidates_[i], i);
    }
    if (verify) {
        out_ << "cec_summary accepted=" << accepted << " rejected=" << rejected
             << " indeterminate=" << candidates_.size() - accepted - rejected << '\n';
    }
}

void CommandShell::commandApply(std::istringstream& rest) {
    if (!context_) {
        throw std::invalid_argument("no circuit loaded");
    }
    std::string id, path;
    if (!(rest >> id >> std::quoted(path, '"', '\0')) || path.empty()) {
        throw std::invalid_argument("usage: rar_apply <id> <aag> [seconds]");
    }
    const auto index = parseInteger<std::size_t>(id, "candidate id");
    const auto seconds = readTimeout(rest);
    if (index >= candidates_.size()) {
        throw std::invalid_argument("candidate id is out of range");
    }
    const sat::CadicalBackend backend;
    const rar::RarVerifier verifier(backend);
    const auto verified = verifier.verify(context_->circuit(), candidates_[index], seconds);
    if (verified.acceptance != rar::RarAcceptance::Accepted || !verified.rewritten) {
        out_ << "rar_apply=" << rar::rarAcceptanceName(verified.acceptance)
             << " stage=" << rar::rarStageName(verified.failedStage) << ' '
             << verified.message << '\n';
        return;
    }
    rar::ExportOptions options;
    options.timeoutSeconds = seconds;
    const auto published =
        rar::exportVerifiedAag(context_->circuit(), *verified.rewritten, path, backend, options);
    out_ << "rar_apply=" << (published.published ? "verified_equivalent" : "not_published") << " candidate="
         << index << ' ' << published.message << '\n';
}

void CommandShell::commandSat(std::istringstream& rest) {
    if (!context_) {
        throw std::invalid_argument("no circuit loaded");
    }
    std::vector<aig::Literal> assumptions;
    std::string token;
    while (rest >> token) {
        assumptions.push_back(signedVariable(token));
    }
    solver::SolverSession session(context_->net());
    session.setCdcl(true);
    solver::Budget budget;
    budget.decisionLimit = 100000;
    const auto result = session.solve(assumptions, budget);
    const char* name = result.state == solver::SolveState::Sat      ? "SAT"
                       : result.state == solver::SolveState::Unsat ? "UNSAT"
                                                                   : "UNKNOWN";
    out_ << name << " reason=" << result.reason << " decisions=" << result.decisions
         << " conflicts=" << result.conflicts << '\n';
    if (result.state == solver::SolveState::Sat) {
        out_ << "pi";
        for (const bool bit : result.primaryInputs) {
            out_ << ' ' << (bit ? 1 : 0);
        }
        out_ << '\n';
    }
}

void CommandShell::commandExact(std::istringstream& rest) {
    if (!context_) {
        throw std::invalid_argument("no circuit loaded");
    }
    std::string kind;
    rest >> kind;
    const auto& circuit = context_->circuit();
    if (kind == "pin") {
        const auto fault = parsePin(rest);
        requireEnd(rest);
        context_->setPinFault(fault);
        printTarget(out_, circuit, fault);
    } else if (kind == "dom") {
        const auto fault = readOutputFault(circuit, rest);
        requireEnd(rest);
        context_->setDominatorOutputFault(fault);
        printTarget(out_, circuit, fault);
    } else {
        throw std::invalid_argument("fault_exact expects pin or dom");
    }
    solver::Budget budget;
    budget.decisionLimit = 100000;
    const auto answer = context_->solveDetection({}, budget, true);
    const char* name = answer.state == solver::SolveState::Sat      ? "SAT"
                       : answer.state == solver::SolveState::Unsat ? "UNSAT"
                                                                   : "UNKNOWN";
    out_ << name << " reason=" << answer.reason;
    if (answer.redundantFault) {
        out_ << " redundant=1";
    }
    out_ << '\n';
    if (answer.state == solver::SolveState::Sat) {
        out_ << "pi";
        for (const bool bit : answer.primaryInputs) {
            out_ << ' ' << (bit ? 1 : 0);
        }
        out_ << '\n';
    }
}

void CommandShell::commandMaCheck(std::istringstream& rest) {
    const auto target = parsePin(rest);
    std::string token;
    if (!(rest >> token)) {
        throw std::invalid_argument("usage: ma_check pin <var> <pin> <sa> <signed literal>");
    }
    requireEnd(rest);
    const auto literal = signedVariable(token);
    context_->setPinFault(target);
    printTarget(out_, context_->circuit(), target);
    solver::Budget budget;
    budget.decisionLimit = 100000;
    const auto answer = context_->checkMandatory(literal, budget, true);
    if (answer.redundantFault) {
        out_ << "redundant_fault\n";
    } else if (answer.state == solver::SolveState::Unknown) {
        out_ << "UNKNOWN reason=" << answer.reason << '\n';
    } else {
        out_ << (answer.mandatory ? "mandatory" : "not_mandatory") << '\n';
    }
}

}  // namespace satrar::app
