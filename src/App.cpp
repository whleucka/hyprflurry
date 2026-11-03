#include "App.hpp"
#include "Json.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <poll.h>
#include <print>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

static uint64_t nowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1'000'000'000ull + (uint64_t)ts.tv_nsec;
}

// ---- wayland listeners ------------------------------------------------------

static void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* iface, uint32_t version) {
    static_cast<CApp*>(data)->onGlobal(registry, name, iface, version);
}
static void registryGlobalRemove(void* data, wl_registry*, uint32_t name) {
    static_cast<CApp*>(data)->onGlobalRemove(name);
}
static const wl_registry_listener REGISTRY_LISTENER = {registryGlobal, registryGlobalRemove};

static void outputGeometry(void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {}
static void outputMode(void*, wl_output*, uint32_t, int32_t, int32_t, int32_t) {}
static void outputDone(void* data, wl_output*) {
    auto* out = static_cast<SOutput*>(data);
    out->app->onOutputDone(out);
}
static void outputScale(void* data, wl_output*, int32_t scale) {
    static_cast<SOutput*>(data)->outputScale = scale;
}
static void outputName(void* data, wl_output*, const char* name) {
    static_cast<SOutput*>(data)->name = name;
}
static void outputDescription(void*, wl_output*, const char*) {}
static const wl_output_listener OUTPUT_LISTENER = {outputGeometry, outputMode, outputDone, outputScale, outputName, outputDescription};

static void surfaceEnter(void*, wl_surface*, wl_output*) {}
static void surfaceLeave(void*, wl_surface*, wl_output*) {}
static void surfacePreferredScale(void* data, wl_surface*, int32_t scale) {
    auto* out = static_cast<SOutput*>(data);
    out->app->onPreferredScale(out, scale);
}
static void surfacePreferredTransform(void*, wl_surface*, uint32_t) {}
static const wl_surface_listener SURFACE_LISTENER = {surfaceEnter, surfaceLeave, surfacePreferredScale, surfacePreferredTransform};

static void layerConfigure(void* data, zwlr_layer_surface_v1*, uint32_t serial, uint32_t w, uint32_t h) {
    auto* out = static_cast<SOutput*>(data);
    out->app->onConfigure(out, serial, w, h);
}
static void layerClosed(void* data, zwlr_layer_surface_v1*) {
    auto* out = static_cast<SOutput*>(data);
    out->app->onClosed(out);
}
static const zwlr_layer_surface_v1_listener LAYER_LISTENER = {layerConfigure, layerClosed};

static void frameDone(void* data, wl_callback* cb, uint32_t) {
    auto* out = static_cast<SOutput*>(data);
    wl_callback_destroy(cb);
    out->frameCallback = nullptr;
    out->app->onFrame(out);
}
static const wl_callback_listener FRAME_LISTENER = {frameDone};

// ---- app --------------------------------------------------------------------

CApp::CApp(std::string configPath) : m_config(std::move(configPath)) {}

CApp::~CApp() {
    for (auto& out : m_outputs) {
        destroySurface(out.get());
        if (out->output)
            wl_output_release(out->output);
    }
    m_outputs.clear();
    m_renderer.destroy();
    if (m_layerShell)
        zwlr_layer_shell_v1_destroy(m_layerShell);
    if (m_compositor)
        wl_compositor_destroy(m_compositor);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_display) {
        wl_display_flush(m_display);
        wl_display_disconnect(m_display);
    }
    if (m_inotifyFd >= 0)
        close(m_inotifyFd);
    if (m_signalFd >= 0)
        close(m_signalFd);
    if (m_timerFd >= 0)
        close(m_timerFd);
}

