// MiniJson.h — intentionally tiny JSON helpers for our small, controlled
// control-channel messages (a handful of flat key/value pairs). Not a general
// JSON parser; it just extracts a value by key and escapes strings we emit.
// Avoids pulling a JSON library into the cross-compiled build.
#pragma once

#include <cstdlib>
#include <string>

namespace sec {
namespace mjson {

// Escape a string for safe inclusion inside JSON double quotes.
inline std::string escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) o += ' ';
                else o += c;
        }
    }
    return o;
}

// Find the position just after `"key"` and its colon. Returns npos if absent.
inline std::size_t valuePos(const std::string& j, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t k = j.find(needle);
    if (k == std::string::npos) return std::string::npos;
    std::size_t c = j.find(':', k + needle.size());
    if (c == std::string::npos) return std::string::npos;
    ++c;
    while (c < j.size() && (j[c] == ' ' || j[c] == '\t')) ++c;
    return c;
}

inline bool getNumber(const std::string& j, const std::string& key, double& out) {
    std::size_t p = valuePos(j, key);
    if (p == std::string::npos) return false;
    char* end = nullptr;
    double v = std::strtod(j.c_str() + p, &end);
    if (end == j.c_str() + p) return false;
    out = v;
    return true;
}

inline bool getBool(const std::string& j, const std::string& key, bool& out) {
    std::size_t p = valuePos(j, key);
    if (p == std::string::npos) return false;
    if (j.compare(p, 4, "true") == 0)  { out = true;  return true; }
    if (j.compare(p, 5, "false") == 0) { out = false; return true; }
    return false;
}

inline bool getString(const std::string& j, const std::string& key, std::string& out) {
    std::size_t p = valuePos(j, key);
    if (p == std::string::npos || p >= j.size() || j[p] != '"') return false;
    ++p;
    std::string s;
    while (p < j.size() && j[p] != '"') {
        if (j[p] == '\\' && p + 1 < j.size()) {
            char n = j[p + 1];
            switch (n) {
                case 'n': s += '\n'; break;
                case 'r': s += '\r'; break;
                case 't': s += '\t'; break;
                default:  s += n;    break;
            }
            p += 2;
        } else {
            s += j[p++];
        }
    }
    if (p >= j.size()) return false;  // unterminated
    out = std::move(s);
    return true;
}

} // namespace mjson
} // namespace sec
