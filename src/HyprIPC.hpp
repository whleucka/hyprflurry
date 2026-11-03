#pragma once

#include <string>
#include <string_view>

// Talks to Hyprland: request/reply on .socket.sock, event stream on .socket2.sock.
class CHyprIPC {
  public:
    ~CHyprIPC();

    // false when not running under Hyprland
    bool        init();

    // Blocking request (Hyprland replies within a few ms). Empty string on failure.
    std::string request(std::string_view command) const;

    // Event stream fd for poll(), -1 if unavailable.
    int         eventFd() const {
        return m_eventFd;
    }
    // Drains pending events; true if any of them can change window layout.
    // Sets `closed` if the event socket died.
    bool        readEvents(bool& closed);

  private:
    std::string m_dir;
    int         m_eventFd = -1;
    std::string m_partial; // incomplete line from the last read
};
