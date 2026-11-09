// Implémentation Win32 + WGL de la couche plateforme.
#include "window.h"

#include <windows.h>
#include <windowsx.h>

#include <cstdio>

#include "gl_api.h"

#include <cstdint>
#include <cstring>

// L'icône de la fenêtre et de la barre des tâches : l'emblème de la bannière de la cartouche (son adresse en 0x68
// de l'en-tête ; 32×32 points de 4 bits en tuiles de 8×8, puis seize couleurs RGB555, la première transparente).
// Le portage ne livre aucune image du jeu : celle-ci est relue dans la cartouche du joueur à chaque lancement,
// comme le fait le lanceur (launcher/images.c). Sans bannière lisible, la fenêtre garde l'icône de Windows.
static uint32_t g_icone[32 * 32];
static bool g_a_icone;

extern "C" void fenetre_icone(const uint8_t *rom, size_t taille) {
    if (!rom || taille < 0x200) return;
    uint32_t b;
    std::memcpy(&b, rom + 0x68, 4);
    if (!b || b > taille || taille - b < 0x240) return;
    const uint8_t *banniere = rom + b;
    uint32_t palette[16];
    for (int i = 0; i < 16; i++) {
        const unsigned c = banniere[0x220 + 2 * i] | banniere[0x221 + 2 * i] << 8;
        const uint32_t r = (c & 31) * 255 / 31, v = (c >> 5 & 31) * 255 / 31, bl = (c >> 10 & 31) * 255 / 31;
        palette[i] = i ? 0xFF000000u | r << 16 | v << 8 | bl : 0;
    }
    for (int tuile = 0; tuile < 16; tuile++)
        for (int k = 0; k < 64; k++) {
            const int x = (tuile % 4) * 8 + k % 8, y = (tuile / 4) * 8 + k / 8;
            g_icone[y * 32 + x] = palette[banniere[0x20 + tuile * 32 + k / 2] >> (4 * (k & 1)) & 15];
        }
    g_a_icone = true;
}

static HICON creer_icone() {
    if (!g_a_icone) return nullptr;
    BITMAPV5HEADER bh{};
    bh.bV5Size = sizeof bh;
    bh.bV5Width = 32;
    bh.bV5Height = -32;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000;
    bh.bV5GreenMask = 0x0000FF00;
    bh.bV5BlueMask = 0x000000FF;
    bh.bV5AlphaMask = 0xFF000000;
    void *bits = nullptr;
    HDC dc = GetDC(nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&bh), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!ii.hbmColor || !bits) {
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        return nullptr;
    }
    std::memcpy(bits, g_icone, sizeof g_icone);
    static const uint8_t masque[32 * 4] = {};
    ii.hbmMask = CreateBitmap(32, 32, 1, 1, masque);
    HICON icone = CreateIconIndirect(&ii);
    DeleteObject(ii.hbmMask);
    DeleteObject(ii.hbmColor);
    return icone;
}

