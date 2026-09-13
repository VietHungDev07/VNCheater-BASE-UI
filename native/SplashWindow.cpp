#include "SplashWindow.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>

using namespace Gdiplus;
namespace {
constexpr wchar_t kClass[] = L"VNCheaterDesktopSplash";
constexpr UINT_PTR kFrameTimer = 1;
constexpr float kCanvas = 640.f;
constexpr float kPi = 3.14159265f;
constexpr double kIntroSeconds = 3.8;
float Clamp(float n) { return std::clamp(n, 0.f, 1.f); }
float Smooth(float n) {
    n = Clamp(n);
    return n * n * (3.f - 2.f * n);
}
Color Tint(float alpha, BYTE r, BYTE g, BYTE b) {
    return Color(static_cast<BYTE>(Clamp(alpha) * 255.f), r, g, b);
}
void Glow(Graphics &g, float x, float y, float rx, float ry, Color center) {
    GraphicsPath path;
    path.AddEllipse(x - rx, y - ry, rx * 2.f, ry * 2.f);
    PathGradientBrush brush(&path);
    brush.SetCenterPoint(PointF(x, y));
    brush.SetCenterColor(center);
    Color edge(0, center.GetR(), center.GetG(), center.GetB());
    INT count = 1;
    brush.SetSurroundColors(&edge, &count);
    // Smooth radial falloff; the last pixels meet the fully transparent desktop.
    const REAL factors[]{0.f, .015f, .14f, .52f, 1.f};
    const REAL positions[]{0.f, .25f, .55f, .82f, 1.f};
    brush.SetBlend(factors, positions, 5);
    g.FillPath(&brush, &path);
}
void CenteredText(GraphicsPath &path, const wchar_t *text, float size, float y, INT style) {
    FontFamily font(L"Segoe UI");
    path.AddString(text, -1, &font, style, size, PointF(0, 0), StringFormat::GenericTypographic());
    RectF bounds{};
    path.GetBounds(&bounds);
    Matrix move;
    move.Translate((kCanvas - bounds.Width) / 2.f - bounds.X, y - bounds.Y);
    path.Transform(&move);
}
} // namespace

struct SplashWindow::Impl {
    ULONG_PTR gdiplus{};
    HWND hwnd{}, notify{};
    UINT readyMessage{};
    HDC dc{};
    HBITMAP dib{};
    HGDIOBJ oldBitmap{};
    BYTE *pixels{};
    int size{};
    POINT location{};
    std::unique_ptr<Bitmap> surface, logo;
    std::unique_ptr<Graphics> graphics;
    GraphicsPath title, slogan;
    ULONGLONG started{}, finishAt{};
    bool notified{}, reduced{};

    ~Impl() {
        Close();
        graphics.reset();
        surface.reset();
        logo.reset();
        // GraphicsPath members must be destroyed before GdiplusShutdown as well.
        // The enclosing SplashWindow owns startup/shutdown (see Destroy below).
        if (oldBitmap && dc)
            SelectObject(dc, oldBitmap);
        if (dib)
            DeleteObject(dib);
        if (dc)
            DeleteDC(dc);
    }

