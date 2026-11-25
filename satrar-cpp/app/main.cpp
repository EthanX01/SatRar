#include "satrar/app/CommandShell.hpp"

#include <iostream>
#include <string>

namespace {

void printUsage(const char* program) {
    std::cout
        << "satrar-cpp - SAT-Controlled Redundancy Addition and Removal (SatRAR)\n"
        << "Wu/Lin/Huang/Huang ASPDAC 2009 (incremental reimplementation)\n"
        << "\n"
        << "Usage:\n"
        << "  " << program << " [options] [circuit.aag]\n"
        << "\n"
        << "Options:\n"
        << "  --help, -h       Show this help and exit.\n"
        << "  --version, -v    Show build identity.\n"
        << "  --shell          Interactive command shell (default if no args).\n"
        << "  --verbose        Enable [satrar][INFO] diagnostics on stderr.\n"
        << "\n"
        << "Solvers:\n"
        << "  Native AIG propagation - SatRAR mandatory assignments and reasons\n"
        << "  MiniSat                - exact good/faulty SAT queries\n"
        << "  CaDiCaL                - two-stage equivalence checks\n";
}

void printVersion() {
    std::cout << "satrar-cpp 0.3.0\n";
}

bool isHelpFlag(const std::string& arg) {
    return arg == "--help" || arg == "-h" || arg == "/?";
}

bool isVersionFlag(const std::string& arg) {
    return arg == "--version" || arg == "-v";
}

bool isShellFlag(const std::string& arg) {
    return arg == "--shell";
}

bool isVerboseFlag(const std::string& arg) {
    return arg == "--verbose";
}

}  // namespace

int main(int argc, char* argv[]) {
    const char* program = (argc > 0 && argv[0] != nullptr) ? argv[0] : "satrar";
    bool verboseCli = false;
    std::string preloadPath;

    if (argc == 1) {
        satrar::app::CommandShell shell;
        if (verboseCli) {
            shell.setVerbose(true);
        }
        shell.run();
        return 0;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (isHelpFlag(arg)) {
            printUsage(program);
            return 0;
        }
        if (isVersionFlag(arg)) {
            printVersion();
            return 0;
        }
        if (isShellFlag(arg)) {
            continue;
        }
        if (isVerboseFlag(arg)) {
            verboseCli = true;
            continue;
        }
        if (arg.size() > 0 && arg[0] == '-') {
            std::cerr << "[satrar][ERROR] unknown option: " << arg << "\n"
                      << "Try \"" << program << " --help\".\n";
            return 2;
        }

        preloadPath = arg;
    }

    satrar::app::CommandShell shell;
    shell.setVerbose(verboseCli);
    if (!preloadPath.empty()) {
        if (!shell.executeLine(std::string("read ") + preloadPath)) {
            return 1;
        }
    }
    shell.run();
    return 0;
}
