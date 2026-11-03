#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Hyprlang {
    class CConfig;
}

enum class eFlakeStyle {
    SOFT,    // soft round dots
    CRYSTAL, // six-armed crystals (small flakes fall back to dots)
    MIXED,   // mostly dots, some crystals up close
};

struct SSnowConfig {
    int                      flakes    = 300;   // per 1920x1080 logical pixels
    int                      fps       = 60;    // 0 = uncapped (follow refresh rate)
    float                    speed     = 1.0f;  // fall speed multiplier
    float                    wind      = 0.15f; // horizontal drift, 1.0 = 45 degrees
    float                    gust      = 0.25f; // wind variation strength
    float                    sway      = 1.0f;  // side-to-side wobble multiplier
    float                    minSize   = 1.0f;  // flake radius in logical px (far)
    float                    maxSize   = 4.5f;  // flake radius in logical px (near)
    float                    opacity   = 0.9f;
    uint32_t                 color     = 0xFFFFFFFF; // 0xAARRGGBB
    eFlakeStyle              style     = eFlakeStyle::MIXED;
    bool                     background = false; // true = background layer, false = bottom layer
    std::vector<std::string> monitors;           // empty = all

    bool                     pile          = true;  // snow piles up at the bottom of the screen
    bool                     pileWindows   = true;  // ... and on top of windows (Hyprland only)
    float                    pileMax       = 40.0f; // max ground pile height, logical px
    float                    pileWindowMax = 12.0f; // max window pile height, logical px
    float                    pileRate      = 1.0f;  // how fast piles grow
    float                    pileMelt      = 0.05f; // melt speed, logical px/s
};

class CConfigManager {
  public:
    explicit CConfigManager(std::string path);
    ~CConfigManager();

    // (Re)parses the config file. Returns false and fills `err` on error;
    // valid values are still applied.
    bool               reload(std::string& err);

    const SSnowConfig& get() const {
        return m_snow;
    }
    const std::string& path() const {
        return m_path;
    }

  private:
    std::string                       m_path;
    std::unique_ptr<Hyprlang::CConfig> m_config;
    SSnowConfig                       m_snow;
};