    bool Initialize(const std::filesystem::path &path, int dimension) {
        size = dimension;
        Bitmap input(path.c_str());
        if (input.GetLastStatus() != Ok || input.GetWidth() < 32 || input.GetHeight() < 32)
            return false;
        const int w = static_cast<int>(input.GetWidth()), h = static_cast<int>(input.GetHeight());
        // The supplied PNG includes a small duplicate signature below the main crest.
        // Crop only the in-memory texture; the user's original file is preserved.
        Rect crop(static_cast<int>(w * .12), 0, static_cast<int>(w * .76), static_cast<int>(h * .888));
        logo.reset(input.Clone(crop, PixelFormat32bppARGB));
        if (!logo || logo->GetLastStatus() != Ok)
            return false;
        BitmapData data{};
        Rect all(0, 0, crop.Width, crop.Height);
        if (logo->LockBits(&all, ImageLockModeRead | ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok)
            return false;
        for (int y = 0; y < crop.Height; ++y) {
            auto row = static_cast<BYTE *>(data.Scan0) + y * data.Stride;
            for (int x = 0; x < crop.Width; ++x) {
                BYTE *p = row + x * 4;
                const float light = static_cast<float>(std::max({p[0], p[1], p[2]}));
                const float matte = Smooth((light - 55.f) / 65.f);
                const int borderDistance = std::min({x, y, crop.Width - 1 - x, crop.Height - 1 - y});
                const float edge = Smooth(static_cast<float>(borderDistance) / 18.f);
                p[3] = static_cast<BYTE>(p[3] * matte * edge);
            }
        }
        logo->UnlockBits(&data);

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = size;
        info.bmiHeader.biHeight = -size;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        if (!dc)
            return false;
        dib = CreateDIBSection(dc, &info, DIB_RGB_COLORS, reinterpret_cast<void **>(&pixels), nullptr, 0);
        if (!dib || !pixels)
            return false;
        oldBitmap = SelectObject(dc, dib);
        surface = std::make_unique<Bitmap>(size, size, size * 4, PixelFormat32bppPARGB, pixels);
        if (surface->GetLastStatus() != Ok)
            return false;
        graphics = std::make_unique<Graphics>(surface.get());
        graphics->SetSmoothingMode(SmoothingModeAntiAlias);
        graphics->SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics->SetPixelOffsetMode(PixelOffsetModeHighQuality);
        CenteredText(title, L"VNCheater", 49.f, 435.f, FontStyleBold);
        CenteredText(slogan, L"R E  &  C H E A T  D E V", 11.f, 499.f, FontStyleRegular);
        return graphics->GetLastStatus() == Ok;
    }

    void Draw(float seconds, float opacity = 1.f) {
        Graphics &g = *graphics;
        g.ResetTransform();
        g.ResetClip();
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.Clear(Color(0, 0, 0, 0));
        g.SetCompositingMode(CompositingModeSourceOver);
        const float dpi = static_cast<float>(size) / kCanvas;
        g.ScaleTransform(dpi, dpi);
        const float t = reduced ? 2.8f : seconds;
        const float entrance = Smooth(t / .75f) * opacity;
        const float reveal = Smooth((t - .24f) / 1.45f);
        const float titleAlpha = Smooth((t - 1.12f) / .8f) * entrance;
        const float pulse = reduced ? 1.f : .87f + .13f * std::sin(t * 1.7f);

        // A breathing, irregular mist occupies only the logo footprint, never a panel.
        Glow(g, 320, 265, 182, 205, Tint(.11f * entrance * pulse, 29, 166, 209));
        for (int i = 0; i < 10; ++i) {
            const float a = i * .628f + t * .13f;
            const float r = 95.f + 18.f * std::sin(t * .6f + i * 2.f);
            Glow(g, 320 + std::cos(a) * r, 267 + std::sin(a) * r * 1.1f, 62, 76,
                 i % 2 ? Tint(.07f * entrance, 151, 89, 236) : Tint(.09f * entrance, 38, 207, 242));
        }

        // Two broken energy strokes draw on, drift and then settle behind the crest.
        for (int i = 0; i < 2; ++i) {
            const float start = (i ? 35.f : 205.f) + (reduced ? 0 : t * (i ? -12.f : 17.f));
            const float sweep = (i ? 68.f : 87.f) * Smooth(t / 1.2f);
            const RectF ring(143.f + i * 11.f, 82.f + i * 11.f, 354.f - i * 22.f, 354.f - i * 22.f);
            Pen bloom(Tint(.065f * entrance, i ? 174 : 63, i ? 118 : 221, 255), 7.f);
            Pen line(Tint(.30f * entrance, i ? 190 : 127, i ? 153 : 240, 255), .9f);
            g.DrawArc(&bloom, ring, start, sweep);
            g.DrawArc(&line, ring, start, sweep);
        }

        // Deterministic motes converge on reveal; later they drift out like embers.
        for (int i = 0; i < 48; ++i) {
            const float seed = static_cast<float>(i);
            const float a = seed * 2.39996f + t * (.07f + (i % 4) * .008f);
            const float cycle = std::fmod(t * .17f + seed * .618f, 1.f);
            const float r = 120.f + cycle * 92.f + 42.f * (1.f - reveal);
            const float x = 320.f + std::cos(a) * r * .89f, y = 268.f + std::sin(a) * r * .89f;
            const float pa = std::sin(cycle * kPi) * (.27f + .3f * (i % 3) / 2.f) * entrance;
            const float dot = (i % 7 == 0) ? 1.7f : .85f;
            const Color color = i % 3 == 0 ? Tint(pa, 182, 146, 255) : Tint(pa, 145, 239, 255);
            if (i % 7 == 0)
                Glow(g, x, y, 7, 7, Tint(pa * .25f, color.GetR(), color.GetG(), color.GetB()));
            SolidBrush brush(color);
            g.FillEllipse(&brush, x, y, dot * 2, dot * 2);
        }

        // Scale/float is applied to the crest alone; typography remains anchored.
        const float scale = .88f + .12f * Smooth(t / 1.5f);
        const float width = 343.f * scale;
        const float height = width * static_cast<float>(logo->GetHeight()) / logo->GetWidth();
        const float y = 88.f + (1.f - reveal) * 17.f + (reduced ? 0.f : std::sin(t * 1.1f) * 2.f);
        const RectF target(320.f - width / 2.f, y, width, height);
        ColorMatrix matrix = {{{1, 0, 0, 0, 0},
                               {0, 1, 0, 0, 0},
                               {0, 0, 1, 0, 0},
                               {0, 0, 0, entrance * reveal, 0},
                               {0, 0, 0, 0, 1}}};
        ImageAttributes attrs;
        attrs.SetColorMatrix(&matrix);
        g.DrawImage(logo.get(), target, 0, 0, static_cast<REAL>(logo->GetWidth()),
                    static_cast<REAL>(logo->GetHeight()), UnitPixel, &attrs);

        // A narrow light pass follows the reveal, contained within the crest alpha.
        const float pass = Clamp((t - .55f) / 1.65f);
        if (pass > 0 && pass < 1) {
            const GraphicsState state = g.Save();
            g.SetClip(RectF(target.X, target.Y + pass * target.Height - 14.f, width, 28.f));
            matrix.m[0][0] = 1.7f;
            matrix.m[1][1] = 1.55f;
            matrix.m[2][2] = 1.3f;
            matrix.m[3][3] = .46f * entrance * std::sin(pass * kPi);
            attrs.SetColorMatrix(&matrix);
            g.DrawImage(logo.get(), target, 0, 0, static_cast<REAL>(logo->GetWidth()),
                        static_cast<REAL>(logo->GetHeight()), UnitPixel, &attrs);
            g.Restore(state);
        }

        Pen textShadow(Tint(.19f * titleAlpha, 2, 14, 34), 5.f);
        textShadow.SetLineJoin(LineJoinRound);
        g.DrawPath(&textShadow, &title);
        Pen textGlow(Tint(.09f * titleAlpha, 72, 219, 255), 2.f);
        g.DrawPath(&textGlow, &title);
        LinearGradientBrush ink(PointF(0, 435), PointF(0, 483), Tint(titleAlpha, 249, 254, 255),
                                Tint(titleAlpha, 139, 214, 247));
        g.FillPath(&ink, &title);
        SolidBrush sloganInk(Tint(titleAlpha * .9f, 183, 216, 236));
        g.FillPath(&sloganInk, &slogan);
        const float rule = Smooth((t - 1.65f) / .8f);
        Pen divider(Tint(.45f * titleAlpha, 99, 210, 240), .8f);
        g.DrawLine(&divider, 320.f - 75.f * rule, 489.f, 320.f + 75.f * rule, 489.f);
        Glow(g, 320, 489, 53, 5, Tint(.2f * titleAlpha, 87, 224, 255));

        // Tiny heartbeat remains alive while the hidden WebView initializes.
        if (t > 2.3f)
            for (int i = 0; i < 3; ++i) {
                const float a = (.22f + .28f * (.5f + .5f * std::sin(t * 3.f - i * .9f))) * titleAlpha;
                SolidBrush dot(Tint(a, 169, 225, 253));
                g.FillEllipse(&dot, 309.f + i * 9.f, 534.f, 2.5f, 2.5f);
            }
        g.Flush(FlushIntentionSync);
    }

    bool Present() {
        SIZE dimensions{size, size};
        POINT origin{};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        // surface uses PARGB, so every RGB channel is already multiplied by alpha.
        return UpdateLayeredWindow(hwnd, nullptr, &location, &dimensions, dc, &origin, 0, &blend,
                                   ULW_ALPHA) != FALSE;
    }
    void Close() {
        if (hwnd) {
            HWND old = hwnd;
            hwnd = nullptr;
            KillTimer(old, kFrameTimer);
            DestroyWindow(old);
        }
    }
    void Tick() {
        const ULONGLONG now = GetTickCount64();
        const float t = static_cast<float>(now - started) / 1000.f;
        float alpha = 1.f;
        if (finishAt) {
            alpha = 1.f - Smooth(static_cast<float>(now - finishAt) / (reduced ? 120.f : 460.f));
            if (alpha <= 0) {
                Close();
                return;
            }
        }
        Draw(t, alpha);
        const bool presented = Present();
        if (!notified && (t >= (reduced ? 1.2 : kIntroSeconds) || !presented)) {
            notified = true;
            PostMessageW(notify, readyMessage, presented ? 0 : 1, 0);
        }
        if (!presented)
            Close();
    }
    static LRESULT CALLBACK Proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
        auto self = reinterpret_cast<Impl *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            self = static_cast<Impl *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        switch (msg) {
        case WM_TIMER:
            if (self && wp == kFrameTimer)
                self->Tick();
            return 0;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            if (self)
                self->Close();
            return 0;
        case WM_NCDESTROY:
            if (self)
                self->hwnd = nullptr;
            break;
        }
        return DefWindowProcW(window, msg, wp, lp);
    }
};