void CApp::onGlobal(wl_registry* registry, uint32_t name, const char* iface, uint32_t version) {
    if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        m_compositorVersion = std::min<uint32_t>(version, 6);
        m_compositor        = (wl_compositor*)wl_registry_bind(registry, name, &wl_compositor_interface, m_compositorVersion);
    } else if (std::strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        m_layerShell = (zwlr_layer_shell_v1*)wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min<uint32_t>(version, 4));
    } else if (std::strcmp(iface, wl_output_interface.name) == 0) {
        if (version < 4) {
            std::println(stderr, "wl_output v{} lacks names, skipping", version);
            return;
        }
        auto out        = std::make_unique<SOutput>();
        out->app        = this;
        out->globalName = name;
        out->output     = (wl_output*)wl_registry_bind(registry, name, &wl_output_interface, 4);
        wl_output_add_listener(out->output, &OUTPUT_LISTENER, out.get());
        m_outputs.push_back(std::move(out));
    }
}

void CApp::onGlobalRemove(uint32_t name) {
    auto it = std::find_if(m_outputs.begin(), m_outputs.end(), [&](auto& o) { return o->globalName == name; });
    if (it == m_outputs.end())
        return;
    destroySurface(it->get());
    wl_output_release((*it)->output);
    m_outputs.erase(it);
}

bool CApp::wantsOutput(const SOutput* out) const {
    if (m_snow.monitors.empty())
        return true;
    return std::find(m_snow.monitors.begin(), m_snow.monitors.end(), out->name) != m_snow.monitors.end();
}

void CApp::onOutputDone(SOutput* out) {
    out->ready = true;
    if (!out->surface && m_layerShell && wantsOutput(out))
        createSurface(out);
    else if (out->surface && out->configured)
        applySize(out); // output scale may have changed
}

