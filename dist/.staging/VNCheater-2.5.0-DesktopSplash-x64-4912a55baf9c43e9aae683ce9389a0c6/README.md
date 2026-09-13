# VNCheater — Strinova Launcher 2.5.0

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

Run `./scripts/package.ps1` after building to create `dist/VNCheater-2.5.0-DesktopSplash-x64.zip`.
The package script rejects stale UI files and includes `SHA256SUMS.txt`.

The script pins Microsoft.Web.WebView2 **1.0.4191.47**, downloads its NuGet package when needed and verifies its SHA-256. Loader and C++ runtime are statically linked; the Evergreen browser runtime is a separate prerequisite. The generated Visual Studio solution lives in `build/`.

## Design

- Updated user-supplied backgrounds and dragon logo in `assets/images` are preserved.
- Sea-blue glass, aqua/ivory controls and restrained warm highlights match the daylight/rain artwork.
- Startup splash is a native, borderless layered window. It renders the supplied dragon mark over the desktop with a transparent surround, rotating rings, glow, scanline and particle pass. During the final splash tail the 1336 × 768 launcher window crossfades in, then the splash closes. The source logo is not rewritten.
- Startup sequence: native splash timeline → hidden WebView2 creation/navigation → frontend posts `ui-ready` → host crossfades the launcher window in alongside the splash tail → host posts `begin-loading` → `Check Auto Update` mock progress → authentication. This keeps the logo from replaying inside WebView2.
- Browser preview retains the HTML intro for visual review. In the desktop host, the frontend starts on the updater at 0% and waits for the native handshake.
- Rain appears over intro/auth; sunny scenes have slow light motes and soft rays.
- Subtle pointer depth. Shader output capped at 30 fps; Balanced uses 20 fps. Animation pauses while minimized or hidden.
- Appearance modal: keyboard-operated listbox for Cinematic / Balanced / Still. Only appearance is stored in localStorage.
- Reduced-motion and unavailable WebGL use still artwork. GPU resources are reused. Replay from logo or Appearance.
- System fonts and local assets; no remote font dependency.

## Source map

- `native/main.cpp`: Win32 lifetime, native splash/WebView2 handshake, DPI, window commands, folder picker, external link allowlist.
- `native/SplashWindow.*`: borderless transparent startup splash renderer and animation timeline.
- `native/app.manifest`: DPI and standard-user execution.
- `index.html`, `css/style.css`: screens, dialogs, styling.
- `js/app.js`: UI state machine, mock flows and narrow native bridge.
- `js/effects.js`: GLSL, texture handling, motion preferences and renderer lifecycle.

The host maps only its `ui` folder to `https://launcher.vncheater.local/`. Native messages are accepted only from the exact local entry page, and only named window/folder commands are allowed. External navigation is blocked except for the configured social links, which open outside WebView. No host objects or unrestricted file/system APIs are exposed to JavaScript.

Logs/profile: `%LOCALAPPDATA%/VNCheater/WebView2/`. Logs record startup/navigation/shutdown results, not keys or selected paths.

## Validation notes (2026-09-13)

The build script configures an x64 Release build and copies the current HTML/CSS/JS/assets into `build/Release/ui`. Run it from the repository root with `./scripts/build.ps1`. The launcher requires the WebView2 Evergreen Runtime at runtime.

- Release x64 builds successfully with MSVC; both JavaScript files pass `node --check`.
- The production native renderer exported five representative frames. Every frame has fully transparent outer edges and valid premultiplied alpha. The rendered logo was inspected on a checkerboard; this is an offscreen renderer preview, not a desktop screenshot.
- The EXE was launched locally. Runtime logs confirm `main_visible=0`, `alpha=0`, `splash_visible=1` throughout the 3.8-second intro and WebView initialization. The main reveal completes at `alpha=255`, with the splash gone. Accessibility confirms the Strinova authentication page loads. Closing with Alt+F4 fades to alpha 0 and shuts down cleanly.
- Desktop screenshot capture is unavailable on this machine: the capture tool reports `SetIsBorderRequired failed: 0x80004002`. Desktop visual composition, Replay interaction, folder picker, drag/resize and multiple DPI settings have not been visually validated end to end.

To reproduce the renderer checks without showing a window:

```powershell
cmake --build build --config Release --target SplashPreview
./build/Release/SplashPreview.exe ./assets/images/logo.png ./build/splash-preview
```

The exported PNGs retain transparency; the `-checker.png` files add a checkerboard for inspection only. The updater, authentication and installation actions are frontend simulations; no game files or processes are touched.

## Microsoft references

- [WebView2 SDK](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4191.47)
- [Win32 virtual host mapping](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2_3?view=webview2-1.0.4022.49)
- [WebView2 security guidance](https://github.com/MicrosoftDocs/edge-developer/blob/main/microsoft-edge/webview2/concepts/security.md)
- [UpdateLayeredWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow)
- [Premultiplied alpha blending](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-blendfunction)
