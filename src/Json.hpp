#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Minimal read-only JSON tree, enough for Hyprland's IPC replies.
struct SJson {
    enum class eType {
        NUL,
        BOOL,
        NUMBER,
        STRING,
        ARRAY,
        OBJECT,
    };

    eType                                      type = eType::NUL;
    bool                                       b    = false;
    double                                     num  = 0;
    std::string                                str;
    std::vector<SJson>                         arr;
    std::vector<std::pair<std::string, SJson>> obj;

    // Missing keys / indices return a null value, so lookups can be chained.
    const SJson&       operator[](std::string_view key) const;
    const SJson&       operator[](size_t i) const;

    double             number(double def = 0) const;
    bool               boolean(bool def = false) const;
    const std::string& string() const;

    // Returns a null value and sets *ok = false on malformed input.
    static SJson       parse(std::string_view text, bool* ok = nullptr);
};
