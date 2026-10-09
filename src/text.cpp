#include "htpd/text.hpp"

#include <cstdint>
#include <format>

namespace htpd::text {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

constexpr bool is_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
constexpr bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool is_ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
constexpr char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Non-ASCII code points that are punctuation or spacing rather than letters.
constexpr bool is_unicode_punct(char32_t cp) {
    return (cp >= 0x0080 && cp <= 0x00BF) ||   // C1 controls, Latin-1 punctuation and symbols
           (cp >= 0x2000 && cp <= 0x206F) ||   // general punctuation, dashes, quotes
           (cp >= 0x3000 && cp <= 0x303F) ||   // CJK punctuation
           cp == 0x00D7 || cp == 0x00F7 || cp == kReplacement;
}

constexpr bool is_control(char32_t cp) {
    return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F);
}

}  // namespace

void append_utf8(std::string& out, char32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

char32_t decode_utf8(std::string_view s, std::size_t& i) {
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char b0 = byte(i);
    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    std::size_t extra = 0;
    char32_t cp = 0;
    char32_t smallest = 0;   // smallest code point that needs this many bytes
    if ((b0 & 0xE0) == 0xC0) { extra = 1; cp = b0 & 0x1F; smallest = 0x80; }
    else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0F; smallest = 0x800; }
    else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07; smallest = 0x10000; }
    else { ++i; return kReplacement; }

    if (i + extra >= s.size()) {   // truncated sequence
        ++i;
        return kReplacement;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
        const unsigned char b = byte(i + k);
        if ((b & 0xC0) != 0x80) { ++i; return kReplacement; }
        cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < smallest || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++i;
        return kReplacement;
    }
    i += extra + 1;
    return cp;
}

std::string utf16le_to_utf8(std::span<const unsigned char> bytes) {
    std::string out;
    const std::size_t units = bytes.size() / 2;
    const auto unit = [&](std::size_t k) {
        return static_cast<char32_t>(bytes[2 * k] | (bytes[2 * k + 1] << 8));
    };
    for (std::size_t k = 0; k < units; ++k) {
        char32_t u = unit(k);
        if (u == 0) break;
        if (u >= 0xD800 && u <= 0xDBFF && k + 1 < units) {
            const char32_t lo = unit(k + 1);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                ++k;
            }
        }
        append_utf8(out, u);
    }
    return out;
}

std::string sanitize_utf8(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const char32_t cp = decode_utf8(s, i);
        if (!is_control(cp)) append_utf8(out, cp);
    }
    return out;
}

std::string to_lower_ascii(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower(c);
    return out;
}

std::string collapse_spaces(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool pending = false;
    for (char c : s) {
        if (is_ascii_space(c)) {
            pending = !out.empty();
        } else {
            if (pending) out.push_back(' ');
            pending = false;
            out.push_back(c);
        }
    }
    return out;
}

std::string skeleton(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const char32_t cp = decode_utf8(s, i);
        if (cp < 0x80) {
            const char c = static_cast<char>(cp);
            if (is_ascii_alpha(c) || is_ascii_digit(c)) out.push_back(lower(c));
        } else if (!is_unicode_punct(cp)) {
            append_utf8(out, cp);
        }
    }
    return out;
}

std::string slugify(std::string_view s, std::size_t max_len) {
    std::string out;
    bool dash = false;
    for (char c : s) {
        if (is_ascii_alpha(c) || is_ascii_digit(c)) {
            if (dash && !out.empty()) out.push_back('-');
            dash = false;
            out.push_back(lower(c));
        } else {
            dash = true;
        }
        if (out.size() >= max_len) break;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.empty()) out = "section";
    return out;
}

std::string yaml_quote(std::string_view s) {
    std::string out = "\"";
    for (std::size_t i = 0; i < s.size();) {
        const char32_t cp = decode_utf8(s, i);
        switch (cp) {
            case U'"':  out += "\\\""; break;
            case U'\\': out += "\\\\"; break;
            case U'\n': out += "\\n"; break;
            case U'\r': out += "\\r"; break;
            case U'\t': out += "\\t"; break;
            default:
                if (is_control(cp)) {
                    out += std::format("\\x{:02X}", static_cast<unsigned>(cp));
                } else if (cp == 0x2028 || cp == 0x2029 || cp == 0xFEFF) {
                    // Line/paragraph separators and the byte-order mark must not
                    // appear raw inside a YAML scalar.
                    out += std::format("\\u{:04X}", static_cast<unsigned>(cp));
                } else {
                    append_utf8(out, cp);
                }
        }
    }
    out.push_back('"');
    return out;
}

std::string md_link_text(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '[' || c == ']' || c == '\\') out.push_back('\\');
        out.push_back(c == '\n' ? ' ' : c);
    }
    return out;
}

bool is_all_digits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!is_ascii_digit(c)) return false;
    }
    return true;
}

std::size_t word_count(std::string_view s) {
    std::size_t n = 0;
    bool in_word = false;
    for (char c : s) {
        if (is_ascii_space(c)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++n;
        }
    }
    return n;
}

std::size_t letter_count(std::string_view s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size();) {
        const char32_t cp = decode_utf8(s, i);
        if (cp < 0x80 ? is_ascii_alpha(static_cast<char>(cp)) : !is_unicode_punct(cp)) ++n;
    }
    return n;
}

bool ends_with_sentence_punct(std::string_view s) {
    while (!s.empty() && is_ascii_space(s.back())) s.remove_suffix(1);
    if (s.empty()) return false;
    const char c = s.back();
    return c == '.' || c == ':' || c == ';' || c == '!' || c == '?';
}

}  // namespace htpd::text
