#include "satrar/app/CommandShell.hpp"

#include <iostream>
#include <string>

namespace {

void printUsage(const char* program) {
    std::cout
        << "satrar-v3 - SatRAR with ABC as the only equivalence checker\n"
        << "\n"
        << "Usage:\n"
        << "  " << program << " [options] [circuit.aag]\n"
        << "\n"
        << "Options:\n"
        << "  --help, -h       Show this help and exit.\n"
        << "  --version, -v    Show build identity.\n"
        << "  --shell          Interactive command shell (default if no args).\n"
        << "  --verbose        Enable diagnostics on stderr.\n"
        << "\n"
        << "Equivalence checks run berkeley-abc (&cec). Search still uses the native propagator\n"
        << "and MiniSat. Set SATRAR_ABC_EXE to override the ABC executable.\n";
}

void printVersion() { std::cout << "satrar-v3 0.1.0\n"; }

}  // namespace

int main(int argc, char* argv[]) {
    const char* program = (argc > 0 && argv[0] != nullptr) ? argv[0] : "satrar";
    bool verbose = false;
    std::string preload;
    if (argc == 1) {
        satrar::app::CommandShell shell;
        shell.run();
        return 0;
    }
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h" || arg == "/?") {
            printUsage(program);
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            printVersion();
            return 0;
        }
        if (arg == "--shell") continue;
        if (arg == "--verbose") {
            verbose = true;
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "[satrar][ERROR] unknown option: " << arg << "\n";
            return 2;
        }
        preload = arg;
    }
    satrar::app::CommandShell shell;
    shell.setVerbose(verbose);
    if (!preload.empty() && !shell.executeLine("read " + preload)) return 1;
    shell.run();
    return 0;
}
