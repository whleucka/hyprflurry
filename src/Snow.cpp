#include "Snow.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

// Fall speed of the nearest flakes at speed = 1, in logical px/s.
constexpr float BASE_FALL = 90.0f;
// Only flakes nearer than this can land; farther ones are "behind" the scene.
constexpr float LAND_DEPTH = 0.3f;
// Fraction of a flake's disc area it adds to a pile (snow packs down).
constexpr float PACKING = 0.35f;
// Steepest slope a pile keeps before snow slides (height per column width).
constexpr float REPOSE = 0.7f;
// Pile column width in logical px.
constexpr float COLUMN = 2.0f;
// Most flakes released when a window's pile falls off.
constexpr size_t MAX_SHAKE = 160;

CSnow::CSnow() : m_rng(std::random_device{}()) {}

float CSnow::rand01() {
    return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng);
}

void CSnow::reset(const SSnowConfig& cfg, uint32_t w, uint32_t h, float scale) {
    const bool sameSize = m_w == (float)w && m_h == (float)h && m_scale == scale;

    m_cfg   = cfg;
    m_w     = (float)w;
    m_h     = (float)h;
    m_scale = scale;
    m_colW  = COLUMN * scale;

    if (!cfg.pile) {
        m_ground.h.clear();
        m_piles.clear();
        m_active.clear();
    } else {
        if (!sameSize || m_ground.h.empty())
            initPile(m_ground, 0, m_h, m_w, cfg.pileMax * scale, true);
        m_ground.maxH = cfg.pileMax * scale;
        if (!sameSize || !cfg.pileWindows) {
            m_piles.clear();
            m_active.clear();
        }
        for (auto& [id, pile] : m_piles)
            pile.maxH = cfg.pileWindowMax * scale;
    }

    // density is defined per 1080p of logical area
    const float  logicalArea = (m_w / scale) * (m_h / scale);
    const size_t count       = (size_t)std::lround(cfg.flakes * logicalArea / (1920.0f * 1080.0f));

    if (!sameSize) {
        m_flakes.resize(count);
        for (auto& f : m_flakes) {
            // more far flakes than near ones
            f.z = std::pow(rand01(), 1.7f);
            spawn(f, true);
        }
    } else
        retarget(count);

    // draw far flakes first so near ones blend on top; respawns keep z, so this stays sorted
    std::sort(m_flakes.begin(), m_flakes.end(), [](const SFlake& a, const SFlake& b) { return a.z < b.z; });
    update(0.0);
}

void CSnow::retarget(size_t count) {
    // flakes already on screen keep their position and pick up the new look and speed
    size_t live = 0;
    for (auto& f : m_flakes) {
        if (f.transient)
            continue;
        applyStyle(f);
        ++live;
    }

    if (live < count) {
        // new flakes start spread out above the screen, so the snow thickens from the top
        for (size_t i = live; i < count; ++i) {
            SFlake f;
            f.z = std::pow(rand01(), 1.7f);
            spawn(f, false);
            f.y = -f.radius - rand01() * m_h;
            m_flakes.push_back(f);
        }
    } else if (live > count) {
        // surplus flakes finish their fall and aren't respawned
        std::vector<SFlake*> pool;
        pool.reserve(live);
        for (auto& f : m_flakes)
            if (!f.transient)
                pool.push_back(&f);
        std::shuffle(pool.begin(), pool.end(), m_rng);
        for (size_t i = 0; i < live - count; ++i)
            pool[i]->transient = true;
    }
}

void CSnow::applyStyle(SFlake& f) {
    const float z     = f.z;
    const float depth = 0.35f + 0.65f * z; // speed/wind factor: far flakes move slower

    f.radius    = (m_cfg.minSize + (m_cfg.maxSize - m_cfg.minSize) * std::pow(z, 1.5f)) * (0.85f + 0.3f * f.rSize) * m_scale;
    f.alpha     = m_cfg.opacity * (0.35f + 0.65f * z) * (0.8f + 0.2f * f.rAlpha);
    f.fallSpeed = BASE_FALL * m_cfg.speed * depth * (0.85f + 0.3f * f.rSpeed) * m_scale;
    f.swayAmp   = m_cfg.sway * (3.0f + 11.0f * z) * (0.5f + f.rSway) * m_scale;

    // crystals need enough pixels to read as a shape
    const bool bigEnough = f.radius >= 3.0f * m_scale;
    switch (m_cfg.style) {
        case eFlakeStyle::SOFT: f.crystal = false; break;
        case eFlakeStyle::CRYSTAL: f.crystal = bigEnough; break;
        case eFlakeStyle::MIXED: f.crystal = bigEnough && z > 0.6f && f.rCrystal < 0.5f; break;
    }
}

