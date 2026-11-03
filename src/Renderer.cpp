#include "Renderer.hpp"

#include <EGL/eglext.h>

#include <cstddef>
#include <print>

static const char* VERT_SRC = R"(#version 300 es
layout(location = 0) in vec2 a_corner;   // quad corner in [-1, 1]
layout(location = 1) in vec4 a_flake;    // x, y, radius, alpha
layout(location = 2) in vec2 a_rotShape; // rotation, shape

uniform vec2 u_resolution;

out vec2  v_uv;
out float v_alpha;
out float v_rot;
out float v_shape;
out float v_radius;

void main() {
    // pad by one pixel so antialiased edges are not clipped
    float r  = a_flake.z + 1.0;
    vec2  px = a_flake.xy + a_corner * r;
    gl_Position = vec4(px.x / u_resolution.x * 2.0 - 1.0, 1.0 - px.y / u_resolution.y * 2.0, 0.0, 1.0);

    v_uv     = a_corner * (r / a_flake.z);
    v_alpha  = a_flake.w;
    v_rot    = a_rotShape.x;
    v_shape  = a_rotShape.y;
    v_radius = a_flake.z;
}
)";

static const char* FRAG_SRC = R"(#version 300 es
precision mediump float;

in vec2  v_uv;
in float v_alpha;
in float v_rot;
in float v_shape;
in float v_radius;

uniform vec4 u_color; // straight alpha

out vec4 o_color;

float segment(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0));
}

void main() {
    float r  = length(v_uv);
    float aa = 1.0 / max(v_radius, 0.5); // ~1 pixel in uv units
    float a;

    if (v_shape < 0.5) {
        // soft dot: solid core, gentle falloff
        a = 1.0 - smoothstep(max(0.0, 0.45 - aa), 1.0, r);
    } else {
        // six-armed crystal: fold the angle into one 60 degree wedge
        float ang = atan(v_uv.y, v_uv.x) + v_rot;
        ang = mod(ang, 1.0471976) - 0.5235988;
        vec2 p = vec2(cos(ang), abs(sin(ang))) * r;

        float d = segment(p, vec2(0.0), vec2(0.92, 0.0));               // main arm
        d = min(d, segment(p, vec2(0.48, 0.0), vec2(0.70, 0.22)));      // branch
        d = min(d, segment(p, vec2(0.26, 0.0), vec2(0.38, 0.12)));      // inner branch

        float w = max(0.07, aa);
        a  = 1.0 - smoothstep(w - aa * 0.5, w + aa, d);
        a  = max(a, 1.0 - smoothstep(0.14, 0.14 + aa, r));              // center
        a  = max(a, 0.25 * (1.0 - r) * (1.0 - r));                      // faint glow
    }

    a *= v_alpha * u_color.a;
    if (a <= 0.003) discard;
    o_color = vec4(u_color.rgb * a, a); // premultiplied
}
)";

static const char* PILE_VERT_SRC = R"(#version 300 es
layout(location = 0) in vec4 a_vert; // x, y, depth below surface, unused

uniform vec2 u_resolution;

out float v_depth;

void main() {
    gl_Position = vec4(a_vert.x / u_resolution.x * 2.0 - 1.0, 1.0 - a_vert.y / u_resolution.y * 2.0, 0.0, 1.0);
    v_depth     = a_vert.z;
}
)";

static const char* PILE_FRAG_SRC = R"(#version 300 es
precision mediump float;

in float v_depth;

uniform vec4  u_color; // straight alpha
uniform float u_scale;

out vec4 o_color;

void main() {
    // soft one-pixel top edge; very thin dustings stay faint
    float a = clamp(v_depth / u_scale, 0.0, 1.0) * u_color.a;
    // packed snow deeper down picks up a cool blue shade
    float shade = clamp(v_depth / (14.0 * u_scale), 0.0, 1.0);
    vec3  c     = u_color.rgb * mix(vec3(1.0), vec3(0.78, 0.85, 0.96), shade * 0.75);
    o_color = vec4(c * a, a); // premultiplied
}
)";

CRenderer::~CRenderer() {
    destroy();
}

void CRenderer::destroy() {
    if (m_display == EGL_NO_DISPLAY)
        return;
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (m_context != EGL_NO_CONTEXT)
        eglDestroyContext(m_display, m_context);
    eglTerminate(m_display);
    m_display = EGL_NO_DISPLAY;
    m_context = EGL_NO_CONTEXT;
    m_glReady = false;
}

