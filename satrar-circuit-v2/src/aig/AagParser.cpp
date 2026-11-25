#include "satrar/aig/AagParser.hpp"

#include <fstream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace satrar::aig {

namespace {

[[noreturn]] void throwParse(const std::string& file, int line, const std::string& msg) {
    std::ostringstream os;
    os << file << ':' << line << ": " << msg;
    throw std::runtime_error(os.str());
}

struct RawLine {
    int number = 0;
    std::string text;
};

std::vector<RawLine> loadLines(const std::string& content) {
    std::vector<RawLine> lines;
    std::istringstream in(content);
    std::string line;
    int no = 0;
    while (std::getline(in, line)) {
        ++no;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back({no, line});
    }
    return lines;
}

Literal parseLiteralToken(const std::string& tok, const std::string& file, int line) {
    if (tok.empty()) {
        throwParse(file, line, "expected literal");
    }
    std::size_t idx = 0;
    unsigned long long value = 0;
    try {
        value = std::stoull(tok, &idx, 10);
    } catch (...) {
        throwParse(file, line, "invalid literal '" + tok + "'");
    }
    if (idx != tok.size() || value > UINT32_MAX || tok.front() == '-') {
        throwParse(file, line, "invalid literal '" + tok + "'");
    }
    return Literal(static_cast<std::uint32_t>(value));
}

struct PendingAnd {
    std::uint32_t lhsVar = 0;
    Literal left;
    Literal right;
};

bool isStructuralNumberLine(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    const char c = text[0];
    return c >= '0' && c <= '9';
}

}  // namespace

