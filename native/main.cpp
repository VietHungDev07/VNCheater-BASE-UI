#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <algorithm>
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <WebView2.h>
#include "SplashWindow.h"
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace {
constexpr wchar_t kPage[] = L"https://launcher.vncheater.local/index.html";
constexpr UINT kPickFolder = WM_APP + 1;
constexpr UINT kDrag = WM_APP + 2;
constexpr UINT kSplashReady = WM_APP + 3;
constexpr UINT kRevealTick = WM_APP + 4;
constexpr UINT kRevealWatchdog = WM_APP + 5;
constexpr UINT kCloseTick = WM_APP + 6;
HWND window = nullptr;
SplashWindow splash;
ComPtr<ICoreWebView2Controller> controller;
ComPtr<ICoreWebView2> web;
fs::path profile, uiFolder;
bool closing = false, pickerOpen = false, navReady = false, uiReady = false, replayPending = false;
int alpha = 0;
int closeStartAlpha = 0;
ULONGLONG transitionStarted = 0;
enum class Stage { Splash, CreatingWebView, WaitingForUi, Revealing, Running, Closing };
Stage stage = Stage::Splash;
void Log(const char *text, HRESULT hr = S_OK) {
    if (profile.empty())
        return;
    std::ofstream out(profile / "launcher.log", std::ios::app);
    out << GetTickCount64() << " " << text << " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr)
        << std::dec << " main_visible=" << (window && IsWindowVisible(window)) << " alpha=" << alpha
        << " splash_visible=" << (splash.Handle() && IsWindowVisible(splash.Handle())) << '\n';
}
void Failure(const wchar_t *message, HRESULT hr) {
    Log("failure", hr);
    if (closing)
        return;
    closing = true;
    stage = Stage::Closing;
    splash.Close();
    MessageBoxW(nullptr,
                (std::wstring(message) + L"\n\nError: 0x" +
                 [&] {
                     wchar_t b[16]{};
                     swprintf_s(b, L"%08X", static_cast<unsigned>(hr));
                     return std::wstring(b);
                 }())
                    .c_str(),
                L"VNCheater", MB_OK | MB_ICONERROR);
    KillTimer(window, kRevealTick);
    KillTimer(window, kRevealWatchdog);
    transitionStarted = GetTickCount64();
    closeStartAlpha = alpha;
    if (window && IsWindowVisible(window) && alpha > 0 && SetTimer(window, kCloseTick, 16, nullptr)) {
    } else {
        if (controller)
            controller->Close();
        web.Reset();
        controller.Reset();
        if (window)
            DestroyWindow(window);
    }
}
bool Trusted(const wchar_t *uri) {
    if (!uri)
        return false;
    std::wstring s(uri);
    return s == kPage || s.rfind(std::wstring(kPage) + L"#", 0) == 0;
}
bool ExternalAllowed(const wchar_t *uri) {
    if (!uri)
        return false;
    std::wstring s(uri);
    return s == L"https://discord.gg/7ScmWMXBm" ||
           s == L"https://www.facebook.com/nguyen.viet.hung.524822/" || s == L"https://vncheater.com" ||
           s == L"https://vncheater.com/";
}
void External(const wchar_t *uri) {
    if (ExternalAllowed(uri))
        ShellExecuteW(window, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
}
void Post(const std::wstring &message) {
    if (web && !closing)
        web->PostWebMessageAsString(message.c_str());
}
void Bounds() {
    if (!controller)
        return;
    RECT r{};
    GetClientRect(window, &r);
    controller->put_Bounds(r);
    controller->NotifyParentWindowPositionChanged();
}
void PickFolder() {
    if (pickerOpen || closing)
        return;
    pickerOpen = true;
    ComPtr<IFileOpenDialog> dlg;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (SUCCEEDED(hr)) {
        DWORD flags{};
        dlg->GetOptions(&flags);
        dlg->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR |
                        FOS_DONTADDTORECENT);
        dlg->SetTitle(L"Select game directory");
        hr = dlg->Show(window);
        if (SUCCEEDED(hr)) {
            ComPtr<IShellItem> item;
            hr = dlg->GetResult(&item);
            if (SUCCEEDED(hr)) {
                PWSTR path{};
                hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
                if (SUCCEEDED(hr)) {
                    Post(std::wstring(L"folder:") + path);
                    CoTaskMemFree(path);
                }
            }
        }
    }
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        Post(L"folder-cancelled");
    else if (FAILED(hr)) {
        Post(L"folder-error");
        Log("folder picker", hr);
    }
    pickerOpen = false;
}
void TryReveal() {
    if (closing || stage != Stage::WaitingForUi || !controller || (!navReady && !replayPending) || !uiReady)
        return;
    KillTimer(window, kRevealWatchdog);
    stage = Stage::Revealing;
    alpha = 0;
    transitionStarted = GetTickCount64();
    if (!SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA)) {
        Failure(L"Could not prepare the launcher fade.", HRESULT_FROM_WIN32(GetLastError()));
        return;
    }
    Bounds();
    controller->put_IsVisible(TRUE);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    splash.Finish();
    if (!SetTimer(window, kRevealTick, 16, nullptr)) {
        Failure(L"Could not start the launcher animation.", HRESULT_FROM_WIN32(GetLastError()));
        return;
    }
    Log("reveal begin");
}
void Configure() {
    ComPtr<ICoreWebView2_3> mapping;
    HRESULT hr = web.As(&mapping);
    if (FAILED(hr)) {
        Failure(L"Please update Microsoft Edge WebView2 Runtime.", hr);
        return;
    }
    hr = mapping->SetVirtualHostNameToFolderMapping(L"launcher.vncheater.local", uiFolder.c_str(),
                                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    if (FAILED(hr)) {
        Failure(L"Could not load the local interface folder.", hr);
        return;
    }
    ComPtr<ICoreWebView2Settings> settings;
    web->get_Settings(&settings);
    settings->put_AreDefaultContextMenusEnabled(FALSE);
    settings->put_IsStatusBarEnabled(FALSE);
    settings->put_AreDevToolsEnabled(FALSE);
    settings->put_AreHostObjectsAllowed(FALSE);
    settings->put_IsZoomControlEnabled(FALSE);
    ComPtr<ICoreWebView2Settings3> s3;
    if (SUCCEEDED(settings.As(&s3)))
        s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
    ComPtr<ICoreWebView2Settings4> s4;
    if (SUCCEEDED(settings.As(&s4))) {
        s4->put_IsPasswordAutosaveEnabled(FALSE);
        s4->put_IsGeneralAutofillEnabled(FALSE);
    }
    ComPtr<ICoreWebView2Controller2> c2;
    if (SUCCEEDED(controller.As(&c2)))
        c2->put_DefaultBackgroundColor({255, 7, 24, 37});
    EventRegistrationToken tok{};
    web->add_NavigationStarting(
        Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2 *,
                                                                 ICoreWebView2NavigationStartingEventArgs *a)
                                                                  -> HRESULT {
            LPWSTR u{};
            a->get_Uri(&u);
            if (!Trusted(u)) {
                a->put_Cancel(TRUE);
                External(u);
            }
            CoTaskMemFree(u);
            return S_OK;
        }).Get(),
        &tok);
    web->add_NewWindowRequested(
        Callback<ICoreWebView2NewWindowRequestedEventHandler>([](ICoreWebView2 *,
                                                                 ICoreWebView2NewWindowRequestedEventArgs *a)
                                                                  -> HRESULT {
            a->put_Handled(TRUE);
            LPWSTR u{};
            a->get_Uri(&u);
            External(u);
            CoTaskMemFree(u);
            return S_OK;
        }).Get(),
        &tok);
    web->add_PermissionRequested(
        Callback<ICoreWebView2PermissionRequestedEventHandler>(
            [](ICoreWebView2 *, ICoreWebView2PermissionRequestedEventArgs *a) -> HRESULT {
                a->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY);
                return S_OK;
            })
            .Get(),
        &tok);
    web->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>([](ICoreWebView2 *,
                                                                 ICoreWebView2WebMessageReceivedEventArgs *a)
                                                                  -> HRESULT {
            LPWSTR src{};
            a->get_Source(&src);
            bool ok = Trusted(src);
            CoTaskMemFree(src);
            if (!ok || closing)
                return S_OK;
            LPWSTR raw{};
            if (FAILED(a->TryGetWebMessageAsString(&raw)))
                return S_OK;
            std::wstring m(raw);
            CoTaskMemFree(raw);
            if (m == L"ui-ready") {
                uiReady = true;
                Log("ui ready");
                TryReveal();
            } else if (m == L"close")
                PostMessageW(window, WM_CLOSE, 0, 0);
            else if (m == L"minimize")
                ShowWindow(window, SW_MINIMIZE);
            else if (m == L"drag")
                PostMessageW(window, kDrag, 0, 0);
            else if (m == L"pick-folder")
                PostMessageW(window, kPickFolder, 0, 0);
            else if (m == L"replay-splash") {
                replayPending = true;
                uiReady = false;
                navReady = true;
                stage = Stage::Splash;
                controller->put_IsVisible(FALSE);
                ShowWindow(window, SW_HIDE);
                if (!splash.Start(GetModuleHandleW(nullptr), window,
                                  uiFolder / L"assets" / L"images" / L"logo.png", kSplashReady))
                    PostMessageW(window, kSplashReady, 0, 0);
            }
            return S_OK;
        }).Get(),
        &tok);
    web->add_NavigationCompleted(
        Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *a) -> HRESULT {
                BOOL ok{};
                a->get_IsSuccess(&ok);
                if (!ok) {
                    Failure(L"The local launcher interface could not load.", E_FAIL);
                    return S_OK;
                }
                navReady = true;
                Log("navigation ready");
                TryReveal();
                return S_OK;
            })
            .Get(),
        &tok);
    web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
                               [](ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *) -> HRESULT {
                                   Failure(L"The interface renderer stopped. Please restart the launcher.",
                                           E_FAIL);
                                   return S_OK;
                               })
                               .Get(),
                           &tok);
    controller->put_IsVisible(FALSE);
    Bounds();
    stage = Stage::WaitingForUi;
    SetTimer(window, kRevealWatchdog, 20000, nullptr);
    hr = web->Navigate(kPage);
    if (FAILED(hr))
        Failure(L"Unable to navigate to the launcher interface.", hr);
}
void CreateWebView() {
    if (closing || controller || stage != Stage::CreatingWebView)
        return;
    SetTimer(window, kRevealWatchdog, 20000, nullptr);
    Log("webview environment begin");
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, profile.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([](HRESULT result,
                                                                                ICoreWebView2Environment *env)
                                                                                 -> HRESULT {
            if (closing)
                return S_OK;
            if (FAILED(result) || !env) {
                Failure(L"Microsoft Edge WebView2 Runtime is required.", result);
                return S_OK;
            }
            HRESULT hr = env->CreateCoreWebView2Controller(
                window, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [](HRESULT result, ICoreWebView2Controller *value) -> HRESULT {
                                if (closing) {
                                    if (value)
                                        value->Close();
                                    return S_OK;
                                }
                                if (FAILED(result) || !value) {
                                    Failure(L"Unable to create the WebView2 window.", result);
                                    return S_OK;
                                }
                                controller = value;
                                controller->get_CoreWebView2(&web);
                                Configure();
                                return S_OK;
                            })
                            .Get());
            if (FAILED(hr))
                Failure(L"Unable to initialize WebView2 controller.", hr);
            return S_OK;
        }).Get());
    if (FAILED(hr))
        Failure(L"Unable to start WebView2.", hr);
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp)
            return 0;
        break;
    case WM_NCHITTEST: {
        RECT r{};
        GetWindowRect(hwnd, &r);
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp), b = MulDiv(6, GetDpiForWindow(hwnd), 96);
        bool l = x < r.left + b, rr = x >= r.right - b, t = y < r.top + b, bt = y >= r.bottom - b;
        if (t && l)
            return HTTOPLEFT;
        if (t && rr)
            return HTTOPRIGHT;
        if (bt && l)
            return HTBOTTOMLEFT;
        if (bt && rr)
            return HTBOTTOMRIGHT;
        if (t)
            return HTTOP;
        if (bt)
            return HTBOTTOM;
        if (l)
            return HTLEFT;
        if (rr)
            return HTRIGHT;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto i = reinterpret_cast<MINMAXINFO *>(lp);
        UINT d = GetDpiForWindow(hwnd);
        i->ptMinTrackSize = {MulDiv(960, d, 96), MulDiv(600, d, 96)};
        return 0;
    }
    case WM_SIZE:
        if (controller && (stage == Stage::Revealing || stage == Stage::Running)) {
            controller->put_IsVisible(wp != SIZE_MINIMIZED);
            if (wp != SIZE_MINIMIZED)
                Bounds();
            Post(wp == SIZE_MINIMIZED ? L"paused" : L"resumed");
        }
        return 0;
    case WM_TIMER:
        if (wp == kRevealTick && stage == Stage::Revealing) {
            const double t = std::min(1., static_cast<double>(GetTickCount64() - transitionStarted) / 620.);
            alpha = static_cast<int>(255. * t * t * (3. - 2. * t));
            SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(alpha), LWA_ALPHA);
            if (t >= 1.) {
                KillTimer(hwnd, kRevealTick);
                stage = Stage::Running;
                replayPending = false;
                Log("reveal complete");
                Post(L"begin-loading");
                SetForegroundWindow(hwnd);
            }
            return 0;
        }
        if (wp == kRevealWatchdog) {
            KillTimer(hwnd, kRevealWatchdog);
            if (stage == Stage::WaitingForUi || stage == Stage::CreatingWebView)
                Failure(L"The launcher interface did not become ready in time.",
                        HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            return 0;
        }
        if (wp == kCloseTick) {
            const double t = std::min(1., static_cast<double>(GetTickCount64() - transitionStarted) / 280.);
            alpha = static_cast<int>(closeStartAlpha * (1. - t * t * (3. - 2. * t)));
            SetLayeredWindowAttributes(hwnd, 0, static_cast<BYTE>(alpha), LWA_ALPHA);
            if (t >= 1.) {
                KillTimer(hwnd, kCloseTick);
                if (controller)
                    controller->Close();
                web.Reset();
                controller.Reset();
                DestroyWindow(hwnd);
            }
            return 0;
        }
        break;
    case kSplashReady:
        if (closing)
            return 0;
        Log("splash ready");
        if (!controller) {
            stage = Stage::CreatingWebView;
            CreateWebView();
        } else {
            stage = Stage::WaitingForUi;
            uiReady = true;
            TryReveal();
        }
        return 0;
    case WM_MOVE:
        if (controller)
            controller->NotifyParentWindowPositionChanged();
        return 0;
    case WM_DPICHANGED: {
        auto r = reinterpret_cast<RECT *>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Bounds();
        return 0;
    }
    case WM_SETFOCUS:
        if (controller)
            controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
    case kPickFolder:
        PickFolder();
        return 0;
    case kDrag:
        ReleaseCapture();
        SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    case WM_CLOSE:
        if (pickerOpen || stage == Stage::Closing)
            return 0;
        closing = true;
        stage = Stage::Closing;
        Log("close requested");
        KillTimer(hwnd, kRevealTick);
        KillTimer(hwnd, kRevealWatchdog);
        splash.Close();
        if (!IsWindowVisible(hwnd) || alpha <= 0) {
            if (controller)
                controller->Close();
            web.Reset();
            controller.Reset();
            DestroyWindow(hwnd);
            return 0;
        }
        transitionStarted = GetTickCount64();
        closeStartAlpha = alpha;
        if (!SetTimer(hwnd, kCloseTick, 16, nullptr)) {
            if (controller)
                controller->Close();
            web.Reset();
            controller.Reset();
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        Log("clean shutdown");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;
    wchar_t exe[32768]{};
    GetModuleFileNameW(nullptr, exe, 32768);
    uiFolder = fs::path(exe).parent_path() / L"ui";
    if (!fs::exists(uiFolder / L"index.html")) {
        MessageBoxW(nullptr, L"The ui folder is missing.", L"VNCheater", MB_ICONERROR);
        CoUninitialize();
        return 2;
    }
    PWSTR local{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        CoUninitialize();
        return 3;
    }
    profile = fs::path(local) / L"VNCheater" / L"WebView2";
    CoTaskMemFree(local);
    std::error_code ec;
    fs::create_directories(profile, ec);
    if (ec) {
        CoUninitialize();
        return 4;
    }
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"VNCheaterSummerWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(7, 24, 37));
    RegisterClassExW(&wc);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    UINT d = GetDpiForSystem();
    int width = std::min<int>(MulDiv(1336, d, 96), work.right - work.left - 24),
        height = std::min<int>(MulDiv(768, d, 96), work.bottom - work.top - 24);
    window = CreateWindowExW(
        WS_EX_APPWINDOW | WS_EX_LAYERED, wc.lpszClassName, L"VNCheater - Strinova Launcher",
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX,
        work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2,
        width, height, nullptr, nullptr, instance, nullptr);
    if (!window) {
        CoUninitialize();
        return 5;
    }
    SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
    ShowWindow(window, SW_HIDE);
    stage = Stage::Splash;
    if (!splash.Start(instance, window, uiFolder / L"assets" / L"images" / L"logo.png", kSplashReady)) {
        Log("splash unavailable; using launcher fallback");
        PostMessageW(window, kSplashReady, 0, 0);
    } else
        Log("startup native splash visible");
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DeleteObject(wc.hbrBackground);
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
