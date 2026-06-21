#pragma once

#ifdef __APPLE__

// Thin presentation shim for macOS: the engine renders into a Mesa EGL pbuffer
// (driven by Zink + KosmicKrisp → Metal) and the contents are copied into an
// SDL_Renderer/Metal-backed streaming texture each frame.
//
// Apple removed real OpenGL support past 4.1 so SDL_GL_CreateContext gives us
// nothing useful. We bypass it on macOS and drive Mesa directly.

struct SDL_Window;

namespace MacGL {
    bool  Init(SDL_Window* window, int reqMajor, int reqMinor);
    void  Present();
    void  Resize(int drawableWidth, int drawableHeight);
    void  Shutdown();
    int   GetMajorVersion();
    int   GetMinorVersion();
    void* ProcAddress(const char* name);

    // Re-bind our EGL pbuffer context to the calling thread. The engine
    // occasionally calls SDL_GL_MakeCurrent expecting to restore "its"
    // GL context (e.g. on the main thread after mtLoading has run, or
    // before any explicit SwapBuffers). With our setup that SDL call
    // unbinds the EGL context and leaves GL with no current context —
    // every subsequent draw silently no-ops until we re-make-current.
    // This helper is the macOS replacement.
    void  MakeCurrent();
}

#endif // __APPLE__