SplashWindow::SplashWindow() = default;
SplashWindow::~SplashWindow() { Close(); }
void SplashWindow::Close() {
    if (!impl_)
        return;
    const ULONG_PTR token = impl_->gdiplus;
    impl_.reset(); // All GDI+ objects, including paths, must die before shutdown.
    GdiplusShutdown(token);
}
HWND SplashWindow::Handle() const { return impl_ ? impl_->hwnd : nullptr; }
bool SplashWindow::Start(HINSTANCE instance, HWND notifyWindow, const std::filesystem::path &logoPath,
                         UINT readyMessage) {
    Close();
    GdiplusStartupInput input;
    ULONG_PTR token{};
    if (GdiplusStartup(&token, &input, nullptr) != Ok)
        return false;
    impl_ = std::make_unique<Impl>();
    impl_->gdiplus = token;
    impl_->notify = notifyWindow;
    impl_->readyMessage = readyMessage;
    BOOL animation = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animation, 0);
    impl_->reduced = !animation;
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(notifyWindow, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT &work = monitor.rcWork;
    const UINT dpi = GetDpiForWindow(notifyWindow);
    const int size = std::max(
        240, std::min<int>({MulDiv(640, static_cast<int>(dpi), 96), static_cast<int>(work.right - work.left),
                            static_cast<int>(work.bottom - work.top)}));
    impl_->location = {(work.left + work.right - size) / 2, (work.top + work.bottom - size) / 2};
    if (!impl_->Initialize(logoPath, size)) {
        Close();
        return false;
    }
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = instance;
    wc.lpfnWndProc = Impl::Proc;
    wc.lpszClassName = kClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Close();
        return false;
    }
    impl_->hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                                      WS_EX_TRANSPARENT,
                                  kClass, L"VNCheater Desktop Splash", WS_POPUP, impl_->location.x,
                                  impl_->location.y, size, size, nullptr, nullptr, instance, impl_.get());
    if (!impl_->hwnd) {
        Close();
        return false;
    }
    impl_->started = GetTickCount64();
    impl_->Draw(0);
    if (!impl_->Present() || !SetTimer(impl_->hwnd, kFrameTimer, 16, nullptr)) {
        Close();
        return false;
    }
    ShowWindow(impl_->hwnd, SW_SHOWNOACTIVATE);
    return true;
}
void SplashWindow::Finish() {
    if (impl_ && impl_->hwnd && !impl_->finishAt)
        impl_->finishAt = GetTickCount64();
}

