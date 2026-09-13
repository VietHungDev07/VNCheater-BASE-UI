#pragma once
#include <filesystem>
#include <memory>
#include <windows.h>

// A native per-pixel-alpha surface. No browser, background brush or window chrome.
class SplashWindow final {
  public:
    SplashWindow();
    ~SplashWindow();
    SplashWindow(const SplashWindow &) = delete;
    SplashWindow &operator=(const SplashWindow &) = delete;

    bool Start(HINSTANCE instance, HWND notifyWindow, const std::filesystem::path &logoPath,
               UINT readyMessage);
    void Finish();
    void Close();
    HWND Handle() const;

    // Uses the exact production renderer without showing a window.
    static bool ExportFrames(const std::filesystem::path &logoPath,
                             const std::filesystem::path &outputDirectory);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