void CSnow::spawn(SFlake& f, bool anywhere) {
    f.rSize     = rand01();
    f.rAlpha    = rand01();
    f.rSpeed    = rand01();
    f.rSway     = rand01();
    f.rCrystal  = rand01();
    f.swayFreq  = 0.4f + 0.9f * rand01();
    f.swayPhase = rand01() * 2 * std::numbers::pi_v<float>;
    f.rotation  = rand01() * 2 * std::numbers::pi_v<float>;
    f.rotSpeed  = (rand01() - 0.5f) * 1.2f;
    f.behind    = false;
    f.transient = false;
    f.dead      = false;
    applyStyle(f);

    f.x     = rand01() * m_w;
    f.y     = anywhere ? rand01() * (m_h + 2 * f.radius) - f.radius : -f.radius - rand01() * 30.0f * m_scale;
    f.drawX = f.x;
}

// ---- piles ------------------------------------------------------------------

void CSnow::initPile(SPile& pile, float x, float y, float w, float maxH, bool wallEnds) {
    pile.x        = x;
    pile.y        = y;
    pile.w        = std::max(w, m_colW);
    pile.maxH     = maxH;
    pile.peak     = 0;
    pile.wallEnds = wallEnds;
    pile.h.assign((size_t)std::ceil(pile.w / m_colW) + 1, 0.0f);
}

float CSnow::heightAt(const SPile& pile, float x) const {
    const long col = std::lround((x - pile.x) / m_colW);
    return pile.h[(size_t)std::clamp<long>(col, 0, (long)pile.h.size() - 1)];
}

void CSnow::deposit(SPile& pile, float x, float radius) {
    const float area   = std::numbers::pi_v<float> * radius * radius * PACKING * m_cfg.pileRate;
    const long  center = std::lround((x - pile.x) / m_colW);
    const long  half   = std::max(0L, std::lround(radius / m_colW));
    const long  first = std::max(0L, center - half), last = std::min((long)pile.h.size() - 1, center + half);
    if (first > last)
        return;

    const float add = area / ((float)(last - first + 1) * m_colW);
    for (long c = first; c <= last; ++c)
        pile.h[c] = std::min(pile.h[c] + add, pile.maxH);
}

void CSnow::settle(SPile& pile, float dt) {
    auto&        h    = pile.h;
    const size_t n    = h.size();
    const float  slip = REPOSE * m_colW;

    // let steep slopes slide, sweeping both directions to avoid a bias
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t k = 0; k + 1 < n; ++k) {
            const size_t i = pass == 0 ? k : n - 2 - k;
            const float  d = h[i] - h[i + 1];
            if (std::abs(d) <= slip)
                continue;
            const float move = (std::abs(d) - slip) * 0.5f * (d > 0 ? 1.0f : -1.0f);
            h[i] -= move;
            h[i + 1] += move;
        }
    }

    // on windows, snow slides off the edges
    if (!pile.wallEnds) {
        h[0]     = std::min(h[0], slip * 0.5f);
        h[n - 1] = std::min(h[n - 1], slip * 0.5f);
    }

    const float melt = m_cfg.pileMelt * m_scale * dt;
    pile.peak        = 0;
    for (auto& v : h) {
        v         = std::clamp(v - melt, 0.0f, pile.maxH);
        pile.peak = std::max(pile.peak, v);
    }
}

void CSnow::shakeOff(const SPile& pile) {
    float volume = 0;
    for (float v : pile.h)
        volume += v * m_colW;

    const float r     = (m_cfg.minSize + (m_cfg.maxSize - m_cfg.minSize) * 0.5f) * m_scale;
    const auto  count = std::min(MAX_SHAKE, (size_t)(volume / (std::numbers::pi_v<float> * r * r * PACKING * m_cfg.pileRate)));

    for (size_t i = 0; i < count; ++i) {
        // pick a column weighted by its height
        size_t col = 0;
        for (int tries = 0; tries < 8; ++tries) {
            col = std::min(pile.h.size() - 1, (size_t)(rand01() * pile.h.size()));
            if (rand01() * pile.peak <= pile.h[col])
                break;
        }

        SFlake f;
        f.z = 0.45f + 0.55f * rand01();
        spawn(f, false);
        f.transient = true;
        f.x = f.drawX = pile.x + col * m_colW;
        f.y           = pile.y - pile.h[col] * rand01();
        f.fallSpeed *= 0.6f + 0.6f * rand01();
        m_flakes.push_back(f);
    }
}