bool CRenderer::init(wl_display* display) {
    m_display = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display, nullptr);
    if (m_display == EGL_NO_DISPLAY) {
        std::println(stderr, "egl: no display");
        return false;
    }
    if (!eglInitialize(m_display, nullptr, nullptr)) {
        std::println(stderr, "egl: initialize failed");
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        return false;

    const EGLint cfgAttrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE,
    };
    EGLint n = 0;
    if (!eglChooseConfig(m_display, cfgAttrs, &m_config, 1, &n) || n < 1) {
        std::println(stderr, "egl: no RGBA8 GLES3 config");
        return false;
    }

    const EGLint ctxAttrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    m_context               = eglCreateContext(m_display, m_config, EGL_NO_CONTEXT, ctxAttrs);
    if (m_context == EGL_NO_CONTEXT) {
        std::println(stderr, "egl: context creation failed");
        return false;
    }
    return true;
}

EGLSurface CRenderer::createSurface(wl_egl_window* window) {
    EGLSurface s = eglCreatePlatformWindowSurface(m_display, m_config, window, nullptr);
    if (s == EGL_NO_SURFACE)
        return s;

    eglMakeCurrent(m_display, s, s, m_context);
    // pacing comes from wl_surface.frame callbacks; swaps must never block
    eglSwapInterval(m_display, 0);

    if (!m_glReady && !initGL()) {
        eglDestroySurface(m_display, s);
        return EGL_NO_SURFACE;
    }
    return s;
}

void CRenderer::destroySurface(EGLSurface surface) {
    if (surface == EGL_NO_SURFACE)
        return;
    if (eglGetCurrentSurface(EGL_DRAW) == surface)
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(m_display, surface);
}

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::println(stderr, "gl: shader compile failed: {}", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link(const char* vertSrc, const char* fragSrc) {
    GLuint vs = compile(GL_VERTEX_SHADER, vertSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs)
        return 0;

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::println(stderr, "gl: program link failed: {}", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool CRenderer::initGL() {
    m_program     = link(VERT_SRC, FRAG_SRC);
    m_pileProgram = link(PILE_VERT_SRC, PILE_FRAG_SRC);
    if (!m_program || !m_pileProgram)
        return false;

    m_uPileResolution = glGetUniformLocation(m_pileProgram, "u_resolution");
    m_uPileColor      = glGetUniformLocation(m_pileProgram, "u_color");
    m_uPileScale      = glGetUniformLocation(m_pileProgram, "u_scale");

    glGenVertexArrays(1, &m_pileVao);
    glBindVertexArray(m_pileVao);
    glGenBuffers(1, &m_pileVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_pileVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(SPileVertex), nullptr);

    m_uResolution = glGetUniformLocation(m_program, "u_resolution");
    m_uColor      = glGetUniformLocation(m_program, "u_color");

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    static const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenBuffers(1, &m_quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glGenBuffers(1, &m_instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    constexpr GLsizei stride = sizeof(SFlakeInstance);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(SFlakeInstance, x));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)offsetof(SFlakeInstance, rotation));
    glVertexAttribDivisor(2, 1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    m_glReady = true;
    return true;
}

static void setColor(GLint loc, uint32_t argb) {
    glUniform4f(loc, ((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f);
}

void CRenderer::begin(EGLSurface surface, int width, int height) {
    eglMakeCurrent(m_display, surface, surface, m_context);
    m_width  = width;
    m_height = height;

    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
}

void CRenderer::drawPiles(const std::vector<SPileVertex>& verts, const std::vector<SPileStrip>& strips, uint32_t argb, float scale) {
    if (strips.empty())
        return;

    glUseProgram(m_pileProgram);
    glUniform2f(m_uPileResolution, (float)m_width, (float)m_height);
    glUniform1f(m_uPileScale, scale);
    setColor(m_uPileColor, argb);

    glBindVertexArray(m_pileVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_pileVbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(SPileVertex), verts.data(), GL_STREAM_DRAW);
    for (const auto& s : strips)
        glDrawArrays(GL_TRIANGLE_STRIP, s.first, s.count);
}

void CRenderer::drawFlakes(const std::vector<SFlakeInstance>& flakes, uint32_t argb) {
    if (flakes.empty())
        return;

    glUseProgram(m_program);
    glUniform2f(m_uResolution, (float)m_width, (float)m_height);
    setColor(m_uColor, argb);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, flakes.size() * sizeof(SFlakeInstance), flakes.data(), GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)flakes.size());
}

void CRenderer::present(EGLSurface surface) {
    eglSwapBuffers(m_display, surface);
}
