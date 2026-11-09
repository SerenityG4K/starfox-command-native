#include "gl_api.h"

namespace sfc::gfx {

#define SFC_DEFINE(name, ret, params) PFN_##name name = nullptr;
SFC_GL_FUNCTIONS(SFC_DEFINE)
#undef SFC_DEFINE

namespace {

// wglGetProcAddress ne connaît pas les fonctions du noyau 1.1 ; celles-ci
// doivent être prises directement dans opengl32.dll.
void* resolve(const char* name) {
    void* address = reinterpret_cast<void*>(wglGetProcAddress(name));
    const auto invalid = [](void* p) {
        return p == nullptr || p == reinterpret_cast<void*>(1) ||
               p == reinterpret_cast<void*>(2) || p == reinterpret_cast<void*>(3) ||
               p == reinterpret_cast<void*>(-1);
    };
    if (invalid(address)) {
        static HMODULE gl = LoadLibraryA("opengl32.dll");
        address = gl ? reinterpret_cast<void*>(GetProcAddress(gl, name)) : nullptr;
    }
    return address;
}

}  // namespace

bool loadGlFunctions(const char** missing) {
#define SFC_LOAD(name, ret, params)                              \
    name = reinterpret_cast<PFN_##name>(resolve(#name));         \
    if (name == nullptr) {                                       \
        if (missing != nullptr) { *missing = #name; }            \
        return false;                                            \
    }
    SFC_GL_FUNCTIONS(SFC_LOAD)
#undef SFC_LOAD
    return true;
}

}  // namespace sfc::gfx
