#pragma once

#include "Snow.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <cstdint>
#include <vector>

// One EGL context shared by every output; each output gets its own EGLSurface.
class CRenderer {
  public:
    ~CRenderer();

    // Releases GL/EGL resources. Must run before the wl_display is disconnected.
    void       destroy();

    bool       init(wl_display* display);
    EGLSurface createSurface(wl_egl_window* window);
    void       destroySurface(EGLSurface surface);

    // Makes `surface` current and clears it.
    void       begin(EGLSurface surface, int width, int height);
    void       drawPiles(const std::vector<SPileVertex>& verts, const std::vector<SPileStrip>& strips, uint32_t argb, float scale);
    void       drawFlakes(const std::vector<SFlakeInstance>& flakes, uint32_t argb);
    // Swaps buffers (this commits the wl_surface).
    void       present(EGLSurface surface);

  private:
    bool       initGL();

    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLConfig  m_config  = nullptr;
    EGLContext m_context = EGL_NO_CONTEXT;

    bool       m_glReady = false;
    GLuint     m_program = 0, m_vao = 0, m_quadVbo = 0, m_instanceVbo = 0;
    GLint      m_uResolution = -1, m_uColor = -1;

    GLuint     m_pileProgram = 0, m_pileVao = 0, m_pileVbo = 0;
    GLint      m_uPileResolution = -1, m_uPileColor = -1, m_uPileScale = -1;

    int        m_width = 0, m_height = 0;
};
