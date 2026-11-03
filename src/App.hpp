#pragma once

#include "Config.hpp"
#include "HyprIPC.hpp"
#include "Protocols.hpp"
#include "Renderer.hpp"
#include "Snow.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class CApp;

struct SOutput {
    CApp*                  app        = nullptr;
    uint32_t               globalName = 0;
    wl_output*             output     = nullptr;
    std::string            name;
    int32_t                outputScale = 1;
    bool                   ready       = false; // received the first wl_output.done

    wl_surface*            surface      = nullptr;
    zwlr_layer_surface_v1* layerSurface = nullptr;
    wl_egl_window*         eglWindow    = nullptr;
    EGLSurface             eglSurface   = EGL_NO_SURFACE;
    wl_callback*           frameCallback = nullptr;

    uint32_t               width = 0, height = 0; // logical size
    int32_t                preferredScale = 0;    // from wl_surface v6, 0 = unknown
    int32_t                bufferScale    = 1;
    bool                   configured     = false;
    bool                   fullscreen     = false; // a fullscreen window hides the snow

    uint64_t               lastStepNs = 0; // last simulation step
    uint64_t               nextDueNs  = 0; // fps cap
    CSnow                  snow;
};

class CApp {
  public:
    explicit CApp(std::string configPath);
    ~CApp();

    int  run();

    // wayland listener entry points
    void onGlobal(wl_registry* registry, uint32_t name, const char* iface, uint32_t version);
    void onGlobalRemove(uint32_t name);
    void onOutputDone(SOutput* out);
    void onConfigure(SOutput* out, uint32_t serial, uint32_t w, uint32_t h);
    void onClosed(SOutput* out);
    void onFrame(SOutput* out);
    void onPreferredScale(SOutput* out, int32_t scale);

  private:
    bool                                  wantsOutput(const SOutput* out) const;
    void                                  createSurface(SOutput* out);
    void                                  destroySurface(SOutput* out);
    void                                  applySize(SOutput* out);
    void                                  draw(SOutput* out);
    void                                  requestFrame(SOutput* out);
    void                                  syncSurfaces();
    void                                  reloadConfig(bool initial);
    void                                  setPaused(bool paused);
    void                                  resume(SOutput* out);
    void                                  refreshWindows();
    void                                  refreshOptions();
    void                                  armTimer();

    CConfigManager                        m_config;
    CRenderer                             m_renderer;
    SSnowConfig                           m_snow;
    bool                                  m_paused  = false;
    bool                                  m_running = true;

    wl_display*                           m_display    = nullptr;
    wl_registry*                          m_registry   = nullptr;
    wl_compositor*                        m_compositor = nullptr;
    uint32_t                              m_compositorVersion = 0;
    zwlr_layer_shell_v1*                  m_layerShell = nullptr;
    std::vector<std::unique_ptr<SOutput>> m_outputs;

    int                                   m_inotifyFd = -1;
    int                                   m_signalFd  = -1;
    int                                   m_timerFd   = -1;

    CHyprIPC                              m_ipc;
    bool                                  m_ipcOk      = false;
    int                                   m_borderSize = 0;
    int                                   m_rounding   = 0;
};
