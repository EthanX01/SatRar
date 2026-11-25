#pragma once

#include "satrar/aig/Circuit.hpp"

#include <string>

namespace satrar::aig {

struct ParseError {
    std::string message;
    std::string file;
    int line = -1;
};

class AagParser {
public:
    static Circuit parseFile(const std::string& path);
    static Circuit parseString(const std::string& content, const std::string& diagnosticName = "<string>");
};

}  // namespace satrar::aig