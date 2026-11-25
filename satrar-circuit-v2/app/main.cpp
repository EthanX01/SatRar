#include "satrar/app/CommandShell.hpp"

#include <cstdio>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

int main() {
#ifdef _WIN32
    const bool interactive = _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
    const bool interactive = isatty(fileno(stdin)) && isatty(fileno(stdout));
#endif
    satrar::app::CommandShell shell;
    shell.run(interactive);
    return 0;
}