Circuit AagParser::parseString(const std::string& content, const std::string& diagnosticName) {
    const std::vector<RawLine> lines = loadLines(content);
    if (lines.empty()) {
        throwParse(diagnosticName, 1, "empty file");
    }

    std::uint32_t numInputs = 0, numLatches = 0, numOutputs = 0, numAnds = 0;
    std::uint32_t numBad = 0, numInvariant = 0, numJustice = 0, numFair = 0;

    {
        std::istringstream hdr(lines[0].text);
        std::string magic;
        std::uint32_t maxVar = 0;
        hdr >> magic;
        if (magic != "aag") {
            throwParse(diagnosticName, lines[0].number,
                       "expected 'aag' header (binary .aig not supported)");
        }
        hdr >> maxVar >> numInputs >> numLatches >> numOutputs >> numAnds;
        if (!hdr) {
            throwParse(diagnosticName, lines[0].number, "malformed header");
        }
        if (hdr >> numBad) {
            (void)numBad;
            hdr >> numInvariant >> numJustice >> numFair;
        }
        if (numLatches > 0) {
            throwParse(diagnosticName, lines[0].number, "latches not supported in combinational-only parser");
        }
        if (numBad > 0 || numInvariant > 0 || numJustice > 0 || numFair > 0) {
            throwParse(diagnosticName, lines[0].number,
                       "non-zero AIGER property fields are not supported in v1");
        }
        (void)maxVar;
    }

    std::size_t cursor = 1;
    auto takeNumberLine = [&](const char* section) -> Literal {
        while (cursor < lines.size()) {
            const RawLine& row = lines[cursor];
            if (isStructuralNumberLine(row.text)) {
                ++cursor;
                return parseLiteralToken(row.text, diagnosticName, row.number);
            }
            if (row.text.empty() || row.text[0] == 'c') {
                ++cursor;
                continue;
            }
            break;
        }
        throwParse(diagnosticName, lines.back().number, std::string("missing ") + section);
    };

    std::vector<Literal> inputs;
    inputs.reserve(numInputs);
    for (std::uint32_t i = 0; i < numInputs; ++i) {
        Literal lit = takeNumberLine("primary input");
        if (lit.isConstant() || lit.inverted()) {
            const int errLine = cursor > 0 ? lines[cursor - 1].number : 1;
            throwParse(diagnosticName, errLine, "PI literal must be positive and nonconstant");
        }
        inputs.push_back(lit);
    }

    std::vector<Literal> outputs;
    outputs.reserve(numOutputs);
    for (std::uint32_t i = 0; i < numOutputs; ++i) {
        outputs.push_back(takeNumberLine("primary output"));
    }

    std::vector<PendingAnd> pending;
    pending.reserve(numAnds);
    for (std::uint32_t i = 0; i < numAnds; ++i) {
        while (cursor < lines.size()) {
            const RawLine& row = lines[cursor];
            if (isStructuralNumberLine(row.text)) {
                break;
            }
            if (row.text.empty() || row.text[0] == 'c') {
                ++cursor;
                continue;
            }
            throwParse(diagnosticName, row.number, "expected AND gate definition");
        }
        if (cursor >= lines.size()) {
            throwParse(diagnosticName, lines.back().number, "missing AND gate");
        }
        const RawLine& andLine = lines[cursor++];
        std::istringstream row(andLine.text);
        std::string a, b, c;
        row >> a >> b >> c;
        if (!row || a.empty() || b.empty() || c.empty()) {
            throwParse(diagnosticName, andLine.number, "malformed AND line");
        }
        PendingAnd gate;
        const Literal lhs = parseLiteralToken(a, diagnosticName, andLine.number);
        if (lhs.isConstant() || lhs.inverted()) {
            throwParse(diagnosticName, andLine.number,
                       "AND output literal must be a positive even literal");
        }
        gate.lhsVar = lhs.variable();
        gate.left = parseLiteralToken(b, diagnosticName, andLine.number);
        gate.right = parseLiteralToken(c, diagnosticName, andLine.number);
        pending.push_back(gate);
    }

    Circuit circuit;
    circuit.addConstantNode(false);
    circuit.addConstantNode(true);
    for (const Literal lit : inputs) {
        circuit.addPrimaryInput(lit.variable());
    }

    std::unordered_map<std::uint32_t, bool> defined;
    for (const Literal lit : inputs) {
        defined[lit.variable()] = true;
    }

    std::unordered_map<std::uint32_t, std::size_t> gates;
    std::vector<std::vector<std::size_t>> successors(pending.size());
    std::vector<unsigned> inDegree(pending.size());
    std::queue<std::size_t> ready;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto var = pending[i].lhsVar;
        if (defined.count(var) || !gates.emplace(var, i).second) {
            throwParse(diagnosticName, 0, "duplicate AND variable");
        }
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
        for (const auto input : {pending[i].left, pending[i].right}) {
            if (input.isConstant() || defined.count(input.variable())) {
                continue;
            }
            const auto driver = gates.find(input.variable());
            if (driver == gates.end()) {
                throwParse(diagnosticName, 0, "undefined AND input");
            }
            successors[driver->second].push_back(i);
            ++inDegree[i];
        }
        if (!inDegree[i]) {
            ready.push(i);
        }
    }
    std::size_t added = 0;
    while (!ready.empty()) {
        const auto i = ready.front();
        ready.pop();
        const auto& gate = pending[i];
        circuit.addAndGate(gate.lhsVar, gate.left, gate.right);
        ++added;
        for (const auto next : successors[i]) {
            if (!--inDegree[next]) {
                ready.push(next);
            }
        }
    }
    if (added != pending.size()) {
        throwParse(diagnosticName, 0, "cyclic AND definitions");
    }

    for (const Literal lit : outputs) {
        circuit.addPrimaryOutput(lit);
    }

    bool inComment = false;
    std::string comment;
    while (cursor < lines.size()) {
        const RawLine& row = lines[cursor++];
        if (row.text.empty()) {
            continue;
        }
        if (inComment) {
            if (!comment.empty()) {
                comment += '\n';
            }
            comment += row.text;
            continue;
        }
        if (row.text[0] == 'c') {
            if (row.text.size() == 1) {
                inComment = true;
                continue;
            }
            if (row.text[1] == ' ') {
                circuit.setComment(row.text.substr(2));
            }
            continue;
        }
        if (row.text[0] == 'i' || row.text[0] == 'o') {
            const auto split = row.text.find(' ');
            if (split == std::string::npos || split <= 1 || split + 1 == row.text.size()) {
                throwParse(diagnosticName, row.number, "invalid port symbol");
            }
            const auto index = parseLiteralToken(row.text.substr(1, split - 1), diagnosticName, row.number).raw();
            if (row.text[0] == 'i' && index < numInputs) {
                circuit.setInputSymbol(index, row.text.substr(split + 1));
            } else if (row.text[0] == 'o' && index < numOutputs) {
                circuit.setOutputSymbol(index, row.text.substr(split + 1));
            } else {
                throwParse(diagnosticName, row.number, "port symbol index out of range");
            }
            continue;
        }
        if (row.text[0] == 'a') {
            continue;
        }
        throwParse(diagnosticName, row.number, "unexpected trailing line");
    }
    if (!comment.empty()) {
        circuit.setComment(comment);
    }

    circuit.validate();
    return circuit;
}

Circuit AagParser::parseFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open '" + path + "'");
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return parseString(buf.str(), path);
}

}  // namespace satrar::aig
