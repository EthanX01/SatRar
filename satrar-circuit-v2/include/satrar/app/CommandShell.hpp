#pragma once

#include "satrar/analysis/FaultContext.hpp"
#include "satrar/rar/Alternative.hpp"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace satrar::app {

class CommandShell {
public:
    explicit CommandShell(std::istream& in = std::cin, std::ostream& out = std::cout,
                          std::ostream& err = std::cerr);

    void run(bool interactive = false);
    bool executeLine(const std::string& line);

private:
    std::istream& in_;
    std::ostream& out_;
    std::ostream& err_;
    std::unique_ptr<analysis::FaultContext> context_;
    std::vector<rar::Alternative> candidates_;

    void printHelp() const;
    bool commandRead(const std::string& path);
    void commandFaultMa(std::istringstream& rest);
    void commandRar(const std::string& command, std::istringstream& rest);
    void commandApply(std::istringstream& rest);
    void commandSat(std::istringstream& rest);
    void commandExact(std::istringstream& rest);
    void commandMaCheck(std::istringstream& rest);
    [[nodiscard]] analysis::PinFault parsePin(std::istringstream& rest) const;
    [[nodiscard]] static aig::Literal signedVariable(const std::string& token);
};

}  // namespace satrar::app
