#include "SplashWindow.h"
#include <iostream>

int wmain(int argc, wchar_t **argv) {
    if (argc != 3) {
        std::wcerr << L"Usage: SplashPreview.exe logo.png output-directory\n";
        return 2;
    }
    const bool ok = SplashWindow::ExportFrames(argv[1], argv[2]);
    std::cout << (ok ? "Splash frames exported; alpha checks passed.\n"
                     : "Splash export/alpha validation failed.\n");
    return ok ? 0 : 1;
}
