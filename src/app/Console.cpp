#include "Console.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

void UseUtf8Console() {
#ifdef _WIN32
    // The process code page is UTF-8 through the manifest, but the console keeps its own OEM code page.
    SetConsoleOutputCP(CP_UTF8);
#endif
}
