#include "satrar/app/CommandShell.hpp"

#include "satrar/aig/AagParser.hpp"
#include "satrar/aig/AagWriter.hpp"
#include "satrar/rar/Alternative.hpp"
#include "satrar/app/Diagnostics.hpp"
#include "satrar/rar/CircuitRewriter.hpp"
#include "satrar/rar/EquivalenceChecker.hpp"
#include "satrar/rar/ExportAag.hpp"
#include "satrar/rar/RarVerifier.hpp"
#include "satrar/rar/Rewriter.hpp"
#include "satrar/sat/CadicalBackend.hpp"
#include "satrar/sat/CnfEncoder.hpp"
#include "satrar/sat/MiterBuilder.hpp"

#if defined(SATRAR_HAS_MINISAT) && SATRAR_HAS_MINISAT
#include "satrar/analysis/Fault.hpp"
#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/Alternative.hpp"
#include "satrar/rar/SatRarEngine.hpp"
#include "satrar/sat/MinisatVerificationSolver.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <type_traits>
#include <unordered_map>
#include <sstream>

namespace satrar::app {


namespace {

std::pair<std::string, std::string> pathAndTimeout(std::istringstream& input) {
    std::string path;
    std::string timeout;
    input >> std::ws;
    if (input.peek() == '"') {
        input.get();
        if (!std::getline(input, path, '"')) {
            throw std::invalid_argument("unterminated path");
        }
        input >> timeout;
    } else {
        std::getline(input, path);
        const auto end = path.find_last_not_of(" \t");
        if (end != std::string::npos) {
            path.resize(end + 1);
        }
        const auto space = path.find_last_of(" \t");
        if (space != std::string::npos) {
            const auto tail = path.substr(space + 1);
            if (!tail.empty() && tail.find_first_not_of("0123456789") == std::string::npos) {
                timeout = tail;
                path.resize(path.find_last_not_of(" \t", space) + 1);
            }
        }
    }
    return {path, timeout};
}

std::uint32_t seconds(const std::string& token) {
    if (token.empty()) {
        return 300;
    }
    if (token.find_first_not_of("0123456789") != std::string::npos) {
        throw std::invalid_argument("timeout must be a nonnegative integer in seconds");
    }
    const auto value = std::stoull(token);
    if (value > UINT32_MAX) {
        throw std::invalid_argument("timeout out of range");
    }
    return static_cast<std::uint32_t>(value);
}

#if defined(SATRAR_HAS_MINISAT) && SATRAR_HAS_MINISAT
analysis::StuckAt stuckAt(const std::string& token) {
    if (token == "sa0" || token == "stuck0" || token == "0") {
        return analysis::StuckAt::Zero;
    }
    if (token == "sa1" || token == "stuck1" || token == "1") {
        return analysis::StuckAt::One;
    }
    throw std::invalid_argument("stuck must be sa0 or sa1");
}

aig::NodeId lookupAnd(const aig::Circuit& circuit, std::uint32_t var) {
    const auto id = circuit.findNodeByAigVar(var);
    if (id == aig::kInvalidNodeId || !circuit.nodes()[id].isAnd()) {
        throw std::invalid_argument("expected an AND variable");
    }
    return id;
}

analysis::PinFault pinFault(const aig::Circuit& circuit, std::uint32_t var,
                            unsigned pin, const std::string& stuck) {
    if (pin > 1) {
        throw std::invalid_argument("pin must be 0 or 1");
    }
    return {{lookupAnd(circuit, var), static_cast<std::uint8_t>(pin)}, stuckAt(stuck)};
}

rar::SatRarOptions searchOptions(const std::string& mode, bool paper = false) {
    using Mode = rar::SatRarMode;
    rar::SatRarOptions options;
    options.excludeTargetDirectReceiverGates = paper;
    if (mode == "l0") options.mode = Mode::TwoWayRarL0;
    else if (mode == "l1") options.mode = Mode::TwoWayRarL1;
    else if (mode == "l2") options.mode = Mode::TwoWayRarL2;
    else if (mode == "wire") options.mode = Mode::SrarWire;
    else if (mode == "gate" || mode == "gate_nolearn" || mode == "gate_nofilter") options.mode = Mode::SrarGate;
    else if (mode == "sub") options.mode = Mode::SrarSubCircuit;
    else throw std::invalid_argument("mode must be l0, l1, l2, wire, gate, gate_nolearn, gate_nofilter or sub");
    options.enableLearning = mode != "gate_nolearn";
    options.enableImplicationFilter = mode != "gate_nofilter";
    return options;
}

const char* acceptanceLabel(const rar::RarVerificationResult& result) {
    if (result.acceptance == rar::RarAcceptance::Accepted) return "verified_equivalent";
    if (result.acceptance == rar::RarAcceptance::Rejected) return "rejected";
    return "unverified";
}

std::string describeCandidate(const aig::Circuit& circuit, const rar::Alternative& alt) {
    std::ostringstream text;
    std::visit([&](const auto& item) {
        text << "attach=" << circuit.nodes()[item.attachment.dominator].aagVar
             << " rule=" << item.attachment.evidence;
        if constexpr (std::is_same_v<std::decay_t<decltype(item)>, rar::WireAlternative>) {
            text << " source_raw=" << item.source.raw();
        } else {
            const auto list = [&](const auto& lits, const char* label) {
                text << ' ' << label << '=';
                for (std::size_t i = 0; i < lits.size(); ++i) {
                    if (i) text << ',';
                    text << lits[i].raw();
                }
            };
            list(item.conjunction, "negated_and_raw");
            if (!item.disjunction.empty()) list(item.disjunction, "or_raw");
        }
    }, alt);
    return text.str();
}
#endif

}  // namespace

CommandShell::CommandShell(std::istream& in, std::ostream& out, std::ostream& err)
    : in_(in), out_(out), err_(err) {}

void CommandShell::setVerbose(bool on) noexcept {
    verbose_ = on;
    Diagnostics::setVerbose(on);
}

std::string CommandShell::trim(const std::string& s) {
    std::size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

void CommandShell::printHelp() const {
    out_ << "Commands:\n"
         << "  read <path>            Load ASCII .aag (path may contain spaces when quoted)\n"
         << "  stats                  Circuit statistics\n"
         << "  inputs                 List PI node AIG variables\n"
         << "  outputs                List PO AIG literals (raw)\n"
         << "  gate <aigVar>          Gate type by AIG variable index\n"
         << "  fanin <aigVar>         Fanin literals (AND) or note for PI\n"
         << "  fanout <aigVar>        Fanout wires (destination,pin)\n"
         << "  sim <01...>            Simulate POs for PI assignment (MSB=first PI)\n"
         << "  write_cnf <path>       Export Tseitin CNF (DIMACS)\n"
         << "  write_aag <path>       Export ASCII .aag from loaded circuit\n"
         << "  sat_solve <cnf> [sec]  Run CaDiCaL on DIMACS (exit 10=SAT, 20=UNSAT)\n"
         << "  verify_equiv <aag> [sec]  Miter CEC vs loaded circuit (CaDiCaL)\n"
         << "  verbose on|off         Extra parse/process timing on stderr\n"
         << "  trace on|off           Print TRACE lines (scopes, trials, conflicts, learning, emits) from rar_* searches\n"
#if defined(SATRAR_HAS_MINISAT) && SATRAR_HAS_MINISAT
         << "  fault_ma pin <var> <pin> <sa0|sa1>  Print MA, levels and reasons (optional exact)\n"
         << "  fault_ma dom <var> <sa0|sa1>        Print MA for output fault\n"
         << "  rar_l0|rar_l1|rar_l2 <var> <pin> <sa0|sa1>  2WRAR with recursive depths 0/1/2\n"
         << "  rar_wire|rar_gate|rar_gate_nolearn|rar_gate_nofilter|rar_sub <var> <pin> <sa0|sa1>\n"
         << "  rar_verify <var> <pin> <sa0|sa1> [mode] [sec]   Verify all candidates\n"
         << "  rar_apply <id> <aag> [sec]    Verify a listed candidate and export its rewrite\n"
         << "  rar_batch <mode> [wireLimit] [sec] [paper|all] [verify|discover] [all_rows|summary] [maxTrials] [wireOffset]\n"
#endif
         << "  help                   This message\n"
         << "  quit                   Exit shell\n"
         << "\n"
         << "Errors use [satrar][ERROR] on stderr; use verbose on for [satrar][INFO].\n";
}

void CommandShell::run() {
    out_ << "satrar interactive shell (type help)\n";
    std::string line;
    while (true) {
        out_ << "> " << std::flush;
        if (!std::getline(in_, line)) {
            out_ << '\n';
            break;
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (line == "quit" || line == "exit") {
            break;
        }
        if (!executeLine(line)) {
            Diagnostics::error(err_, "command failed: " + line);
        }
    }
}

bool CommandShell::executeLine(const std::string& line) {
    std::string work = trim(line);
    if (work.size() >= 3 && static_cast<unsigned char>(work[0]) == 0xEF &&
        static_cast<unsigned char>(work[1]) == 0xBB && static_cast<unsigned char>(work[2]) == 0xBF) {
        work.erase(0, 3);
    }
    if (!work.empty() && work[0] == '\xEF' && work.size() > 1 && work[1] == '\xBB') {
        work.erase(0, 1);  // stray BOM fragments
    }
    std::istringstream iss(work);
    std::string cmd;
    iss >> cmd;
    cmd = trim(cmd);

    if (cmd == "help" || cmd == "?") {
        printHelp();
        return true;
    }
    if (cmd == "read") {
        std::string path;
        std::getline(iss >> std::ws, path);
        path = trim(path);
        if (path.empty()) {
            Diagnostics::error(err_, "usage: read <path>");
            return false;
        }
        if (path.front() == '"' && path.back() == '"' && path.size() >= 2) {
            path = path.substr(1, path.size() - 2);
        }
        return commandRead(path);
    }
    if (cmd == "stats") {
        commandStats();
        return true;
    }
    if (cmd == "inputs") {
        commandInputs();
        return true;
    }
    if (cmd == "outputs") {
        commandOutputs();
        return true;
    }
    if (cmd == "gate") {
        std::uint32_t var = 0;
        if (!(iss >> var)) {
            Diagnostics::error(err_, "usage: gate <aigVar>");
            return false;
        }
        commandGate(var);
        return true;
    }
    if (cmd == "fanin") {
        std::uint32_t var = 0;
        if (!(iss >> var)) {
            err_ << "usage: fanin <aigVar>\n";
            return false;
        }
        commandFanin(var);
        return true;
    }
    if (cmd == "fanout") {
        std::uint32_t var = 0;
        if (!(iss >> var)) {
            err_ << "usage: fanout <aigVar>\n";
            return false;
        }
        commandFanout(var);
        return true;
    }
    if (cmd == "sim") {
        std::string bits;
        iss >> bits;
        if (bits.empty()) {
            err_ << "usage: sim <01...>\n";
            return false;
        }
        commandSim(bits);
        return true;
    }
    if (cmd == "write_aag") {
        std::string path;
        std::getline(iss >> std::ws, path);
        path = trim(path);
        if (path.empty()) {
            Diagnostics::error(err_, "usage: write_aag <path>");
            return false;
        }
        return commandWriteAag(path);
    }
    if (cmd == "write_cnf") {
        std::string path;
        std::getline(iss >> std::ws, path);
        path = trim(path);
        if (path.empty()) {
            err_ << "usage: write_cnf <path>\n";
            return false;
        }
        return commandWriteCnf(path);
    }
    if (cmd == "verify_equiv" || cmd == "sat_solve") {
        try {
            const auto [path, deadline] = pathAndTimeout(iss);
            if (path.empty()) throw std::invalid_argument("path required");
            return cmd == "verify_equiv" ? commandVerifyEquiv(path, deadline)
                                         : commandSatSolve(path, deadline);
        } catch (const std::exception& ex) {
            Diagnostics::error(err_, ex.what());
            return false;
        }
    }
    if (cmd == "verbose") {
        std::string flag;
        if (!(iss >> flag)) {
            Diagnostics::error(err_, "usage: verbose on|off");
            return false;
        }
        return commandVerbose(flag);
    }
    if (cmd == "trace") {
        std::string flag;
        if (!(iss >> flag) || (flag != "on" && flag != "off")) {
            Diagnostics::error(err_, "usage: trace on|off");
            return false;
        }
        trace_ = flag == "on";
        return true;
    }
#if defined(SATRAR_HAS_MINISAT) && SATRAR_HAS_MINISAT
    try {
        if (cmd == "fault_ma") {
            std::string which;
            if (!(iss >> which)) throw std::invalid_argument("usage: fault_ma pin|dom ...");
            return commandFaultMa(which, iss);
        }
        if (cmd == "rar_apply") {
            std::size_t id = 0;
            if (!(iss >> id)) throw std::invalid_argument("candidate ID required");
            const auto [path, deadline] = pathAndTimeout(iss);
            return commandRarApply(id, path, seconds(deadline));
        }
        if (cmd == "rar_batch") {
            std::string mode;
            std::size_t limit = 0;
            std::string deadline;
            std::string scope;
            std::string check;
            std::string detail;
            std::string maxTrials;
            std::string offset;
            if (!(iss >> mode)) throw std::invalid_argument("batch mode required");
            if (iss >> limit) iss >> deadline >> scope >> check >> detail >> maxTrials >> offset;
            if (!scope.empty() && scope != "all" && scope != "paper") throw std::invalid_argument("scope must be paper or all");
            if (!check.empty() && check != "verify" && check != "discover") throw std::invalid_argument("check mode must be verify or discover");
            if (!detail.empty() && detail != "summary" && detail != "all_rows") {
                throw std::invalid_argument("detail mode must be all_rows or summary");
            }
            auto options = searchOptions(mode, scope != "all");
            if (!maxTrials.empty()) {
                options.maxTrials = seconds(maxTrials);
                if (!options.maxTrials) throw std::invalid_argument("maxTrials must be positive");
            }
            return commandRarBatch(options, limit, seconds(deadline), check != "discover",
                                   detail == "summary", offset.empty() ? 0 : seconds(offset));
        }
        if (cmd.rfind("rar_", 0) == 0) {
            std::uint32_t var = 0;
            unsigned pin = 0;
            std::string stuck;
            if (!(iss >> var >> pin >> stuck)) {
                throw std::invalid_argument("usage: " + cmd + " <andVar> <pin> <sa0|sa1>");
            }
            std::string mode = cmd.substr(4);
            std::string deadline;
            const bool verify = cmd == "rar_verify";
            if (verify && !(iss >> mode >> deadline) && mode == "verify") mode = "l0";
            return commandRarSearch(var, pin, stuck, searchOptions(mode), mode, verify, seconds(deadline));
        }
    } catch (const std::exception& ex) {
        Diagnostics::error(err_, ex.what());
        return false;
    }
#endif

    Diagnostics::warn(err_, std::string("unknown command '") + cmd + "' (type help)");
    return false;
}

bool CommandShell::commandRead(const std::string& path) {
    const auto t0 = std::chrono::steady_clock::now();
    Diagnostics::info(err_, "parsing '" + path + "'");
    try {
        circuit_ = aig::AagParser::parseFile(path);
#if defined(SATRAR_HAS_MINISAT) && SATRAR_HAS_MINISAT
        candidates_.clear();
#endif
        const auto st = circuit_->stats();
        const double sec = Diagnostics::elapsedSeconds(t0);
        Diagnostics::notice(out_, "loaded '" + path + "' AND=" + std::to_string(st.andGates) +
                                         " in " + std::to_string(sec) + "s");
        if (verbose_) {
            Diagnostics::info(err_, "PI=" + std::to_string(st.primaryInputs) + " PO=" +
                                        std::to_string(st.primaryOutputs) + " wires=" +
                                        std::to_string(st.wires) + " maxAigVar=" +
                                        std::to_string(st.maxAigVar));
        }
        return true;
    } catch (const std::exception& ex) {
        Diagnostics::error(err_, std::string("read failed '") + path + "': " + ex.what());
        circuit_.reset();
        return false;
    }
}

void CommandShell::commandStats() const {
    if (!circuit_) {
        err_ << "no circuit loaded\n";
        return;
    }
    const auto st = circuit_->stats();
    out_ << "PI=" << st.primaryInputs << " PO=" << st.primaryOutputs << " AND=" << st.andGates
         << " wires=" << st.wires << " maxAigVar=" << st.maxAigVar << '\n';
}

void CommandShell::commandInputs() const {
    if (!circuit_) {
        err_ << "no circuit loaded\n";
        return;
    }
    for (const aig::NodeId id : circuit_->primaryInputIds()) {
        out_ << 'v' << circuit_->nodes()[id].aagVar << '\n';
    }
}

void CommandShell::commandOutputs() const {
    if (!circuit_) {
        err_ << "no circuit loaded\n";
        return;
    }
    for (const aig::Literal lit : circuit_->primaryOutputs()) {
        out_ << lit.raw() << '\n';
    }
}

void CommandShell::commandGate(std::uint32_t aigVar) const {
    if (!circuit_) {
        err_ << "no circuit loaded\n";
        return;
    }
    const aig::NodeId id = circuit_->findNodeByAigVar(aigVar);
    if (id == aig::kInvalidNodeId) {
        err_ << "unknown aigVar\n";
        return;
    }
    const aig::Node& n = circuit_->nodes()[id];
    switch (n.kind) {
        case aig::NodeKind::Constant:
            out_ << "constant\n";
            break;
        case aig::NodeKind::PrimaryInput:
            out_ << "pi\n";
            break;
        case aig::NodeKind::And:
            out_ << "and\n";
            break;
    }
}

void CommandShell::commandFanin(std::uint32_t aigVar) const {
    if (!circuit_) {
        err_ << "no circuit loaded\n";
        return;
    }
    const aig::NodeId id = circuit_->findNodeByAigVar(aigVar);
    if (id == aig::kInvalidNodeId) {
        err_ << "unknown aigVar\n";
        return;
    }
    const aig::Node& n = circuit_->nodes()[id];
    if (n.isAnd()) {
        const auto& in = n.andInputs();
        out_ << in.left.raw() << ' ' << in.right.raw() << '\n';
    } else {
        out_ << "(none)\n";
    }
}

void CommandShell::commandFanout(std::uint32_t aigVar) const {
    if (!circuit_) {
        err_ << "no circuit loaded\n";
        return;
    }
    const aig::NodeId id = circuit_->findNodeByAigVar(aigVar);
    if (id == aig::kInvalidNodeId) {
        err_ << "unknown aigVar\n";
        return;
    }
    for (const aig::WireRef& w : circuit_->fanoutWires(id)) {
        out_ << circuit_->nodes()[w.destination].aagVar << ' ' << static_cast<unsigned>(w.inputPin)
             << '\n';
    }
}

void CommandShell::commandSim(const std::string& bits) const {
    if (!circuit_) {
        err_ << "no circuit loaded\n";
        return;
    }
    if (bits.size() != circuit_->primaryInputIds().size()) {
        err_ << "need " << circuit_->primaryInputIds().size() << " bits\n";
        return;
    }
    std::vector<bool> vals;
    vals.reserve(bits.size());
    for (char c : bits) {
        if (c == '0') {
            vals.push_back(false);
        } else if (c == '1') {
            vals.push_back(true);
        } else {
            err_ << "bits must be 0/1\n";
            return;
        }
    }
    try {
        const std::vector<bool> outs = circuit_->evaluate(vals);
        for (bool o : outs) {
            out_ << (o ? '1' : '0');
        }
        out_ << '\n';
    } catch (const std::exception& ex) {
        err_ << ex.what() << '\n';
    }
}

bool CommandShell::commandWriteAag(const std::string& path) const {
    if (!circuit_) {
        Diagnostics::error(err_, "no circuit loaded");
        return false;
    }
    try {
        aig::AagWriter::writeFile(*circuit_, path);
        Diagnostics::notice(out_, "wrote AIGER '" + path + "'");
        aig::Circuit reloaded = aig::AagParser::parseFile(path);
        reloaded.validate();
        return true;
    } catch (const std::exception& ex) {
        Diagnostics::error(err_, std::string("write_aag failed: ") + ex.what());
        return false;
    }
}

bool CommandShell::commandWriteCnf(const std::string& path) const {
    if (!circuit_) {
        Diagnostics::error(err_, "no circuit loaded");
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    try {
        satrar::sat::CnfFormula formula = satrar::sat::CnfEncoder::encodeCircuit(*circuit_);
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            Diagnostics::error(err_, std::string("cannot write '") + path + "'");
            return false;
        }
        formula.writeDimacs(file);
        const double sec = Diagnostics::elapsedSeconds(t0);
        Diagnostics::notice(out_, std::string("wrote DIMACS '") + path + "' vars=" +
                                   std::to_string(formula.numVariables()) + " clauses=" +
                                   std::to_string(formula.clauses().size()) + " in " +
                                   std::to_string(sec) + "s");
        return true;
    } catch (const std::exception& ex) {
        Diagnostics::error(err_, std::string("write_cnf failed: ") + ex.what());
        return false;
    }
}

bool CommandShell::commandSatSolve(const std::string& path, const std::string& timeout) const {
    const auto backend = sat::defaultVerificationBackend();
    const auto result = backend->solveDimacsFile(path, seconds(timeout));
    out_ << "verdict=" << sat::satVerdictName(result.verdict) << " exit=" << result.exitCode
         << " time=" << result.elapsedSeconds << "s " << result.message << '\n';
    for (const auto lit : result.modelLits) out_ << "v " << lit << '\n';
    return result.verdict == sat::SatVerdict::Sat || result.verdict == sat::SatVerdict::Unsat;
}

bool CommandShell::commandVerifyEquiv(const std::string& path, const std::string& timeout) const {
    if (!circuit_) throw std::logic_error("no circuit loaded");
    const auto backend = sat::defaultVerificationBackend();
    const auto other = aig::AagParser::parseFile(path);
    const auto result = rar::EquivalenceChecker(*backend).check(*circuit_, other, seconds(timeout));
    out_ << "verify_equiv=" << rar::equivalenceVerdictName(result.verdict)
         << " time=" << result.elapsedSeconds << "s " << result.message << '\n';
    if (result.counterexampleReplayOk) {
        out_ << "cex_pi=";
        for (bool value : result.counterexamplePi) out_ << (value ? '1' : '0');
        out_ << " differing_po=" << *result.differingOutput << '\n';
    }
    return result.verdict == rar::EquivalenceVerdict::Equivalent ||
           result.verdict == rar::EquivalenceVerdict::NotEquivalent;
}

bool CommandShell::commandVerbose(const std::string& onOff) {
    if (onOff == "on" || onOff == "1" || onOff == "true") {
        verbose_ = true;
        Diagnostics::setVerbose(true);
        Diagnostics::info(err_, "verbose enabled");
        return true;
    }
    if (onOff == "off" || onOff == "0" || onOff == "false") {
        verbose_ = false;
        Diagnostics::setVerbose(false);
        Diagnostics::info(err_, "verbose disabled");
        return true;
    }
    Diagnostics::error(err_, "usage: verbose on|off");
    return false;
}

#if defined(SATRAR_HAS_MINISAT) && SATRAR_HAS_MINISAT
bool CommandShell::commandFaultMa(const std::string& which, std::istringstream& input) const {
    if (!circuit_) throw std::logic_error("no circuit loaded");
    analysis::FaultContext context(circuit_->clone());
    std::uint32_t var = 0;
    unsigned pin = 0;
    std::string stuck;
    if (which == "pin" && input >> var >> pin >> stuck) {
        context.setPinFault(pinFault(*circuit_, var, pin, stuck));
    } else if (which == "dom" && input >> var >> stuck) {
        context.setDominatorOutputFault({lookupAnd(*circuit_, var), stuckAt(stuck)});
    } else {
        throw std::invalid_argument("usage: fault_ma pin <var> <pin> <sa0|sa1> or fault_ma dom <var> <sa0|sa1>");
    }
    std::vector<sat::DimacsLiteral> decisions;
    std::string flag;
    bool exact = false;
    while (input >> flag) {
        if (flag == "exact") {
            exact = true;
        } else if (flag == "trial") {
            std::int64_t signedVar = 0;
            if (!(input >> signedVar) || signedVar == 0 || signedVar > INT32_MAX || signedVar < -INT32_MAX) {
                throw std::invalid_argument("trial expects a nonzero signed AAG variable");
            }
            const auto id = static_cast<std::uint32_t>(signedVar < 0 ? -signedVar : signedVar);
            const auto encoded = sat::CnfEncoder::positiveLit(context.cnf(), id);
            if (!encoded) throw std::invalid_argument("unknown trial AAG variable");
            decisions.push_back(signedVar < 0 ? -*encoded : *encoded);
        } else {
            throw std::invalid_argument("optional arguments: exact or trial <signed_aig_var>");
        }
    }
    // A trial makes the result conditional: show how it differs from the unconditional base MAs.
    std::unordered_map<sat::DimacsLiteral, bool> baseLits;
    if (!decisions.empty()) {
        for (const auto& item : context.computeMandatoryAssignments().items()) baseLits.emplace(item.lit, true);
        out_ << "base_ma_entries=" << baseLits.size() << '\n';
    }
    const auto ma = context.computeMandatoryAssignments(decisions);
    const auto state = context.status();
    out_ << "ma_entries=" << ma.size() << " fault=" << which << " status="
         << (state == analysis::FaultStatus::Conflict ? "conflict" :
             state == analysis::FaultStatus::NeedsExactTest ? "exact_miter_projection" : "consistent") << '\n';
    const auto& prop = context.propagator();
    std::unordered_map<std::uint32_t, std::uint32_t> aigVars;
    for (const auto& item : context.cnf().aigToSat()) aigVars.emplace(item.second, item.first);
    for (const auto& assignment : ma.items()) {
        out_ << "  dimacs=" << assignment.lit << " level=" << assignment.contextLevel
             << (decisions.empty() ? "" : baseLits.count(assignment.lit) ? " conditional=0" : " conditional=1")
             << " aig_var=" << aigVars.at(sat::dimacsVariable(assignment.lit)) << " reason=";
        for (const auto lit : prop.reason(assignment.lit)) out_ << lit << ',';
        out_ << '\n';
    }
    if (exact) {
        const auto result = sat::MinisatVerificationSolver::solve(context.faultDetectionFormula(decisions));
        out_ << "fault_test=" << sat::satVerdictName(result.verdict) << '\n';
    }
    return true;
}

bool CommandShell::commandRarSearch(std::uint32_t var, unsigned pin, const std::string& stuck,
                                   rar::SatRarOptions options, const std::string& mode,
                                   bool verify, std::uint32_t timeout) {
    if (!circuit_) throw std::logic_error("no circuit loaded");
    analysis::FaultContext context(circuit_->clone());
    rar::SatRarEngine engine(context);
    if (trace_) {
        options.trace = [this](const std::string& line) { out_ << "TRACE\t" << line << '\n'; };
    }
    engine.setOptions(options);
    engine.setPinTarget(pinFault(*circuit_, var, pin, stuck));
    const auto start = std::chrono::steady_clock::now();
    candidates_ = engine.run();
    const auto& stats = engine.statistics();
    out_ << "alternatives=" << candidates_.size() << " mode=" << mode
         << " search_seconds=" << Diagnostics::elapsedSeconds(start)
         << " trials=" << stats.trials << " learned=" << stats.learned
         << " learned_units=" << stats.learnedUnits
         << " filtered=" << stats.filtered << " limited=" << stats.limitReached
         << " limit_step=" << (stats.limitReached ? stats.limitStep : "none")
         << " limit_scope=" << (stats.limitReached ? stats.limitScope : "none")
         << " redundant_target=" << stats.targetConflict
         << " redundant_dominators=" << stats.redundantDominators
         << " max_depth=" << stats.maxDepth << " sub_probes=" << stats.subProbes
         << " sub_reductions=" << stats.subReductions << '\n';
    const auto backend = sat::defaultVerificationBackend();
    const rar::RarVerifier verifier(*backend);
    for (std::size_t i = 0; i < candidates_.size(); ++i) {
        out_ << "  candidate[" << i << "] " << describeCandidate(*circuit_, candidates_[i]) << '\n';
        if (verify) {
            const auto result = verifier.verify(*circuit_, candidates_[i], timeout);
            out_ << "rar_verify=" << acceptanceLabel(result) << " candidate=" << i
                 << " addition=" << rar::equivalenceVerdictName(result.addition.verdict)
                 << " removal=" << (result.removal ? rar::equivalenceVerdictName(result.removal->verdict) : "not_run")
                 << " acceptance=" << rar::rarAcceptanceName(result.acceptance)
                 << " failed_stage=" << rar::rarStageName(result.failedStage)
                 << " seconds=" << result.verifySeconds << ' ' << result.message << '\n';
        }
    }
    return true;
}

bool CommandShell::commandRarApply(std::size_t index, const std::string& path, std::uint32_t timeout) {
    if (!circuit_ || index >= candidates_.size() || path.empty()) {
        throw std::invalid_argument("run a search and provide a listed candidate ID and output path");
    }
    const auto backend = sat::defaultVerificationBackend();
    const auto result = rar::RarVerifier(*backend).verify(*circuit_, candidates_[index], timeout);
    if (!result.rewritten) {
        Diagnostics::error(err_, result.message);
        return false;
    }
    rar::ExportOptions options;
    options.timeoutSeconds = timeout;
    const auto exported = rar::exportVerifiedAag(*circuit_, *result.rewritten, path, *backend, options);
    if (!exported.published) {
        Diagnostics::error(err_, exported.message);
        return false;
    }
    out_ << "rar_apply=verified_equivalent candidate=" << index << '\n';
    return true;
}

bool CommandShell::commandRarBatch(rar::SatRarOptions options, std::size_t limit,
                                  std::uint32_t timeout, bool runCec, bool summaryOnly,
                                  std::size_t offset) const {
    if (!circuit_) throw std::logic_error("no circuit loaded");
    analysis::FaultContext context(circuit_->clone());
    rar::SatRarEngine engine(context);
    engine.setOptions(options);
    const auto backend = sat::defaultVerificationBackend();
    const rar::RarVerifier verifier(*backend);
    std::size_t visited = 0, targets = 0, alternatives = 0, verifiedTargets = 0, verified = 0;
    std::size_t rejected = 0, unverified = 0, limited = 0, complete = 0, redundantTargets = 0;
    std::size_t trials = 0;
    double search = 0, verify = 0, profSetup = 0, profProjection = 0, profTrials = 0, profCanonical = 0;
    std::size_t wireIndex = 0;
    for (const auto id : circuit_->topologicalOrder()) {
        if (!circuit_->nodes()[id].isAnd()) continue;
        for (const auto wire : circuit_->faninWires(id)) {
            if (wireIndex++ < offset) continue;
            if (limit && visited >= limit) break;
            ++visited;
            bool found = false, accepted = false, pinLimited = false;
            for (const auto fault : {analysis::StuckAt::Zero, analysis::StuckAt::One}) {
                engine.setPinTarget({wire, fault});
                context.resetSolver();
                const auto start = std::chrono::steady_clock::now();
                const auto candidates = engine.run();
                const auto elapsed = Diagnostics::elapsedSeconds(start);
                search += elapsed;
                found |= !candidates.empty();
                alternatives += candidates.size();
                const auto& stats = engine.statistics();
                limited += stats.limitReached;
                pinLimited |= stats.limitReached;
                redundantTargets += stats.targetConflict;
                trials += stats.trials;
                profSetup += stats.profileSetup;
                profProjection += stats.profileProjection;
                profTrials += stats.profileTrials;
                profCanonical += stats.profileCanonical;
                const auto var = circuit_->nodes()[id].aagVar;
                const auto pin = static_cast<unsigned>(wire.inputPin);
                if (!summaryOnly) {
                    out_ << "RAR_TARGET\t" << var << '\t' << pin << '\t' << static_cast<int>(fault)
                         << '\t' << candidates.size() << '\t' << elapsed
                         << '\t' << stats.limitReached
                         << '\t' << stats.targetConflict
                         << '\t' << stats.redundantDominators
                         << '\t' << stats.trials
                         << '\t' << stats.subProbes
                         << '\t' << (stats.limitReached ? stats.limitStep : "none")
                         << '\t' << (stats.limitReached ? stats.limitScope : "none") << '\n';
                }
                if (summaryOnly && !runCec) {
                    unverified += candidates.size();
                    continue;
                }
                for (const auto& candidate : candidates) {
                    if (!runCec) {
                        ++unverified;
                        out_ << "RAR_CANDIDATE\t" << var << '\t' << pin << '\t' << static_cast<int>(fault)
                             << '\t' << describeCandidate(*circuit_, candidate)
                             << "\tnot_checked\tnot_run\tnot_run\tnone\t0\n";
                        continue;
                    }
                    const auto answer = verifier.verify(*circuit_, candidate, timeout);
                    const bool passed = answer.acceptance == rar::RarAcceptance::Accepted;
                    verified += passed;
                    rejected += answer.acceptance == rar::RarAcceptance::Rejected;
                    unverified += answer.acceptance == rar::RarAcceptance::Unverified;
                    accepted |= passed;
                    verify += answer.verifySeconds;
                    if (summaryOnly) continue;
                    out_ << "RAR_CANDIDATE\t" << var << '\t' << pin << '\t' << static_cast<int>(fault)
                         << '\t' << describeCandidate(*circuit_, candidate)
                         << '\t' << acceptanceLabel(answer) << '\t'
                         << rar::equivalenceVerdictName(answer.addition.verdict) << '\t'
                         << (answer.removal ? rar::equivalenceVerdictName(answer.removal->verdict) : "not_run")
                         << '\t' << rar::rarStageName(answer.failedStage)
                         << '\t' << answer.verifySeconds << '\n';
                }
            }
            targets += found;
            verifiedTargets += accepted;
            complete += !pinLimited;
        }
        if (limit && visited >= limit) break;
    }
    // Field order is fixed: scripts/rar_batch.py SUMMARY.
    out_ << "RAR_SUMMARY\t" << circuit_->stats().wires << '\t' << visited << '\t' << complete << '\t'
         << targets << '\t' << alternatives << '\t' << search << '\t' << verifiedTargets
         << '\t' << verified << '\t' << rejected << '\t' << unverified << '\t' << limited
         << '\t' << redundantTargets << '\t' << trials << '\t' << verify
         << '\t' << profSetup << '\t' << profProjection << '\t' << profTrials << '\t' << profCanonical << '\n';
    return true;
}
#endif

}  // namespace satrar::app
