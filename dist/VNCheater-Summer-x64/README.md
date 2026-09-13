# VNCheater — Summer Launcher

C++17 / Win32 desktop shell with Microsoft Edge WebView2 and a local HTML/CSS/JavaScript interface. Default window: **1336 × 768 logical pixels**, centered and capped to the available work area. Per-monitor DPI aware; runs without elevation.

## Run

Extract the entire release archive, then launch `VNCheater.exe`. Keep the `ui` directory next to the executable. No Python, Node.js, local server, or Internet connection is required for the interface. Social links open in the default browser.

Microsoft Edge **WebView2 Evergreen Runtime (x64)** must be installed. Microsoft download: https://developer.microsoft.com/microsoft-edge/webview2/

This is a **UI preview**: any nonempty test key opens the launcher. Updater and installation actions are simulated. C++ provides window controls and a read-only Windows folder picker. It does not install game components or modify the selected folder. Keys are neither stored nor transmitted.

## Build

Requirements: Visual Studio C++ desktop workload, Windows SDK, CMake 3.24+, PowerShell.

```powershell
./scripts/build.ps1
```

Output: `build/Release/VNCheater.exe` and `build/Release/ui/`.

The script pins Microsoft.Web.WebView2 **1.0.4191.47**, downloads its NuGet package when needed and verifies its SHA-256. Loader and C++ runtime are statically linked; the Evergreen browser runtime is a separate prerequisite. The generated Visual Studio solution lives in `build/`.

## Design

- Updated user-supplied backgrounds and dragon logo in `assets/images` are preserved.
- Sea-blue glass, aqua/ivory controls and restrained warm highlights match the daylight/rain artwork.
- Intro: GLSL logo texture reveal, rotating rim light, soft halo, water reflection, wordmark entrance and outgoing light sweep. The source JPEG is not rewritten.
- Rain appears over intro/auth; sunny scenes have slow light motes and soft rays.
- Subtle pointer depth. Shader output capped at 30 fps; Balanced uses 20 fps. Animation pauses while minimized or hidden.
- Appearance modal: keyboard-operated listbox for Cinematic / Balanced / Still. Only appearance is stored in localStorage.
- Reduced-motion and unavailable WebGL use still artwork. GPU resources are reused. Replay from logo or Appearance.
- System fonts and local assets; no remote font dependency.

## Source map

- `native/main.cpp`: Win32 lifetime, DPI, WebView2, window commands, folder picker, external link allowlist.
- `native/app.manifest`: DPI and standard-user execution.
- `index.html`, `css/style.css`: screens, dialogs, styling.
- `js/app.js`: UI state machine, mock flows and narrow native bridge.
- `js/effects.js`: GLSL, texture handling, motion preferences and renderer lifecycle.

The host maps only its `ui` folder to `https://launcher.vncheater.local/`. Native messages are accepted only from the exact local entry page, and only named window/folder commands are allowed. External navigation is blocked except for the configured social links, which open outside WebView. No host objects or unrestricted file/system APIs are exposed to JavaScript.

Logs/profile: `%LOCALAPPDATA%/VNCheater/WebView2/`. Logs record startup/navigation/shutdown results, not keys or selected paths.

## Validation

- Release x64 built with MSVC 19.51 / Windows SDK 10.0.28000.0, no reported compiler warnings.
- Native executable launched; WebView2 logged successful local navigation; Windows accessibility exposed the login screen.
- Browser render inspected at 1336 × 768. Logo shader initialization observed. No page overflow at that viewport.
- User stopped further Computer Use. Native folder selection, minimize/close/drag, full modal keyboard interaction and multi-DPI behavior are **not end-to-end verified**. Native screenshot capture unavailable (`SetIsBorderRequired`, E_NOINTERFACE).

## Microsoft references

- [WebView2 SDK](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4191.47)
- [Win32 virtual host mapping](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2_3?view=webview2-1.0.4022.49)
- [WebView2 security guidance](https://github.com/MicrosoftDocs/edge-developer/blob/main/microsoft-edge/webview2/concepts/security.md)