namespace sfc::platform {
namespace {

// Jetons WGL nécessaires à la création d'un contexte 3.3 core, absents des
// en-têtes de base.
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;

constexpr int WGL_DRAW_TO_WINDOW_ARB = 0x2001;
constexpr int WGL_SUPPORT_OPENGL_ARB = 0x2010;
constexpr int WGL_DOUBLE_BUFFER_ARB = 0x2011;
constexpr int WGL_PIXEL_TYPE_ARB = 0x2013;
constexpr int WGL_COLOR_BITS_ARB = 0x2014;
constexpr int WGL_DEPTH_BITS_ARB = 0x2022;
constexpr int WGL_STENCIL_BITS_ARB = 0x2023;
constexpr int WGL_TYPE_RGBA_ARB = 0x202B;
constexpr int WGL_SAMPLE_BUFFERS_ARB = 0x2041;
constexpr int WGL_SAMPLES_ARB = 0x2042;

using PFNWGLCREATECONTEXTATTRIBSARB =
    HGLRC(WINAPI*)(HDC, HGLRC, const int*);
using PFNWGLCHOOSEPIXELFORMATARB =
    BOOL(WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
using PFNWGLSWAPINTERVALEXT = BOOL(WINAPI*)(int);

PFNWGLCREATECONTEXTATTRIBSARB wglCreateContextAttribsARB = nullptr;
PFNWGLCHOOSEPIXELFORMATARB wglChoosePixelFormatARB = nullptr;
PFNWGLSWAPINTERVALEXT wglSwapIntervalEXT = nullptr;

// wglGetProcAddress renvoie un PROC générique ; le passage par void* évite
// l'avertissement de conversion entre types de fonctions incompatibles.
template <typename Fn>
Fn resolveWgl(const char* name) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(wglGetProcAddress(name)));
}

Key keyFromVirtual(WPARAM vk) {
    switch (vk) {
        case VK_ESCAPE:  return Key::Escape;
        case VK_SPACE:   return Key::Space;
        case VK_TAB:     return Key::Tab;
        case VK_SHIFT:   return Key::Shift;
        case VK_CONTROL: return Key::Ctrl;
        case 'Z': return Key::Z;
        case VK_LEFT:   return Key::Left;
        case VK_RIGHT:  return Key::Right;
        case VK_UP:     return Key::Up;
        case VK_DOWN:   return Key::Down;
        case VK_PRIOR:  return Key::PageUp;
        case VK_NEXT:   return Key::PageDown;
        case 'W': return Key::W;
        case 'A': return Key::A;
        case 'S': return Key::S;
        case 'D': return Key::D;
        case 'Q': return Key::Q;
        case 'E': return Key::E;
        case 'R': return Key::R;
        case 'F': return Key::F;
        case 'V': return Key::V;
        case 'P': return Key::P;
        default: return Key::Count;
    }
}

// Un premier contexte « factice » est indispensable : les fonctions WGL
// avancées ne peuvent être résolues qu'avec un contexte déjà courant.
bool bootstrapWglExtensions(HINSTANCE instance) {
    if (wglCreateContextAttribsARB != nullptr) {
        return true;
    }

    WNDCLASSA dummyClass{};
    dummyClass.style = CS_OWNDC;
    dummyClass.lpfnWndProc = DefWindowProcA;
    dummyClass.hInstance = instance;
    dummyClass.lpszClassName = "SfcBootstrapGL";
    if (RegisterClassA(&dummyClass) == 0) {
        return false;
    }

    HWND dummy = CreateWindowExA(0, dummyClass.lpszClassName, "", 0, 0, 0, 1, 1,
                                 nullptr, nullptr, instance, nullptr);
    if (dummy == nullptr) {
        return false;
    }
    HDC dc = GetDC(dummy);

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;

    const int format = ChoosePixelFormat(dc, &pfd);
    SetPixelFormat(dc, format, &pfd);

    HGLRC context = wglCreateContext(dc);
    wglMakeCurrent(dc, context);

    wglCreateContextAttribsARB =
        resolveWgl<PFNWGLCREATECONTEXTATTRIBSARB>("wglCreateContextAttribsARB");
    wglChoosePixelFormatARB =
        resolveWgl<PFNWGLCHOOSEPIXELFORMATARB>("wglChoosePixelFormatARB");
    wglSwapIntervalEXT = resolveWgl<PFNWGLSWAPINTERVALEXT>("wglSwapIntervalEXT");

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(context);
    ReleaseDC(dummy, dc);
    DestroyWindow(dummy);
    UnregisterClassA(dummyClass.lpszClassName, instance);

    return wglCreateContextAttribsARB != nullptr;
}

}  // namespace

struct Window::Impl {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
    bool shouldClose = false;
    InputState* input = nullptr;
    int* width = nullptr;
    int* height = nullptr;
    float lastMouseX = 0.0f;
    float lastMouseY = 0.0f;
    bool hasMouse = false;
    int aspectW = 0;        /* proportions à tenir au redimensionnement (0 : libre) */
    int aspectH = 0;
};

namespace {

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* impl = reinterpret_cast<Window::Impl*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (impl == nullptr) {
        return DefWindowProcA(hwnd, message, wparam, lparam);
    }