void CSnow::setPlatforms(std::vector<SPlatform> platforms, int64_t layoutKey) {
    if (!m_cfg.pile || !m_cfg.pileWindows) {
        m_active.clear();
        return;
    }

    const bool sameLayout = layoutKey == m_layoutKey;
    m_layoutKey           = layoutKey;

    for (auto& [id, pile] : m_piles)
        pile.visible = false;

    m_active.clear();
    for (const auto& p : platforms) {
        const float px = p.x + p.inset, pw = p.w - 2 * p.inset;
        if (pw <= m_colW * 2)
            continue;

        auto [it, inserted] = m_piles.try_emplace(p.id);
        SPile& pile         = it->second;
        if (inserted)
            initPile(pile, px, p.y, pw, m_cfg.pileWindowMax * m_scale, false);
        else if (std::abs(pile.x - px) > 0.5f || std::abs(pile.y - p.y) > 0.5f || std::abs(pile.w - pw) > 0.5f) {
            // the window moved or resized: its snow falls off
            shakeOff(pile);
            initPile(pile, px, p.y, pw, m_cfg.pileWindowMax * m_scale, false);
        }
        pile.visible = true;
        m_active.push_back({p, &pile});
    }

    // windows that vanished without a workspace switch were closed or moved away
    for (auto it = m_piles.begin(); it != m_piles.end();) {
        if (!it->second.visible && (sameLayout || m_piles.size() > 128)) {
            if (sameLayout)
                shakeOff(it->second);
            it = m_piles.erase(it);
        } else
            ++it;
    }
    // erase may not move nodes, but refresh the pointers anyway for safety
    for (auto& a : m_active)
        a.pile = &m_piles.at(a.p.id);
}

void CSnow::appendMesh(const SPile& pile) {
    if (pile.peak < 0.05f)
        return;

    const int first = (int)m_pileVerts.size();
    for (size_t i = 0; i < pile.h.size(); ++i) {
        const float x = std::min(pile.x + (float)i * m_colW, pile.x + pile.w);
        m_pileVerts.push_back({x, pile.y - pile.h[i], 0.0f, 0.0f});
        m_pileVerts.push_back({x, pile.y, pile.h[i], 0.0f});
    }
    m_pileStrips.push_back({first, (int)m_pileVerts.size() - first});
}

// ---- simulation -------------------------------------------------------------

void CSnow::update(double dt) {
    m_time += dt;
    const float t = (float)std::fmod(m_time, 3600.0 * 24.0);

    // smoothly varying wind: base + two slow, unrelated sine waves
    const float gust  = m_cfg.gust * (0.6f * std::sin(t * 0.21f) + 0.4f * std::sin(t * 0.53f + 1.3f));
    const float windV = (m_cfg.wind + gust) * BASE_FALL * m_cfg.speed * m_scale;

    const float margin  = 40.0f * m_scale;
    const float fdt     = (float)dt;
    const bool  ground  = m_cfg.pile && !m_ground.h.empty();
    bool        anyDead = false;

    for (auto& f : m_flakes) {
        const float depth      = 0.35f + 0.65f * f.z;
        const float prevBottom = f.y + f.radius * 0.4f;

        f.y += f.fallSpeed * fdt;
        f.x += windV * depth * fdt;
        f.rotation += f.rotSpeed * fdt;
        f.drawX = f.x + std::sin(t * f.swayFreq + f.swayPhase) * f.swayAmp;

        const float bottom = f.y + f.radius * 0.4f;
        const float x      = f.drawX;
        bool        landed = false;

        if (m_cfg.pile && f.z > LAND_DEPTH) {
            if (!f.behind) {
                for (auto& a : m_active) {
                    const auto& pile = *a.pile;
                    if (x < pile.x || x > pile.x + pile.w)
                        continue;
                    const float surface = pile.y - heightAt(pile, x);
                    if (prevBottom <= surface && bottom > surface) {
                        deposit(*a.pile, x, f.radius);
                        landed = true;
                        break;
                    }
                }
                // once a flake falls behind a window it stays "behind" the scene
                for (size_t i = 0; !landed && i < m_active.size(); ++i) {
                    const auto& p = m_active[i].p;
                    if (x >= p.x && x <= p.x + p.w && f.y > p.y && f.y < p.y + p.h)
                        f.behind = true;
                }
            }
            if (!landed && ground && bottom > m_h - heightAt(m_ground, x)) {
                deposit(m_ground, x, f.radius);
                landed = true;
            }
        }

        if (landed || f.y - f.radius > m_h) {
            if (f.transient) {
                f.dead  = true;
                anyDead = true;
            } else
                spawn(f, false);
            continue;
        }
        if (f.y + f.radius < -margin * 4) // strong updraft config edge case
            f.y = m_h + f.radius;

        const float span = m_w + 2 * margin;
        if (f.x < -margin)
            f.x += span;
        else if (f.x > m_w + margin)
            f.x -= span;
    }

    if (anyDead)
        std::erase_if(m_flakes, [](const SFlake& f) { return f.dead; });

    m_instances.resize(m_flakes.size());
    for (size_t i = 0; i < m_flakes.size(); ++i) {
        const auto& f = m_flakes[i];
        m_instances[i] = {f.drawX, f.y, f.radius, f.alpha, f.rotation, f.crystal ? 1.0f : 0.0f};
    }

    m_pileVerts.clear();
    m_pileStrips.clear();
    if (ground) {
        settle(m_ground, fdt);
        appendMesh(m_ground);
    }
    for (auto& a : m_active) {
        settle(*a.pile, fdt);
        appendMesh(*a.pile);
    }
}