void CApp::createSurface(SOutput* out) {
    out->surface = wl_compositor_create_surface(m_compositor);
    if (m_compositorVersion >= 6)
        wl_surface_add_listener(out->surface, &SURFACE_LISTENER, out);

    // empty input region: every click goes through to whatever is underneath
    wl_region* region = wl_compositor_create_region(m_compositor);
    wl_surface_set_input_region(out->surface, region);
    wl_region_destroy(region);

    const auto layer  = m_snow.background ? ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND : ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
    out->layerSurface = zwlr_layer_shell_v1_get_layer_surface(m_layerShell, out->surface, out->output, layer, "hyprflurry");
    zwlr_layer_surface_v1_set_anchor(out->layerSurface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                         ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(out->layerSurface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(out->layerSurface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_set_size(out->layerSurface, 0, 0);
    zwlr_layer_surface_v1_add_listener(out->layerSurface, &LAYER_LISTENER, out);
    wl_surface_commit(out->surface);
}

void CApp::destroySurface(SOutput* out) {
    if (out->frameCallback) {
        wl_callback_destroy(out->frameCallback);
        out->frameCallback = nullptr;
    }
    if (out->eglSurface != EGL_NO_SURFACE) {
        m_renderer.destroySurface(out->eglSurface);
        out->eglSurface = EGL_NO_SURFACE;
    }
    if (out->eglWindow) {
        wl_egl_window_destroy(out->eglWindow);
        out->eglWindow = nullptr;
    }
    if (out->layerSurface) {
        zwlr_layer_surface_v1_destroy(out->layerSurface);
        out->layerSurface = nullptr;
    }
    if (out->surface) {
        wl_surface_destroy(out->surface);
        out->surface = nullptr;
    }
    out->configured     = false;
    out->preferredScale = 0;
    out->lastStepNs     = 0;
    out->nextDueNs      = 0;
}

void CApp::onPreferredScale(SOutput* out, int32_t scale) {
    out->preferredScale = scale;
    if (out->configured)
        applySize(out);
}

// Sizes the EGL window to logical size * scale and respawns the snow if anything changed.
void CApp::applySize(SOutput* out) {
    const int32_t scale = std::max(1, out->preferredScale > 0 ? out->preferredScale : out->outputScale);
    const int     bw = (int)out->width * scale, bh = (int)out->height * scale;

    if (!out->eglWindow) {
        out->eglWindow  = wl_egl_window_create(out->surface, bw, bh);
        out->eglSurface = m_renderer.createSurface(out->eglWindow);
        if (out->eglSurface == EGL_NO_SURFACE) {
            std::println(stderr, "failed to create EGL surface for {}", out->name);
            m_running = false;
            return;
        }
    } else if (scale == out->bufferScale) {
        int cw = 0, ch = 0;
        wl_egl_window_get_attached_size(out->eglWindow, &cw, &ch);
        if (cw == bw && ch == bh)
            return;
    }

    wl_egl_window_resize(out->eglWindow, bw, bh, 0, 0);
    wl_surface_set_buffer_scale(out->surface, scale);
    out->bufferScale = scale;
    out->snow.reset(m_snow, bw, bh, (float)scale);
}

void CApp::onConfigure(SOutput* out, uint32_t serial, uint32_t w, uint32_t h) {
    zwlr_layer_surface_v1_ack_configure(out->layerSurface, serial);
    out->width  = w;
    out->height = h;
    applySize(out);

    if (!out->configured) {
        out->configured = true;
        draw(out);
    }
}

void CApp::onClosed(SOutput* out) {
    destroySurface(out);
}

void CApp::onFrame(SOutput* out) {
    draw(out);
}

void CApp::requestFrame(SOutput* out) {
    if (out->frameCallback)
        return;
    out->frameCallback = wl_surface_frame(out->surface);
    wl_callback_add_listener(out->frameCallback, &FRAME_LISTENER, out);
}

void CApp::draw(SOutput* out) {
    if (!out->configured || out->eglSurface == EGL_NO_SURFACE)
        return;

    const int bw = (int)out->width * out->bufferScale, bh = (int)out->height * out->bufferScale;

    if (m_paused) {
        // leave one transparent frame up and stop asking for more
        m_renderer.begin(out->eglSurface, bw, bh);
        m_renderer.present(out->eglSurface);
        return;
    }
    if (out->fullscreen)
        return; // nothing is visible; stop until resume()

    const uint64_t now = nowNs();

    // fps cap: frame callbacks arrive at the refresh rate; skip the ones that come too early
    if (m_snow.fps > 0 && out->nextDueNs && now + 2'000'000 < out->nextDueNs) {
        requestFrame(out);
        wl_surface_commit(out->surface);
        return;
    }
    if (m_snow.fps > 0) {
        const uint64_t interval = 1'000'000'000ull / (uint64_t)m_snow.fps;
        out->nextDueNs          = std::max(out->nextDueNs + interval, now);
    }

    // clamp so a long stall (suspend, compositor hiccup) doesn't teleport flakes
    const double dt = out->lastStepNs ? std::min((now - out->lastStepNs) / 1e9, 0.1) : 0.0;
    out->lastStepNs = now;
    out->snow.update(dt);

    m_renderer.begin(out->eglSurface, bw, bh);
    m_renderer.drawPiles(out->snow.pileVertices(), out->snow.pileStrips(), m_snow.color, (float)out->bufferScale);
    m_renderer.drawFlakes(out->snow.instances(), m_snow.color);
    requestFrame(out); // must be requested before the swap commits the surface
    m_renderer.present(out->eglSurface);
}

void CApp::setPaused(bool paused) {
    if (m_paused == paused)
        return;
    m_paused = paused;
    std::println("{}", paused ? "paused" : "resumed");
    for (auto& out : m_outputs) {
        out->lastStepNs = 0;
        out->nextDueNs  = 0;
        if (!out->frameCallback) // if a callback is pending, draw() will handle it
            draw(out.get());
    }
    if (!paused)
        refreshWindows();
}

// Restarts the frame loop of an output that stopped drawing.
void CApp::resume(SOutput* out) {
    out->lastStepNs = 0;
    out->nextDueNs  = 0;
    if (!out->frameCallback)
        draw(out);
}

void CApp::refreshOptions() {
    auto opt     = [&](const char* name) { return (int)SJson::parse(m_ipc.request(std::string{"j/getoption "} + name))["int"].number(0); };
    m_borderSize = opt("general:border_size");
    m_rounding   = opt("decoration:rounding");
}

// Polls Hyprland for visible windows and hands them to each output's snow.
void CApp::refreshWindows() {
    if (!m_ipcOk || m_paused)
        return;

    bool        ok1 = false, ok2 = false;
    const SJson monitors = SJson::parse(m_ipc.request("j/monitors"), &ok1);
    const SJson clients  = SJson::parse(m_ipc.request("j/clients"), &ok2);
    if (!ok1 || !ok2)
        return;

    for (auto& out : m_outputs) {
        if (!out->configured)
            continue;

        const SJson* mon = nullptr;
        for (const auto& m : monitors.arr)
            if (m["name"].string() == out->name)
                mon = &m;
        if (!mon)
            continue;

        const double  mx = (*mon)["x"].number(), my = (*mon)["y"].number();
        const int64_t monId    = (int64_t)(*mon)["id"].number(-1);
        const int64_t activeWs = (int64_t)(*mon)["activeWorkspace"]["id"].number();
        const int64_t specialWs = (int64_t)(*mon)["specialWorkspace"]["id"].number();
        const float   s        = (float)out->bufferScale;
        const float   border   = (float)m_borderSize;

        std::vector<SPlatform> platforms;
        bool                   fullscreen = false;
        for (const auto& c : clients.arr) {
            if (!c["mapped"].boolean() || c["hidden"].boolean())
                continue;
            const int64_t ws      = (int64_t)c["workspace"]["id"].number();
            const bool    onMon   = (int64_t)c["monitor"].number(-1) == monId;
            const bool    visible = ws == activeWs || (specialWs != 0 && ws == specialWs) || (c["pinned"].boolean() && onMon);
            if (!visible)
                continue;
            // 2 = real fullscreen (1 is maximized, which leaves bars and gaps visible)
            if (ws == activeWs && specialWs == 0 && (int)c["fullscreen"].number() >= 2)
                fullscreen = true;

            const float x = (float)(c["at"][0].number() - mx) - border, y = (float)(c["at"][1].number() - my) - border;
            const float w = (float)c["size"][0].number() + 2 * border, h = (float)c["size"][1].number() + 2 * border;
            const auto  id = std::strtoull(c["address"].string().c_str(), nullptr, 16);
            platforms.push_back({id, x * s, y * s, w * s, h * s, m_rounding * 0.5f * s});
        }

        out->snow.setPlatforms(std::move(platforms), activeWs * 1'000'003 + specialWs);

        if (fullscreen != out->fullscreen) {
            out->fullscreen = fullscreen;
            if (!fullscreen)
                resume(out.get());
        }
    }
}

void CApp::armTimer() {
    if (m_timerFd < 0)
        return;
    // window piles need to track moves and resizes, which Hyprland sends no events for;
    // otherwise only fullscreen state matters and a slower poll is enough
    const long        ms = m_snow.pile && m_snow.pileWindows ? 250 : 1000;
    const timespec    ts{.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1'000'000};
    const itimerspec  its{.it_interval = ts, .it_value = ts};
    timerfd_settime(m_timerFd, 0, &its, nullptr);
}

// Creates or destroys surfaces so they match the current config.
void CApp::syncSurfaces() {
    for (auto& out : m_outputs) {
        if (!out->ready)
            continue;
        const bool want = wantsOutput(out.get());
        if (want && !out->surface)
            createSurface(out.get());
        else if (!want && out->surface)
            destroySurface(out.get());
    }
}

void CApp::reloadConfig(bool initial) {
    const SSnowConfig old = m_snow;

    std::string err;
    if (!m_config.reload(err))
        std::println(stderr, "config error in {}: {}", m_config.path(), err);
    m_snow = m_config.get();

    if (initial)
        return;
    std::println("reloaded {}", m_config.path());

    // the layer can't be changed on an existing surface, so recreate them all
    if (old.background != m_snow.background) {
        for (auto& out : m_outputs)
            destroySurface(out.get());
    }
    syncSurfaces();

    for (auto& out : m_outputs) {
        if (!out->configured)
            continue;
        out->snow.reset(m_snow, out->width * out->bufferScale, out->height * out->bufferScale, (float)out->bufferScale);
        out->nextDueNs = 0;
    }
    armTimer();
    refreshWindows();
}

int CApp::run() {
    reloadConfig(true);

    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        std::println(stderr, "cannot connect to a wayland display");
        return 1;
    }

    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &REGISTRY_LISTENER, this);
    wl_display_roundtrip(m_display); // globals
    if (!m_compositor || !m_layerShell) {
        std::println(stderr, "compositor does not support wlr-layer-shell");
        return 1;
    }
    if (!m_renderer.init(m_display))
        return 1;
    wl_display_roundtrip(m_display); // output names + done
    syncSurfaces();
    wl_display_roundtrip(m_display); // first configure, so outputs know their size

    m_ipcOk = m_ipc.init();
    if (m_ipcOk)
        refreshOptions();
    else
        std::println(stderr, "not running under Hyprland: snow won't pile on windows");
    m_timerFd = m_ipcOk ? timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK) : -1;
    armTimer();
    refreshWindows();

    // signals: INT/TERM quit, USR1 toggles pause, HUP reloads the config
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGHUP);
    sigprocmask(SIG_BLOCK, &mask, nullptr);
    m_signalFd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);

    // watch the config's directory, since editors usually replace the file
    const std::filesystem::path cfgPath{m_config.path()};
    m_inotifyFd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (m_inotifyFd >= 0 && inotify_add_watch(m_inotifyFd, cfgPath.parent_path().c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) < 0) {
        close(m_inotifyFd);
        m_inotifyFd = -1;
    }

    const int wlFd = wl_display_get_fd(m_display);

    while (m_running) {
        while (wl_display_prepare_read(m_display) != 0)
            wl_display_dispatch_pending(m_display);
        wl_display_flush(m_display);

        // unused slots have fd -1, which poll() ignores
        pollfd fds[] = {{wlFd, POLLIN, 0}, {m_signalFd, POLLIN, 0}, {m_inotifyFd, POLLIN, 0}, {m_ipc.eventFd(), POLLIN, 0}, {m_timerFd, POLLIN, 0}};
        if (poll(fds, 5, -1) < 0) {
            wl_display_cancel_read(m_display);
            if (errno == EINTR)
                continue;
            break;
        }

        if (fds[0].revents & POLLIN) {
            if (wl_display_read_events(m_display) < 0)
                break;
        } else
            wl_display_cancel_read(m_display);
        if (fds[0].revents & (POLLERR | POLLHUP))
            break;
        if (wl_display_dispatch_pending(m_display) < 0)
            break;

        if (fds[1].revents & POLLIN) {
            signalfd_siginfo si;
            while (read(m_signalFd, &si, sizeof(si)) == sizeof(si)) {
                if (si.ssi_signo == SIGUSR1)
                    setPaused(!m_paused);
                else if (si.ssi_signo == SIGHUP)
                    reloadConfig(false);
                else
                    m_running = false;
            }
        }

        if (m_inotifyFd >= 0 && (fds[2].revents & POLLIN)) {
            alignas(inotify_event) char buf[4096];
            bool                        changed = false;
            ssize_t                     len;
            while ((len = read(m_inotifyFd, buf, sizeof(buf))) > 0) {
                for (char* p = buf; p < buf + len;) {
                    auto* ev = (inotify_event*)p;
                    if (ev->len && cfgPath.filename() == ev->name)
                        changed = true;
                    p += sizeof(inotify_event) + ev->len;
                }
            }
            if (changed)
                reloadConfig(false);
        }

        bool refresh = false;
        if (m_ipc.eventFd() >= 0 && (fds[3].revents & (POLLIN | POLLHUP))) {
            bool closed = false;
            refresh     = m_ipc.readEvents(closed);
            if (closed) {
                std::println(stderr, "lost the Hyprland event socket");
                m_running = false;
            }
        }
        if (m_timerFd >= 0 && (fds[4].revents & POLLIN)) {
            uint64_t expirations;
            if (read(m_timerFd, &expirations, sizeof(expirations)) > 0)
                refresh = true;
        }
        if (refresh)
            refreshWindows();
    }

    return 0;
}
