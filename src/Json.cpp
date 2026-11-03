#include "Json.hpp"

#include <charconv>

static const SJson NULL_JSON;

const SJson& SJson::operator[](std::string_view key) const {
    for (const auto& [k, v] : obj)
        if (k == key)
            return v;
    return NULL_JSON;
}

const SJson& SJson::operator[](size_t i) const {
    return i < arr.size() ? arr[i] : NULL_JSON;
}

double SJson::number(double def) const {
    if (type == eType::NUMBER)
        return num;
    if (type == eType::BOOL)
        return b ? 1 : 0;
    return def;
}

bool SJson::boolean(bool def) const {
    if (type == eType::BOOL)
        return b;
    if (type == eType::NUMBER)
        return num != 0;
    return def;
}

const std::string& SJson::string() const {
    return str;
}

namespace {
    struct SParser {
        std::string_view s;
        size_t           i  = 0;
        bool             ok = true;

        void             ws() {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
                ++i;
        }

        bool eat(char c) {
            ws();
            if (i < s.size() && s[i] == c) {
                ++i;
                return true;
            }
            return false;
        }

        bool literal(std::string_view lit) {
            if (s.substr(i, lit.size()) == lit) {
                i += lit.size();
                return true;
            }
            ok = false;
            return false;
        }

        static void utf8(std::string& out, unsigned cp) {
            if (cp < 0x80)
                out += (char)cp;
            else if (cp < 0x800) {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            } else {
                out += (char)(0xF0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3F));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
        }

        unsigned hex4() {
            unsigned v = 0;
            if (i + 4 > s.size() || std::from_chars(s.data() + i, s.data() + i + 4, v, 16).ptr != s.data() + i + 4) {
                ok = false;
                return 0;
            }
            i += 4;
            return v;
        }

        std::string string() {
            std::string out;
            ++i; // opening quote
            while (ok && i < s.size() && s[i] != '"') {
                char c = s[i++];
                if (c != '\\') {
                    out += c;
                    continue;
                }
                if (i >= s.size())
                    break;
                switch (char e = s[i++]) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        unsigned cp = hex4();
                        if (cp >= 0xD800 && cp < 0xDC00 && s.substr(i, 2) == "\\u") {
                            i += 2;
                            unsigned lo = hex4();
                            cp          = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(out, cp);
                        break;
                    }
                    default: out += e; break; // \" \\ \/
                }
            }
            if (i >= s.size())
                ok = false;
            else
                ++i; // closing quote
            return out;
        }

        SJson value(int depth) {
            SJson v;
            ws();
            if (i >= s.size() || depth > 64) {
                ok = false;
                return v;
            }

            switch (s[i]) {
                case '{':
                    ++i;
                    v.type = SJson::eType::OBJECT;
                    if (eat('}'))
                        return v;
                    do {
                        ws();
                        if (i >= s.size() || s[i] != '"') {
                            ok = false;
                            return v;
                        }
                        std::string key = string();
                        if (!eat(':')) {
                            ok = false;
                            return v;
                        }
                        v.obj.emplace_back(std::move(key), value(depth + 1));
                    } while (ok && eat(','));
                    if (!eat('}'))
                        ok = false;
                    return v;
                case '[':
                    ++i;
                    v.type = SJson::eType::ARRAY;
                    if (eat(']'))
                        return v;
                    do {
                        v.arr.push_back(value(depth + 1));
                    } while (ok && eat(','));
                    if (!eat(']'))
                        ok = false;
                    return v;
                case '"':
                    v.type = SJson::eType::STRING;
                    v.str  = string();
                    return v;
                case 't':
                    v.type = SJson::eType::BOOL;
                    v.b    = literal("true");
                    return v;
                case 'f':
                    v.type = SJson::eType::BOOL;
                    literal("false");
                    return v;
                case 'n': literal("null"); return v;
                default: {
                    v.type   = SJson::eType::NUMBER;
                    auto res = std::from_chars(s.data() + i, s.data() + s.size(), v.num);
                    if (res.ec != std::errc{})
                        ok = false;
                    i = res.ptr - s.data();
                    return v;
                }
            }
        }
    };
}

SJson SJson::parse(std::string_view text, bool* ok) {
    SParser p{text};
    SJson   v = p.value(0);
    p.ws();
    if (p.i != text.size())
        p.ok = false;
    if (ok)
        *ok = p.ok;
    return p.ok ? v : SJson{};
}
