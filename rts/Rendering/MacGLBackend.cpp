#ifdef __APPLE__

#include "MacGLBackend.h"

#include <EGL/egl.h>
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
    EGLDisplay   g_dpy   = EGL_NO_DISPLAY;
    EGLContext   g_ctx   = EGL_NO_CONTEXT;
    EGLSurface   g_pbuf  = EGL_NO_SURFACE;
    EGLConfig    g_cfg   = nullptr;
    SDL_Window*  g_win   = nullptr;
    SDL_Renderer* g_rdr  = nullptr;
    SDL_Texture* g_tex   = nullptr;
    int          g_w = 0, g_h = 0;
    int          g_major = 0, g_minor = 0;
    std::vector<uint8_t> g_pixbuf;

    using PFN_glReadPixels      = void (*)(int, int, int, int, unsigned, unsigned, void*);
    using PFN_glGetString       = const unsigned char* (*)(unsigned);
    using PFN_glFinish          = void (*)();
    using PFN_glBindFramebuffer = void (*)(unsigned target, unsigned framebuffer);
    using PFN_glReadBuffer      = void (*)(unsigned mode);
    using PFN_glGetIntegerv     = void (*)(unsigned pname, int* params);
    using PFN_glGetError        = unsigned (*)();
    using PFN_glClearColor      = void (*)(float, float, float, float);
    using PFN_glClear           = void (*)(unsigned);
    using PFN_glFlush           = void (*)();
    PFN_glReadPixels      p_glReadPixels      = nullptr;
    PFN_glFinish          p_glFinish          = nullptr;
    PFN_glBindFramebuffer p_glBindFramebuffer = nullptr;
    PFN_glReadBuffer      p_glReadBuffer      = nullptr;
    PFN_glGetIntegerv     p_glGetIntegerv     = nullptr;
    PFN_glGetError        p_glGetError        = nullptr;

    constexpr unsigned GL_RGBA              = 0x1908;
    constexpr unsigned GL_UNSIGNED_BYTE     = 0x1401;
    constexpr unsigned GL_VERSION           = 0x1F02;
    constexpr unsigned GL_FRAMEBUFFER       = 0x8D40;
    constexpr unsigned GL_READ_FRAMEBUFFER  = 0x8CA8;
    constexpr unsigned GL_DRAW_FRAMEBUFFER  = 0x8CA9;
    constexpr unsigned GL_READ_FRAMEBUFFER_BINDING = 0x8CAA;
    constexpr unsigned GL_DRAW_FRAMEBUFFER_BINDING = 0x8CA6;
    constexpr unsigned GL_FRONT             = 0x0404;
    constexpr unsigned GL_BACK              = 0x0405;

    bool createPbuffer(int w, int h) {
        if (g_pbuf != EGL_NO_SURFACE) {
            eglDestroySurface(g_dpy, g_pbuf);
            g_pbuf = EGL_NO_SURFACE;
        }
        // EGL_BACK_BUFFER means "this surface has a back buffer that the
        // GL context will draw to by default". Pbuffers default to
        // EGL_BACK_BUFFER but make it explicit so Mesa/Zink can't decide
        // we want a texture-backed pbuffer (which has different semantics).
        const EGLint attrs[] = {
            EGL_WIDTH, w,
            EGL_HEIGHT, h,
            EGL_TEXTURE_FORMAT, EGL_NO_TEXTURE,
            EGL_TEXTURE_TARGET, EGL_NO_TEXTURE,
            EGL_NONE
        };
        g_pbuf = eglCreatePbufferSurface(g_dpy, g_cfg, attrs);
        return g_pbuf != EGL_NO_SURFACE;
    }
}

