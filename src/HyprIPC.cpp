#include "HyprIPC.hpp"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int connectTo(const std::string& path, bool nonblock) {
    sockaddr_un addr{};
    if (path.size() >= sizeof(addr.sun_path))
        return -1;
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | (nonblock ? SOCK_NONBLOCK : 0), 0);
    if (fd < 0)
        return -1;
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

CHyprIPC::~CHyprIPC() {
    if (m_eventFd >= 0)
        close(m_eventFd);
}

bool CHyprIPC::init() {
    const char* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    const char* run = std::getenv("XDG_RUNTIME_DIR");
    if (!sig || !*sig || !run || !*run)
        return false;

    m_dir     = std::string{run} + "/hypr/" + sig;
    m_eventFd = connectTo(m_dir + "/.socket2.sock", true);
    return m_eventFd >= 0;
}

std::string CHyprIPC::request(std::string_view command) const {
    int fd = connectTo(m_dir + "/.socket.sock", false);
    if (fd < 0)
        return {};

    // never let a stuck compositor freeze the snow loop for long
    timeval tv{.tv_sec = 0, .tv_usec = 500'000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::string reply;
    if (write(fd, command.data(), command.size()) == (ssize_t)command.size()) {
        std::array<char, 8192> buf;
        ssize_t                n;
        while ((n = read(fd, buf.data(), buf.size())) > 0)
            reply.append(buf.data(), n);
    }
    close(fd);
    return reply;
}

bool CHyprIPC::readEvents(bool& closed) {
    // events that can move, show or hide windows
    static constexpr std::string_view RELEVANT[] = {
        "openwindow",      "closewindow",      "movewindow",      "movewindowv2",     "workspace",       "workspacev2",    "focusedmon",
        "focusedmonv2",    "changefloatingmode", "fullscreen",    "activespecial",    "activespecialv2", "moveworkspace",  "moveworkspacev2",
        "monitoradded",    "monitoraddedv2",   "monitorremoved",  "monitorremovedv2", "configreloaded",  "minimized",      "pin",
        "togglegroup",     "moveintogroup",    "moveoutofgroup",
    };

    bool relevant = false;
    char buf[4096];
    for (;;) {
        ssize_t n = read(m_eventFd, buf, sizeof(buf));
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
            closed = true;
            break;
        }
        if (n < 0)
            break;
        m_partial.append(buf, n);
    }

    size_t start = 0;
    for (size_t nl; (nl = m_partial.find('\n', start)) != std::string::npos; start = nl + 1) {
        std::string_view line{m_partial.data() + start, nl - start};
        const auto       name = line.substr(0, line.find(">>"));
        for (auto r : RELEVANT)
            if (name == r)
                relevant = true;
    }
    m_partial.erase(0, start);
    return relevant;
}
