#include "Config.hpp"

#include <hyprlang.hpp>

#include <algorithm>
#include <any>
#include <cstdlib>
#include <sstream>

static std::string defaultPath() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::string{xdg} + "/hypr/hyprflurry.conf";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string{home} + "/.config/hypr/hyprflurry.conf";
    return "hyprflurry.conf";
}

CConfigManager::CConfigManager(std::string path) : m_path(path.empty() ? defaultPath() : std::move(path)) {
    Hyprlang::SConfigOptions opts;
    opts.allowMissingConfig = true;
    opts.throwAllErrors     = true;

    m_config = std::make_unique<Hyprlang::CConfig>(m_path.c_str(), opts);

    const SSnowConfig d;
    m_config->addConfigValue("flakes", Hyprlang::INT{d.flakes});
    m_config->addConfigValue("fps", Hyprlang::INT{d.fps});
    m_config->addConfigValue("speed", Hyprlang::FLOAT{d.speed});
    m_config->addConfigValue("wind", Hyprlang::FLOAT{d.wind});
    m_config->addConfigValue("gust", Hyprlang::FLOAT{d.gust});
    m_config->addConfigValue("sway", Hyprlang::FLOAT{d.sway});
    m_config->addConfigValue("min_size", Hyprlang::FLOAT{d.minSize});
    m_config->addConfigValue("max_size", Hyprlang::FLOAT{d.maxSize});
    m_config->addConfigValue("opacity", Hyprlang::FLOAT{d.opacity});
    m_config->addConfigValue("color", Hyprlang::INT{(Hyprlang::INT)d.color});
    m_config->addConfigValue("style", Hyprlang::STRING{"mixed"});
    m_config->addConfigValue("layer", Hyprlang::STRING{"bottom"});
    m_config->addConfigValue("monitors", Hyprlang::STRING{""});
    m_config->addConfigValue("pile", Hyprlang::INT{d.pile});
    m_config->addConfigValue("pile_windows", Hyprlang::INT{d.pileWindows});
    m_config->addConfigValue("pile_max", Hyprlang::FLOAT{d.pileMax});
    m_config->addConfigValue("pile_window_max", Hyprlang::FLOAT{d.pileWindowMax});
    m_config->addConfigValue("pile_rate", Hyprlang::FLOAT{d.pileRate});
    m_config->addConfigValue("pile_melt", Hyprlang::FLOAT{d.pileMelt});

    m_config->commence();
}

CConfigManager::~CConfigManager() = default;

bool CConfigManager::reload(std::string& err) {
    const auto result = m_config->parse();
    if (result.error && result.getError())
        err = result.getError();

    auto i = [&](const char* k) { return std::any_cast<Hyprlang::INT>(m_config->getConfigValue(k)); };
    auto f = [&](const char* k) { return std::any_cast<Hyprlang::FLOAT>(m_config->getConfigValue(k)); };
    auto s = [&](const char* k) { return std::string{std::any_cast<Hyprlang::STRING>(m_config->getConfigValue(k))}; };

    SSnowConfig c;
    c.flakes  = (int)std::clamp<Hyprlang::INT>(i("flakes"), 0, 20000);
    c.fps     = (int)std::clamp<Hyprlang::INT>(i("fps"), 0, 1000);
    c.speed   = std::clamp(f("speed"), 0.0f, 20.0f);
    c.wind    = std::clamp(f("wind"), -10.0f, 10.0f);
    c.gust    = std::clamp(f("gust"), 0.0f, 10.0f);
    c.sway    = std::clamp(f("sway"), 0.0f, 20.0f);
    c.minSize = std::clamp(f("min_size"), 0.25f, 100.0f);
    c.maxSize = std::clamp(f("max_size"), c.minSize, 200.0f);
    c.opacity = std::clamp(f("opacity"), 0.0f, 1.0f);
    c.color   = (uint32_t)i("color");

    c.pile          = i("pile") != 0;
    c.pileWindows   = i("pile_windows") != 0;
    c.pileMax       = std::clamp(f("pile_max"), 1.0f, 1000.0f);
    c.pileWindowMax = std::clamp(f("pile_window_max"), 1.0f, 500.0f);
    c.pileRate      = std::clamp(f("pile_rate"), 0.0f, 100.0f);
    c.pileMelt      = std::clamp(f("pile_melt"), 0.0f, 100.0f);

    const auto style = s("style");
    if (style == "soft")
        c.style = eFlakeStyle::SOFT;
    else if (style == "crystal")
        c.style = eFlakeStyle::CRYSTAL;
    else if (style == "mixed")
        c.style = eFlakeStyle::MIXED;
    else if (err.empty())
        err = "style: expected soft, crystal or mixed, got \"" + style + "\"";

    const auto layer = s("layer");
    if (layer == "background")
        c.background = true;
    else if (layer != "bottom" && err.empty())
        err = "layer: expected bottom or background, got \"" + layer + "\"";

    std::stringstream ss{s("monitors")};
    for (std::string mon; std::getline(ss, mon, ',');) {
        mon.erase(0, mon.find_first_not_of(" \t"));
        mon.erase(mon.find_last_not_of(" \t") + 1);
        if (!mon.empty())
            c.monitors.push_back(mon);
    }

    m_snow = std::move(c);
    return err.empty();
}
