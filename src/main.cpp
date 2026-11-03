#include "App.hpp"

#include <print>
#include <string>
#include <string_view>

static void usage() {
    std::println("usage: hyprflurry [-c <config>]\n\n"
                 "  -c, --config <path>  config file (default: ~/.config/hypr/hyprflurry.conf)\n"
                 "  -h, --help           show this help\n"
                 "  -v, --version        show the version\n\n"
                 "signals: SIGUSR1 toggles pause, SIGHUP reloads the config");
}

int main(int argc, char** argv) {
    std::string configPath;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc)
            configPath = argv[++i];
        else if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::println("hyprflurry 0.1.0");
            return 0;
        } else {
            usage();
            return 1;
        }
    }

    CApp app{configPath};
    return app.run();
}