    switch (message) {
        case WM_CLOSE:
        case WM_DESTROY:
            impl->shouldClose = true;
            return 0;

        case WM_SIZE:
            *impl->width = LOWORD(lparam);
            *impl->height = HIWORD(lparam);
            return 0;

        case WM_SIZING: {
            /* Les deux écrans de la console ont des proportions fixes : on tient celles de la
               zone client pendant le redimensionnement, pour que l'image s'agrandisse sans
               jamais se déformer ni laisser de bandes. Le bord tiré décide de la dimension qui
               commande : un bord vertical fixe la largeur et la hauteur suit, un bord horizontal
               l'inverse, un coin laisse la largeur commander. */
            if (impl->aspectW <= 0 || impl->aspectH <= 0) return TRUE;
            RECT* r = reinterpret_cast<RECT*>(lparam);
            RECT client{}, frame{};
            GetClientRect(hwnd, &client);
            GetWindowRect(hwnd, &frame);
            const int chromeW = (frame.right - frame.left) - (client.right - client.left);
            const int chromeH = (frame.bottom - frame.top) - (client.bottom - client.top);
            int w = (r->right - r->left) - chromeW;
            int h = (r->bottom - r->top) - chromeH;
            if (w < 1) w = 1;
            if (h < 1) h = 1;
            const bool horizontal = (wparam == WMSZ_TOP || wparam == WMSZ_BOTTOM);
            if (horizontal) {
                w = h * impl->aspectW / impl->aspectH;
            } else {
                h = w * impl->aspectH / impl->aspectW;
            }
            switch (wparam) {
                case WMSZ_LEFT:
                case WMSZ_TOPLEFT:
                case WMSZ_BOTTOMLEFT:
                    r->left = r->right - (w + chromeW);
                    break;
                default:
                    r->right = r->left + (w + chromeW);
                    break;
            }
            switch (wparam) {
                case WMSZ_TOP:
                case WMSZ_TOPLEFT:
                case WMSZ_TOPRIGHT:
                    r->top = r->bottom - (h + chromeH);
                    break;
                default:
                    r->bottom = r->top + (h + chromeH);
                    break;
            }
            return TRUE;
        }

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (wparam < 256) {
                if ((lparam & (1 << 30)) == 0) {
                    impl->input->nativePressed[wparam] = true;
                }
                impl->input->nativeDown[wparam] = true;
            }
            const Key key = keyFromVirtual(wparam);
            if (key != Key::Count) {
                const auto index = static_cast<std::size_t>(key);
                // Le bit 30 de lParam distingue une répétition d'un vrai appui.
                if ((lparam & (1 << 30)) == 0) {
                    impl->input->pressed[index] = true;
                }
                impl->input->down[index] = true;
            }
            return 0;
        }

        case WM_KEYUP:
        case WM_SYSKEYUP: {
            if (wparam < 256) {
                impl->input->nativeDown[wparam] = false;
            }
            const Key key = keyFromVirtual(wparam);
            if (key != Key::Count) {
                impl->input->down[static_cast<std::size_t>(key)] = false;
            }
            return 0;
        }

        // Le relâchement d'une touche tenue au moment où la fenêtre perd le clavier (Alt+Tab) part
        // ailleurs : sans cela, la commande resterait tenue jusqu'au prochain appui.
        case WM_KILLFOCUS:
            impl->input->down.fill(false);
            impl->input->nativeDown.fill(false);
            return 0;

        case WM_MOUSEMOVE: {
            const float x = static_cast<float>(GET_X_LPARAM(lparam));
            const float y = static_cast<float>(GET_Y_LPARAM(lparam));
            if (impl->hasMouse) {
                impl->input->mouseDeltaX += x - impl->lastMouseX;
                impl->input->mouseDeltaY += y - impl->lastMouseY;
            }
            impl->lastMouseX = x;
            impl->lastMouseY = y;
            impl->hasMouse = true;
            impl->input->mouseX = x;
            impl->input->mouseY = y;
            return 0;
        }

        case WM_LBUTTONDOWN: impl->input->mouseLeft = true;  SetCapture(hwnd); return 0;
        case WM_LBUTTONUP:   impl->input->mouseLeft = false; ReleaseCapture(); return 0;
        case WM_RBUTTONDOWN: impl->input->mouseRight = true; SetCapture(hwnd); return 0;
        case WM_RBUTTONUP:   impl->input->mouseRight = false; ReleaseCapture(); return 0;

        case WM_MOUSEWHEEL:
            impl->input->wheelDelta +=
                static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
            return 0;

        default:
            return DefWindowProcA(hwnd, message, wparam, lparam);
    }
}

}  // namespace

