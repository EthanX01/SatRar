#pragma once

#include "satrar/aig/Circuit.hpp"
#include "satrar/app/CecSettings.hpp"
#include "satrar/sat/CnfFormula.hpp"

#if defined(SATRAR_HAS_MINISAT) && SATRAR_HAS_MINISAT
#include "satrar/rar/SatRarEngine.hpp"
#endif

#include <iostream>
#include <sstream>
#include <optional>
#include <string>

namespace satrar::app {

class CommandShell {
public:
    explicit CommandShell(std::istream& in = std::cin, std::ostream& out = std::cout,
                            std::ostream& err = std::cerr);

    void setVerbose(bool on) noexcept;

    void run();
    bool executeLine(const std::string& line);

    [[nodiscard]] bool hasCircuit() const noexcept { return circuit_.has_value(); }

private:
    bool verbose_ = false;
    bool trace_ = false;  ///< Emit TRACE lines from rar_* searches (never used by rar_batch).
    std::istream& in_;
    std::ostream& out_;
    std::ostream& err_;
    std::optional<aig::Circuit> circuit_;

    void printHelp() const;
    bool commandRead(const std::string& path);
    void commandStats() const;
    void commandInputs() const;
    void commandOutputs() const;
    void commandGate(std::uint32_t aigVar) const;
    void commandFanin(std::uint32_t aigVar) const;
    void commandFanout(std::uint32_t aigVar) const;
    void commandSim(const std::string& bits) const;
    bool commandWriteCnf(const std::string& path) const;
    bool commandWriteAag(const std::string& path) const;
#if defined(SATRAR_HAS_MINISAT) && SATRAR_HAS_MINISAT
    bool commandFaultMa(const std::string& which, std::istringstream& rest) const;
#endif
    bool commandSatSolve(const std::string& cnfPath, const std::string& timeoutTok) const;
    bool commandVerifyEquiv(const std::string& path, const std::string& timeoutTok) const;
    bool commandSet(const std::string& name, std::istringstream& rest);
    bool commandShowCec() const;
    CecSettings cec_;
#if defined(SATRAR_HAS_MINISAT) && SATRAR_HAS_MINISAT
    std::vector<rar::Alternative> candidates_;
    bool commandRarSearch(std::uint32_t andAigVar, unsigned pin, const std::string& stuck,
                          rar::SatRarOptions options, const std::string& mode, bool verify,
                          std::uint32_t timeout);
    bool commandRarApply(std::size_t index, const std::string& path, std::uint32_t timeout);
    bool commandRarBatch(rar::SatRarOptions options, std::size_t limit,
                         std::uint32_t timeout, bool verify, bool summaryOnly,
                         std::size_t offset, bool bench = false) const;
    bool commandRarDump(rar::SatRarOptions options, const std::string& outDir,
                        std::size_t maxCandidates, std::size_t offset) const;
#endif
    bool commandVerbose(const std::string& onOff);

    static std::string trim(const std::string& s);
};

}  // namespace satrar::app
