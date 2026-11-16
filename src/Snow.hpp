#pragma once

#include "Config.hpp"

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

// Per-instance data uploaded to the GPU. Layout must match Renderer's attributes.
struct SFlakeInstance {
    float x, y;   // center, buffer pixels (top-left origin)
    float radius; // buffer pixels
    float alpha;
    float rotation;
    float shape; // 0 = dot, 1 = crystal
};

// Snow pile mesh: triangle strips, two vertices (top, bottom) per column.
struct SPileVertex {
    float x, y;
    float depth; // pixels below the pile's surface
    float pad;
};
struct SPileStrip {
    int first, count;
};

// A visible window snow can land on, in buffer pixels (border included).
struct SPlatform {
    uint64_t id;
    float    x, y, w, h;
    float    inset; // keep the pile off rounded corners
};

// Simulates the flakes and snow piles for one output.
class CSnow {
  public:
    CSnow();

    // Applies a config for a buffer of w x h pixels at the given scale. A new size
    // respawns all flakes; the same size restyles the flakes in place and adds or
    // retires flakes gradually, and keeps the piles.
    void                               reset(const SSnowConfig& cfg, uint32_t w, uint32_t h, float scale);

    // Visible windows. `layoutKey` identifies the visible workspace(s): while it stays
    // the same, a window that disappears was closed or moved, so its snow falls off.
    void                               setPlatforms(std::vector<SPlatform> platforms, int64_t layoutKey);

    void                               update(double dt);

    const std::vector<SFlakeInstance>& instances() const {
        return m_instances;
    }
    const std::vector<SPileVertex>& pileVertices() const {
        return m_pileVerts;
    }
    const std::vector<SPileStrip>& pileStrips() const {
        return m_pileStrips;
    }

  private:
    struct SFlake {
        float x, y, z; // z: depth, 0 = far, 1 = near
        float drawX;   // x including sway
        float radius, alpha, fallSpeed;
        float swayAmp, swayFreq, swayPhase;
        float rotation, rotSpeed;
        float rSize, rAlpha, rSpeed, rSway, rCrystal; // per-flake randomness, kept across restyles
        bool  crystal;
        bool  behind;    // has passed behind a window, can't land on windows
        bool  transient; // shaken-off or surplus snow: removed instead of respawned
        bool  dead;
    };

    struct SPile {
        float              x = 0, y = 0, w = 0; // y = base line
        std::vector<float> h;                   // height per column
        float              maxH     = 0;
        float              peak     = 0; // tallest column, for skipping empty piles
        bool               wallEnds = false;
        bool               visible  = false;
    };

    struct SActive {
        SPlatform p;
        SPile*    pile;
    };

    void                        spawn(SFlake& f, bool anywhere);
    void                        applyStyle(SFlake& f);
    void                        retarget(size_t count);
    float                       rand01();

    void                        initPile(SPile& pile, float x, float y, float w, float maxH, bool wallEnds);
    float                       heightAt(const SPile& pile, float x) const;
    void                        deposit(SPile& pile, float x, float radius);
    void                        settle(SPile& pile, float dt);
    void                        shakeOff(const SPile& pile);
    void                        appendMesh(const SPile& pile);

    SSnowConfig                 m_cfg;
    float                       m_w = 0, m_h = 0, m_scale = 1;
    float                       m_colW = 2;
    double                      m_time = 0;
    std::vector<SFlake>         m_flakes;
    std::vector<SFlakeInstance> m_instances;
    std::minstd_rand            m_rng;

    SPile                       m_ground;
    std::unordered_map<uint64_t, SPile> m_piles;
    std::vector<SActive>        m_active;
    int64_t                     m_layoutKey = 0;

    std::vector<SPileVertex>    m_pileVerts;
    std::vector<SPileStrip>     m_pileStrips;
};
