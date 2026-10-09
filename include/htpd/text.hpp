// HTPD - small text helpers. All strings are UTF-8.
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace htpd::text {

// Append one code point as UTF-8. Invalid code points become U+FFFD.
void append_utf8(std::string& out, char32_t cp);

// Decode the code point starting at s[i] and advance i. Invalid bytes yield
// U+FFFD and advance by one byte, so the loop always terminates.
char32_t decode_utf8(std::string_view s, std::size_t& i);

// UTF-16LE bytes (as PDFium returns them) -> UTF-8. Stops at a NUL unit.
std::string utf16le_to_utf8(std::span<const unsigned char> bytes);

// Valid UTF-8 without control characters (they become U+FFFD or are dropped).
// Used for text that does not come from the PDF text layer, e.g. file names.
std::string sanitize_utf8(std::string_view s);

std::string to_lower_ascii(std::string_view s);

// Trim and collapse every run of whitespace to one space.
std::string collapse_spaces(std::string_view s);

// "5.2.1  Clock-Tree (PLL)" -> "521clocktreepll". Used to compare a bookmark
// title with page text while ignoring spacing, case and punctuation.
std::string skeleton(std::string_view s);

// File-name safe: lower-case ASCII letters, digits and '-'. Never empty.
std::string slugify(std::string_view s, std::size_t max_len = 60);

// Double-quoted YAML scalar, safe for any input.
std::string yaml_quote(std::string_view s);

// Escape the characters that would break "[text](url)".
std::string md_link_text(std::string_view s);

bool is_all_digits(std::string_view s);
std::size_t word_count(std::string_view s);
std::size_t letter_count(std::string_view s);   // ASCII letters + all non-ASCII letters
bool ends_with_sentence_punct(std::string_view s);

}  // namespace htpd::text