Window::Window() : impl_(new Impl()) {}

Window::~Window() {
    destroy();
    delete impl_;
}

bool Window::create(const std::string& title, int width, int height) {
    impl_->instance = GetModuleHandleA(nullptr);
    impl_->input = &input_;
    impl_->width = &width_;
    impl_->height = &height_;
    /* La taille demandée à la création donne les proportions à tenir ensuite. */
    impl_->aspectW = width;
    impl_->aspectH = height;
    width_ = width;
    height_ = height;

    if (!bootstrapWglExtensions(impl_->instance)) {
        std::fprintf(stderr, "Impossible de résoudre les extensions WGL.\n");
        return false;
    }

    WNDCLASSA windowClass{};
    windowClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = impl_->instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = "SfcNativeWindow";
    RegisterClassA(&windowClass);

    RECT rect{0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    impl_->hwnd = CreateWindowExA(
        0, windowClass.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, impl_->instance, nullptr);
    if (impl_->hwnd == nullptr) {
        std::fprintf(stderr, "Création de la fenêtre impossible.\n");
        return false;
    }

    SetWindowLongPtrA(impl_->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl_));
    impl_->dc = GetDC(impl_->hwnd);
    if (HICON icone = creer_icone()) {         // l'emblème de la cartouche (fenetre_icone), gardé jusqu'à la fin
        SendMessageA(impl_->hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icone));
        SendMessageA(impl_->hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icone));
    }

    const int pixelAttribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
        WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
        WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
        WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB,     32,
        WGL_DEPTH_BITS_ARB,     24,
        WGL_STENCIL_BITS_ARB,   8,
        WGL_SAMPLE_BUFFERS_ARB, GL_TRUE,
        WGL_SAMPLES_ARB,        4,
        0,
    };

    int format = 0;
    UINT formatCount = 0;
    if (wglChoosePixelFormatARB == nullptr ||
        !wglChoosePixelFormatARB(impl_->dc, pixelAttribs, nullptr, 1, &format,
                                 &formatCount) ||
        formatCount == 0) {
        // Repli sans multi-échantillonnage.
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        format = ChoosePixelFormat(impl_->dc, &pfd);
    }

    PIXELFORMATDESCRIPTOR chosen{};
    DescribePixelFormat(impl_->dc, format, sizeof(chosen), &chosen);
    SetPixelFormat(impl_->dc, format, &chosen);

    const int contextAttribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        0,
    };
    impl_->context = wglCreateContextAttribsARB(impl_->dc, nullptr, contextAttribs);
    if (impl_->context == nullptr) {
        std::fprintf(stderr, "Contexte OpenGL 3.3 core indisponible.\n");
        return false;
    }
    wglMakeCurrent(impl_->dc, impl_->context);

    const char* missing = nullptr;
    if (!gfx::loadGlFunctions(&missing)) {
        std::fprintf(stderr, "Point d'entrée OpenGL manquant : %s\n",
                     missing ? missing : "(inconnu)");
        return false;
    }

    ShowWindow(impl_->hwnd, SW_SHOW);
    setVSync(false);
    return true;
}

void Window::destroy() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->context != nullptr) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(impl_->context);
        impl_->context = nullptr;
    }
    if (impl_->dc != nullptr) {
        ReleaseDC(impl_->hwnd, impl_->dc);
        impl_->dc = nullptr;
    }
    if (impl_->hwnd != nullptr) {
        DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }
}

bool Window::pumpEvents() {
    input_.pressed.fill(false);
    input_.nativePressed.fill(false);
    input_.mouseDeltaX = 0.0f;
    input_.mouseDeltaY = 0.0f;
    input_.wheelDelta = 0.0f;

    MSG message;
    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return !impl_->shouldClose;
}

void Window::swapBuffers() {
    SwapBuffers(impl_->dc);
}

void Window::setVSync(bool enabled) {
    vsync_ = enabled;
    if (wglSwapIntervalEXT != nullptr) {
        wglSwapIntervalEXT(enabled ? 1 : 0);
    }
}

void Window::setTitle(const std::string& title) {
    if (impl_->hwnd != nullptr) {
        SetWindowTextA(impl_->hwnd, title.c_str());
    }
}

}  // namespace sfc::platform