bool SplashWindow::ExportFrames(const std::filesystem::path &path, const std::filesystem::path &directory) {
    GdiplusStartupInput input;
    ULONG_PTR token{};
    if (GdiplusStartup(&token, &input, nullptr) != Ok)
        return false;
    bool ok = false;
    {
        Impl renderer;
        if (renderer.Initialize(path, 640)) {
            std::error_code ec;
            std::filesystem::create_directories(directory, ec);
            UINT count{}, bytes{};
            GetImageEncodersSize(&count, &bytes);
            std::vector<BYTE> encoders(bytes);
            auto enc = reinterpret_cast<ImageCodecInfo *>(encoders.data());
            CLSID png{};
            bool found = false;
            if (bytes && GetImageEncoders(count, bytes, enc) == Ok)
                for (UINT i = 0; i < count; ++i) {
                    if (wcscmp(enc[i].MimeType, L"image/png") == 0) {
                        png = enc[i].Clsid;
                        found = true;
                        break;
                    }
                }
            ok = found && !ec;
            std::ofstream report(directory / L"alpha-check.txt");
            for (const float t : {.0f, .55f, 1.35f, 2.8f, 4.4f}) {
                renderer.Draw(t);
                size_t transparent = 0, invalid = 0, border = 0;
                for (int y = 0; y < 640; ++y)
                    for (int x = 0; x < 640; ++x) {
                        const auto p = renderer.pixels + (y * 640 + x) * 4;
                        if (!p[3])
                            ++transparent;
                        if (p[0] > p[3] || p[1] > p[3] || p[2] > p[3])
                            ++invalid;
                        if ((x == 0 || x == 639 || y == 0 || y == 639) && p[3])
                            ++border;
                    }
                report << "t=" << t << " transparent=" << transparent
                       << "/409600 invalid_premultiplied=" << invalid << " opaque_border=" << border << '\n';
                ok = ok && invalid == 0 && border == 0;
                const std::wstring stem = L"splash-" + std::to_wstring(static_cast<int>(t * 1000));
                if (found) {
                    ok = (renderer.surface->Save((directory / (stem + L".png")).c_str(), &png, nullptr) ==
                          Ok) &&
                         ok;
                    // Composite the same production frame over a checkerboard for visual QA.
                    Bitmap preview(640, 640, PixelFormat32bppARGB);
                    Graphics g(&preview);
                    for (int y = 0; y < 640; y += 40)
                        for (int x = 0; x < 640; x += 40) {
                            SolidBrush tile((x / 40 + y / 40) % 2 ? Color(255, 54, 63, 78)
                                                                  : Color(255, 68, 79, 94));
                            g.FillRectangle(&tile, x, y, 40, 40);
                        }
                    g.DrawImage(renderer.surface.get(), 0, 0);
                    ok =
                        (preview.Save((directory / (stem + L"-checker.png")).c_str(), &png, nullptr) == Ok) &&
                        ok;
                }
            }
        }
    }
    GdiplusShutdown(token);
    return ok;
}