bool MacGL::Init(SDL_Window* window, int reqMajor, int reqMinor) {
    g_win = window;

    // Force the SDL Metal renderer; the default may pick Apple's OpenGL backend
    // which is hard-deprecated and can't be paired with a Metal-flagged window.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");

    g_rdr = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_rdr) {
        std::fprintf(stderr, "[MacGL] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    int dw = 0, dh = 0;
    SDL_GetRendererOutputSize(g_rdr, &dw, &dh);
    g_w = dw; g_h = dh;

    g_tex = SDL_CreateTexture(g_rdr, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, g_w, g_h);
    if (!g_tex) {
        std::fprintf(stderr, "[MacGL] SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    g_pixbuf.resize(static_cast<size_t>(g_w) * g_h * 4);

    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY) { std::fprintf(stderr, "[MacGL] eglGetDisplay failed\n"); return false; }
    EGLint emaj = 0, emin = 0;
    if (!eglInitialize(g_dpy, &emaj, &emin)) { std::fprintf(stderr, "[MacGL] eglInitialize failed 0x%x\n", eglGetError()); return false; }
    if (!eglBindAPI(EGL_OPENGL_API)) { std::fprintf(stderr, "[MacGL] eglBindAPI(OPENGL) failed\n"); return false; }

    const EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    EGLint num = 0;
    if (!eglChooseConfig(g_dpy, cfg_attrs, &g_cfg, 1, &num) || num < 1) {
        std::fprintf(stderr, "[MacGL] eglChooseConfig failed\n"); return false;
    }
    if (!createPbuffer(g_w, g_h)) {
        std::fprintf(stderr, "[MacGL] eglCreatePbufferSurface failed 0x%x\n", eglGetError()); return false;
    }

    // Request a COMPATIBILITY profile - the engine's aGui (SelectMenu) and
    // some other code paths still use fixed-function matrix calls
    // (glMatrixMode / glLoadIdentity / gluOrtho2D) which silently no-op in
    // core profile. In core profile those calls leave the shader's matrix
    // uniform untouched, so menu quads with vertex positions in [0,1] land
    // in NDC [0,1] (upper-right quadrant) instead of being orthographically
    // projected to fill the viewport.
    const EGLint ctx_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION, reqMajor,
        EGL_CONTEXT_MINOR_VERSION, reqMinor,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
        EGL_NONE
    };
    g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (g_ctx == EGL_NO_CONTEXT) {
        // Fallback: request core, then version-only.
        const EGLint ctx_core[] = {
            EGL_CONTEXT_MAJOR_VERSION, reqMajor,
            EGL_CONTEXT_MINOR_VERSION, reqMinor,
            EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
            EGL_NONE
        };
        g_ctx = eglCreateContext(g_dpy, g_cfg, EGL_NO_CONTEXT, ctx_core);
        if (g_ctx == EGL_NO_CONTEXT) {
            std::fprintf(stderr, "[MacGL] eglCreateContext failed 0x%x\n", eglGetError()); return false;
        }
        std::fprintf(stderr, "[MacGL] WARNING: compat profile unavailable, using core\n");
    }

    if (!eglMakeCurrent(g_dpy, g_pbuf, g_pbuf, g_ctx)) {
        std::fprintf(stderr, "[MacGL] eglMakeCurrent failed 0x%x\n", eglGetError()); return false;
    }

    p_glReadPixels      = (PFN_glReadPixels)eglGetProcAddress("glReadPixels");
    p_glFinish          = (PFN_glFinish)eglGetProcAddress("glFinish");
    p_glBindFramebuffer = (PFN_glBindFramebuffer)eglGetProcAddress("glBindFramebuffer");
    p_glReadBuffer      = (PFN_glReadBuffer)eglGetProcAddress("glReadBuffer");
    p_glGetIntegerv     = (PFN_glGetIntegerv)eglGetProcAddress("glGetIntegerv");
    p_glGetError        = (PFN_glGetError)eglGetProcAddress("glGetError");


    auto gs = (PFN_glGetString)eglGetProcAddress("glGetString");
    if (gs) {
        const char* ver = (const char*)gs(GL_VERSION);
        if (ver) std::sscanf(ver, "%d.%d", &g_major, &g_minor);
    }

    return true;
}

void MacGL::Present() {
    if (!p_glReadPixels || !g_tex || !g_rdr || g_w <= 0 || g_h <= 0) return;

    // Defensive: always make sure our EGL pbuffer context is current on
    // this thread. Various engine paths (mtLoading, certain SDL flows)
    // can unbind it without us noticing, after which every GL call
    // silently no-ops. Diagnose by comparing what eglGetCurrentContext
    // reports against g_ctx and complain once if they ever differ.
    {
        EGLContext cur = eglGetCurrentContext();
        if (cur != g_ctx) {
            static bool s_warned = false;
            if (!s_warned) {
                s_warned = true;
                std::fprintf(stderr,
                    "[MacGL] WARNING: EGL context drift detected at Present "
                    "(cur=%p expected=%p), re-binding\n",
                    (void*)cur, (void*)g_ctx);
            }
            eglMakeCurrent(g_dpy, g_pbuf, g_pbuf, g_ctx);
        }
    }

    // Engine paths that use deferred rendering (BAR, modern Recoil GL4)
    // leave a custom FBO bound when SwapBuffers is called. We must read
    // from the *default* framebuffer (the pbuffer's backing surface),
    // which is FBO 0 — so explicitly bind it for both read and draw,
    // and select FRONT (pbuffers are single-buffered in EGL by default).
    if (p_glBindFramebuffer) {
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }
    // Pbuffers nominally have only a FRONT buffer, but Zink-on-Vulkan
    // routes draw operations into a Vulkan VkImage and never moves
    // them to a separate "front" the way a real GL driver would. The
    // engine's draw commands therefore end up in what GL calls the
    // BACK buffer; reading from FRONT returns zeros.
    if (p_glReadBuffer) p_glReadBuffer(GL_BACK);

    // glFinish blocks until the GPU has actually completed the frame.
    // Without it, glReadPixels can race against in-flight Vulkan command
    // buffers in Zink+KosmicKrisp, producing visible flicker as half-rendered
    // frames get presented.
    if (p_glFinish) p_glFinish();
    p_glReadPixels(0, 0, g_w, g_h, GL_RGBA, GL_UNSIGNED_BYTE, g_pixbuf.data());

    // glReadPixels returns rows bottom-to-top (GL convention). SDL surfaces
    // store rows top-to-bottom. If we flip on display via SDL_FLIP_VERTICAL,
    // the image looks right-side-up but mouse Y becomes inverted (engine
    // thinks click-at-top-of-display = click-at-bottom-of-viewport-in-GL).
    // Instead, reverse rows here so the SDL texture is naturally top-first
    // and SDL_RenderCopy needs no flip — keeps mouse/screen coords aligned.
    {
        const size_t stride = static_cast<size_t>(g_w) * 4;
        std::vector<uint8_t> tmp(stride);
        for (int y = 0; y < g_h / 2; ++y) {
            uint8_t* top = g_pixbuf.data() + y * stride;
            uint8_t* bot = g_pixbuf.data() + (g_h - 1 - y) * stride;
            std::memcpy(tmp.data(), top,        stride);
            std::memcpy(top,        bot,        stride);
            std::memcpy(bot,        tmp.data(), stride);
        }
    }

    SDL_UpdateTexture(g_tex, nullptr, g_pixbuf.data(), g_w * 4);
    SDL_RenderClear(g_rdr);
    SDL_RenderCopy(g_rdr, g_tex, nullptr, nullptr);
    SDL_RenderPresent(g_rdr);
}

void MacGL::MakeCurrent() {
    if (g_dpy != EGL_NO_DISPLAY && g_ctx != EGL_NO_CONTEXT && g_pbuf != EGL_NO_SURFACE) {
        eglMakeCurrent(g_dpy, g_pbuf, g_pbuf, g_ctx);
    }
}

void MacGL::Resize(int w, int h) {
    if (w == g_w && h == g_h) return;
    if (w <= 0 || h <= 0) return;
    g_w = w; g_h = h;

    if (g_tex) { SDL_DestroyTexture(g_tex); g_tex = nullptr; }
    g_tex = SDL_CreateTexture(g_rdr, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    g_pixbuf.resize(static_cast<size_t>(w) * h * 4);

    if (g_dpy != EGL_NO_DISPLAY && g_ctx != EGL_NO_CONTEXT) {
        createPbuffer(w, h);
        eglMakeCurrent(g_dpy, g_pbuf, g_pbuf, g_ctx);
    }
}

void MacGL::Shutdown() {
    if (g_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_pbuf != EGL_NO_SURFACE) { eglDestroySurface(g_dpy, g_pbuf); g_pbuf = EGL_NO_SURFACE; }
        if (g_ctx  != EGL_NO_CONTEXT) { eglDestroyContext(g_dpy, g_ctx);  g_ctx  = EGL_NO_CONTEXT; }
        eglTerminate(g_dpy);
        g_dpy = EGL_NO_DISPLAY;
    }
    if (g_tex) { SDL_DestroyTexture(g_tex); g_tex = nullptr; }
    if (g_rdr) { SDL_DestroyRenderer(g_rdr); g_rdr = nullptr; }
}

int MacGL::GetMajorVersion() { return g_major; }
int MacGL::GetMinorVersion() { return g_minor; }

void* MacGL::ProcAddress(const char* name) {
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

#endif // __APPLE__
