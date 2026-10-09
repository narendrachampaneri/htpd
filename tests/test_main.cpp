// HTPD tests. No external framework: a few macros and a list of functions.
//
//   make test            (or: ctest, when built with CMake)
//   build/htpd_tests layout     runs only the tests whose name contains "layout"
//
// Unit tests build their input in memory. The end-to-end tests at the bottom
// convert the synthetic PDFs in tests/data with the real PDF library.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "htpd/bundle_writer.hpp"
#include "htpd/converter.hpp"
#include "htpd/furniture.hpp"
#include "htpd/headings.hpp"
#include "htpd/layout.hpp"
#include "htpd/pdf_reader.hpp"
#include "htpd/sections.hpp"
#include "htpd/sha256.hpp"
#include "htpd/text.hpp"

#ifndef HTPD_TEST_DATA
#define HTPD_TEST_DATA "tests/data"
#endif

namespace fs = std::filesystem;
using namespace htpd;

// ---- harness --------------------------------------------------------------

namespace {

struct TestCase {
    const char* name;
    void (*run)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

int g_failures = 0;

struct Registrar {
    Registrar(const char* name, void (*run)()) { registry().push_back({name, run}); }
};

template <class A, class B>
void check_equal(const A& a, const B& b, const char* expr, const char* file, int line) {
    if (a == b) return;
    std::ostringstream os;
    os << file << ':' << line << ": " << expr << "\n    left:  " << a << "\n    right: " << b << '\n';
    std::fputs(os.str().c_str(), stderr);
    ++g_failures;
}

#define TEST(name)                                   \
    void name();                                     \
    const Registrar registrar_##name(#name, name);   \
    void name()

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                    \
        }                                                                    \
    } while (false)

#define CHECK_EQ(a, b) check_equal((a), (b), #a " == " #b, __FILE__, __LINE__)

// Like CHECK, but leaves the test when it fails (use before indexing).
#define REQUIRE(cond)                                                          \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "%s:%d: REQUIRE(%s)\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                      \
            return;                                                            \
        }                                                                      \
    } while (false)

// ---- helpers --------------------------------------------------------------

// Puts `s` on a page as glyphs: each character 0.5 em wide, 0.55 em apart.
void put_text(PageText& page, std::string_view s, float x, float baseline, float size = 10,
              bool bold = false) {
    for (std::size_t i = 0; i < s.size();) {
        const char32_t cp = text::decode_utf8(s, i);
        Glyph g;
        if (cp == U' ') {
            g.cp = U' ';
            g.is_space = true;
        } else {
            g.cp = cp;
            g.origin_x = x;
            g.x0 = x;
            g.x1 = x + 0.5f * size;
            g.y0 = baseline - 0.7f * size;
            g.y1 = baseline + 0.2f * size;
            g.baseline = baseline;
            g.size = size;
            g.bold = bold;
        }
        page.glyphs.push_back(g);
        x += 0.55f * size;
    }
}

Line make_line(int page, std::string text, float baseline, float size = 10, bool bold = false,
               float x0 = 72) {
    Line line;
    line.page = page;
    line.text = std::move(text);
    line.baseline = baseline;
    line.size = size;
    line.bold = bold;
    line.x0 = x0;
    line.origin_x = x0;
    line.x1 = x0 + 0.55f * size * static_cast<float>(line.text.size());
    line.y0 = baseline - 0.7f * size;
    line.y1 = baseline + 0.2f * size;
    return line;
}

std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

fs::path fresh_temp_dir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "htpd_tests" / name;
    fs::remove_all(dir);
    return dir;
}

std::vector<std::string> titles(const std::vector<Heading>& headings) {
    std::vector<std::string> out;
    for (const Heading& h : headings) out.push_back(h.title);
    return out;
}

// Every "](target.md)" link in `markdown` must point at an existing file.
std::size_t count_broken_links(const fs::path& file, const std::string& markdown) {
    std::size_t broken = 0;
    for (std::size_t pos = 0; (pos = markdown.find("](", pos)) != std::string::npos;) {
        pos += 2;
        const std::size_t end = markdown.find(')', pos);
        if (end == std::string::npos) break;
        const std::string target = markdown.substr(pos, end - pos);
        if (target.ends_with(".md") && !fs::exists(file.parent_path() / target)) ++broken;
    }
    return broken;
}

// ---- text -------------------------------------------------------------------

TEST(utf8_round_trip) {
    std::string s;
    for (char32_t cp : {U'A', char32_t{0xB0}, char32_t{0x20AC}, char32_t{0x1F600}}) text::append_utf8(s, cp);
    CHECK_EQ(s, std::string("A\xC2\xB0\xE2\x82\xAC\xF0\x9F\x98\x80"));
    std::size_t i = 0;
    CHECK(text::decode_utf8(s, i) == U'A');
    CHECK(text::decode_utf8(s, i) == 0xB0);
    CHECK(text::decode_utf8(s, i) == 0x20AC);
    CHECK(text::decode_utf8(s, i) == 0x1F600);
    CHECK_EQ(i, s.size());

    const std::string broken = "a\xE2\x82";   // truncated sequence must not run past the end
    std::size_t j = 1;
    CHECK(text::decode_utf8(broken, j) == 0xFFFD);
    CHECK_EQ(j, std::size_t{2});

    const std::string overlong = "\xC0\xAF";  // "/" encoded in two bytes is not valid UTF-8
    std::size_t k = 0;
    CHECK(text::decode_utf8(overlong, k) == 0xFFFD);
}

TEST(utf16_to_utf8) {
    // "5 °C" + U+1F600 as a surrogate pair, then the terminator
    const unsigned char bytes[] = {'5', 0, ' ', 0, 0xB0, 0, 'C', 0, 0x3D, 0xD8, 0x00, 0xDE, 0, 0, 'x', 0};
    CHECK_EQ(text::utf16le_to_utf8(bytes), std::string("5 \xC2\xB0" "C\xF0\x9F\x98\x80"));
}

TEST(text_normalisation) {
    CHECK_EQ(text::collapse_spaces("  a \t b\n\nc  "), std::string("a b c"));
    CHECK_EQ(text::skeleton("5.2.1  Clock-Tree (PLL)"), std::string("521clocktreepll"));
    CHECK_EQ(text::skeleton("Limits \xE2\x80\x93 85 \xC2\xB0" "C"), std::string("limits85c"));   // dash, degree sign
    CHECK_EQ(text::slugify("2.1.1 Limits & Ratings!"), std::string("2-1-1-limits-ratings"));
    CHECK_EQ(text::slugify("\xE0\xAA\x97"), std::string("section"));   // nothing ASCII left
    CHECK_EQ(text::md_link_text("a [b] c"), std::string("a \\[b\\] c"));
    CHECK(text::ends_with_sentence_punct("done. "));
    CHECK(!text::ends_with_sentence_punct("not done"));
}

TEST(yaml_and_file_name_text_is_always_safe) {
    CHECK_EQ(text::yaml_quote("a \"b\" \\ c\n"), std::string("\"a \\\"b\\\" \\\\ c\\n\""));
    // A stray control byte (U+0092, common in badly made PDFs) and a line separator.
    CHECK_EQ(text::yaml_quote("it\xC2\x92s\xE2\x80\xA8ok"), std::string("\"it\\x92s\\u2028ok\""));
    // Bytes that are not UTF-8 at all become U+FFFD instead of breaking the file.
    CHECK_EQ(text::yaml_quote("a\xFF" "b"), std::string("\"a\xEF\xBF\xBD" "b\""));
    CHECK_EQ(text::sanitize_utf8("ma\x07nual\xFF.pdf"), std::string("manual\xEF\xBF\xBD.pdf"));
}

TEST(sha256_known_vectors) {
    const auto hex = [](std::string_view s) {
        Sha256 h;
        h.update({reinterpret_cast<const std::uint8_t*>(s.data()), s.size()});
        return to_hex(h.finish());
    };
    CHECK_EQ(hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    Sha256 million;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) {
        million.update({reinterpret_cast<const std::uint8_t*>(chunk.data()), chunk.size()});
    }
    CHECK_EQ(to_hex(million.finish()),
             std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

// ---- layout -----------------------------------------------------------------

TEST(layout_builds_lines) {
    PageText page;
    put_text(page, "First line here", 72, 100);
    put_text(page, "Second line", 72, 113);
    const auto lines = build_lines(page);
    REQUIRE(lines.size() == 2);
    CHECK_EQ(lines[0].text, std::string("First line here"));
    CHECK_EQ(lines[1].text, std::string("Second line"));
    CHECK(lines[0].size == 10.0f && !lines[0].bold);
}

TEST(layout_keeps_superscript_on_the_line) {
    PageText page;
    put_text(page, "Area 5 m", 72, 100);
    put_text(page, "2", 72 + 8 * 5.5f, 96, 6);        // raised, smaller
    put_text(page, " total", 72 + 9 * 5.5f, 100);
    const auto lines = build_lines(page);
    REQUIRE(lines.size() == 1);
    CHECK_EQ(lines[0].text, std::string("Area 5 m2 total"));
    CHECK(lines[0].size == 10.0f);
}

TEST(layout_marks_column_gaps) {
    PageText page;
    put_text(page, "Bus voltage", 72, 100, 9, true);
    put_text(page, "200", 250, 100, 9, true);
    put_text(page, "480", 320, 100, 9, true);
    const auto lines = build_lines(page);
    REQUIRE(lines.size() == 1);
    CHECK_EQ(lines[0].text, std::string("Bus voltage 200 480"));
    CHECK_EQ(lines[0].gaps.size(), std::size_t{2});
    CHECK(lines[0].bold);
    CHECK_EQ(row_text(lines[0]), std::string("Bus voltage | 200 | 480"));
}

TEST(layout_adds_a_space_where_the_pdf_has_only_a_gap) {
    PageText page;
    put_text(page, "left", 72, 100);              // ends at x = 93.5
    put_text(page, "right", 72 + 4 * 5.5f + 14, 100);   // 1.4 em further on, no space character
    const auto lines = build_lines(page);
    REQUIRE(lines.size() == 1);
    CHECK_EQ(lines[0].text, std::string("left right"));
    CHECK(lines[0].gaps.empty());                 // a word gap, not a column gap
}

TEST(layout_remembers_how_wide_each_space_is) {
    PageText page;
    put_text(page, "ab", 72, 100);          // pen positions 72 and 77.5
    put_text(page, "cd", 72 + 33, 100);     // 'c' starts 27.5 pt after 'b' did
    put_text(page, " e", 72 + 44 - 5.5f, 100);   // an ordinary space, then 'e' at 116
    const auto lines = build_lines(page);
    REQUIRE(lines.size() == 1);
    CHECK_EQ(lines[0].text, std::string("ab cd e"));
    CHECK(std::fabs(lines[0].origin_x - 72.0f) < 0.01f);
    REQUIRE(lines[0].space_spans.size() == 2);    // one entry per space in the text
    CHECK(std::fabs(lines[0].space_spans[0] - 27.5f) < 0.01f);
    CHECK(std::fabs(lines[0].space_spans[1] - 5.5f) < 0.01f);
}

TEST(layout_starts_a_new_line_when_the_text_jumps_back) {
    // A table row whose right cell is stored before its left cell.
    PageText page;
    put_text(page, "200", 250, 100);
    put_text(page, "Bus voltage", 72, 100);   // same baseline, but far to the left of what came before
    const auto lines = build_lines(page);
    REQUIRE(lines.size() == 2);
    CHECK_EQ(lines[0].text, std::string("200"));
    CHECK_EQ(lines[1].text, std::string("Bus voltage"));
}

TEST(layout_takes_the_baseline_of_the_main_text) {
    PageText page;
    put_text(page, "1", 72, 96, 6);            // a raised footnote mark comes first
    put_text(page, "Footnote text", 76, 100);
    const auto lines = build_lines(page);
    REQUIRE(lines.size() == 1);
    CHECK_EQ(lines[0].text, std::string("1Footnote text"));
    CHECK(std::fabs(lines[0].baseline - 100.0f) < 0.01f);
    CHECK(std::fabs(lines[0].size - 10.0f) < 0.01f);
}

TEST(layout_sorts_a_page_that_is_stored_out_of_order) {
    PageText page;
    for (int i = 9; i >= 0; --i) {             // the bottom line is stored first
        put_text(page, "Line number " + std::to_string(i), 72, 100 + 12.0f * static_cast<float>(i));
    }
    const auto lines = build_lines(page);
    REQUIRE(lines.size() == 10);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        CHECK_EQ(lines[i].text, "Line number " + std::to_string(i));
    }
}

TEST(layout_keeps_the_order_of_two_columns) {
    PageText page;
    for (int i = 0; i < 6; ++i) put_text(page, "Left " + std::to_string(i), 72, 100 + 12.0f * static_cast<float>(i));
    for (int i = 0; i < 6; ++i) put_text(page, "Right " + std::to_string(i), 330, 100 + 12.0f * static_cast<float>(i));
    const auto lines = build_lines(page);
    REQUIRE(lines.size() == 12);
    CHECK_EQ(lines[5].text, std::string("Left 5"));    // the whole left column first
    CHECK_EQ(lines[6].text, std::string("Right 0"));
}

TEST(layout_expands_ligatures) {
    PageText page;
    put_text(page, "con\xEF\xAC\x81g", 72, 100);   // "con" + U+FB01 + "g"
    const auto lines = build_lines(page);
    REQUIRE(lines.size() == 1);
    CHECK_EQ(lines[0].text, std::string("config"));
}

// ---- furniture --------------------------------------------------------------

TEST(furniture_removes_repeating_headers_and_footers) {
    std::vector<std::vector<Line>> pages(5);
    const std::vector<float> heights(5, 792.0f);
    for (int p = 0; p < 5; ++p) {
        auto& page = pages[static_cast<std::size_t>(p)];
        page.push_back(make_line(p, "ACME Reference Manual", 40, 8));
        page.push_back(make_line(p, "Body text of page " + std::to_string(p + 1), 300));
        page.push_back(make_line(p, "Page " + std::to_string(p + 1) + " of 5", 760, 8));
    }
    pages[2].push_back(make_line(2, "ACME Reference Manual", 400));   // same words in the body stay

    const FurnitureReport report = remove_furniture(pages, heights);
    CHECK_EQ(report.removed_lines, std::size_t{10});
    REQUIRE(report.patterns.size() == 2);
    CHECK(report.patterns[0].top && !report.patterns[1].top);
    CHECK_EQ(pages[0].size(), std::size_t{1});
    REQUIRE(pages[2].size() == 2);
    CHECK_EQ(pages[2][1].text, std::string("ACME Reference Manual"));
}

TEST(furniture_keeps_text_that_only_repeats_at_the_top_of_some_pages) {
    // Small top margin: the first lines of the body lie inside the band too.
    std::vector<std::vector<Line>> pages(10);
    const std::vector<float> heights(10, 842.0f);
    const char* chapters[] = {"Abstract Commands", "Program Buffer", "System Bus Access", "Reset Control",
                              "Halt Control",      "Triggers",       "Registers",         "Security",
                              "Hardware",          "Glossary"};
    for (int p = 0; p < 10; ++p) {
        auto& page = pages[static_cast<std::size_t>(p)];
        // Running header: a different text on every page, but always "... | Page N".
        page.push_back(make_line(p, std::string(chapters[p]) + " | Page " + std::to_string(p + 1), 18, 8));
        // First body line: on three pages the header row of a table that continues.
        page.push_back(make_line(p, p % 3 == 0 ? "Field Description Access Reset"
                                               : "This page explains " + std::string(chapters[p]), 46));
        page.push_back(make_line(p, "More body text in the middle of page " + std::to_string(p + 1), 400));
        page.push_back(make_line(p, "The Debug Specification", 826, 8));
    }

    const FurnitureReport report = remove_furniture(pages, heights);
    CHECK_EQ(report.removed_lines, std::size_t{20});   // 10 headers + 10 footers, nothing else
    REQUIRE(report.patterns.size() == 2);
    CHECK_EQ(report.patterns[0].pages, std::size_t{10});
    CHECK_EQ(report.patterns[0].variants, std::size_t{10});
    CHECK_EQ(report.patterns[0].sample, std::string("Abstract Commands | Page 1"));
    for (const auto& page : pages) {
        REQUIRE(page.size() == 2);
    }
    CHECK_EQ(pages[0][0].text, std::string("Field Description Access Reset"));
    CHECK_EQ(pages[3][0].text, std::string("Field Description Access Reset"));
}

TEST(furniture_is_only_what_lies_at_the_page_edge) {
    // Every page starts with a line of ordinary text, and below it the same
    // table header on all pages. The header repeats, but text stands between
    // it and the page edge: it is content, not a running header.
    std::vector<std::vector<Line>> pages(10);
    const std::vector<float> heights(10, 842.0f);
    const char* openings[] = {"Abstract commands are optional", "The program buffer holds code",
                              "System bus access is direct",    "Reset control uses two bits",
                              "Halting works per hart",         "Triggers match on addresses",
                              "Registers are listed here",      "Security is left to the system",
                              "Hardware platforms may differ",  "Glossary of the terms used"};
    for (int p = 0; p < 10; ++p) {
        auto& page = pages[static_cast<std::size_t>(p)];
        page.push_back(make_line(p, openings[p], 30));
        page.push_back(make_line(p, "Field Description Access Reset", 46));
        page.push_back(make_line(p, "Body text in the middle of page " + std::to_string(p + 1), 400));
        page.push_back(make_line(p, "The Debug Specification", 826, 8));   // a real footer
    }
    const FurnitureReport report = remove_furniture(pages, heights);
    CHECK_EQ(report.removed_lines, std::size_t{10});   // the footers and nothing else
    REQUIRE(pages[4].size() == 3);
    CHECK_EQ(pages[4][1].text, std::string("Field Description Access Reset"));
}

TEST(furniture_needs_a_position_shared_by_many_pages) {
    // A sub-heading that repeats at the top of a few pages only, with no real header above it.
    std::vector<std::vector<Line>> pages(20);
    const std::vector<float> heights(20, 842.0f);
    for (int p = 0; p < 20; ++p) {
        auto& page = pages[static_cast<std::size_t>(p)];
        if (p < 5) page.push_back(make_line(p, "Synopsis", 40, 14, true));
        page.push_back(make_line(p, "Body text of page " + std::to_string(p + 1), 300));
    }
    CHECK_EQ(remove_furniture(pages, heights).removed_lines, std::size_t{0});
    CHECK_EQ(pages[0].size(), std::size_t{2});
}

TEST(furniture_leaves_short_documents_alone) {
    // Up to three pages: too few to tell a running header from a title that repeats.
    for (const std::size_t page_count : {std::size_t{2}, std::size_t{3}, std::size_t{4}}) {
        std::vector<std::vector<Line>> pages(page_count);
        const std::vector<float> heights(page_count, 792.0f);
        for (std::size_t p = 0; p < page_count; ++p) {
            pages[p].push_back(make_line(static_cast<int>(p), "Header", 40));
            pages[p].push_back(make_line(static_cast<int>(p), "Body of page " + std::string(p + 1, 'x'), 300));
        }
        CHECK_EQ(remove_furniture(pages, heights).removed_lines, page_count == 4 ? std::size_t{4} : std::size_t{0});
    }
    CHECK_EQ(furniture_key("Page 12 of 340"), std::string("page # of #"));
    CHECK_EQ(furniture_key("xii"), std::string("#"));
}

// ---- headings ---------------------------------------------------------------

TEST(section_numbers_are_parsed) {
    std::string_view rest;
    auto n = parse_section_number("5.2.1 Clock tree", rest);
    CHECK(n && n->parts == std::vector<int>({5, 2, 1}) && n->text == "5.2.1" && rest == "Clock tree");
    n = parse_section_number("3. Charging", rest);
    CHECK(n && n->parts == std::vector<int>({3}) && n->text == "3" && rest == "Charging");
    n = parse_section_number("A.1 Status register", rest);
    CHECK(n && n->parts == std::vector<int>({1000, 1}) && n->text == "A.1");
    n = parse_section_number("Appendix B Register map", rest);
    CHECK(n && n->keyword && n->parts == std::vector<int>({1001}) && rest == "Register map");
    n = parse_section_number("Chapter 3: Clocks", rest);
    CHECK(n && n->keyword && n->parts == std::vector<int>({3}) && rest == "Clocks");

    CHECK(!parse_section_number("2024 Annual report", rest));       // a year, not a section
    CHECK(!parse_section_number("3.3V supply", rest));              // no space after the number
    CHECK(!parse_section_number("Section describes the bus", rest));
    CHECK(!parse_section_number("The quick brown fox", rest));
    CHECK(!parse_section_number("5.", rest));
}

TEST(numbering_sequence_rules) {
    const auto follows = [](std::vector<int> a, std::vector<int> b) { return is_successor(a, b); };
    CHECK(follows({2, 1}, {2, 2}));
    CHECK(follows({2, 1}, {2, 1, 1}));
    CHECK(follows({2, 1, 3}, {2, 2}));
    CHECK(follows({2, 9}, {3}));
    CHECK(follows({1000}, {1000, 1}));
    CHECK(!follows({2, 1}, {3, 3}));
    CHECK(!follows({2, 1}, {2, 3}));
    CHECK(!follows({2}, {2}));
}

TEST(headings_from_text_follow_style_and_sequence) {
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text, float size = 10, bool bold = false) {
        lines.push_back(make_line(0, std::move(text), y, size, bold));
        y += 20;
    };
    add("ACME Controller", 28, true);                       // cover title: a style of its own
    add("1 Introduction ........ 3");                       // table of contents entry
    add("Revision history", 16, true);                      // unnumbered, set like a heading
    add("First release of this manual.");
    add("1 Introduction", 16, true);
    add("The controller supervises the power stage.");
    add("1. Connect the cable and power on.");              // numbered list item, body style
    add("1.1 Scope", 13, true);
    add("This manual covers the controller board.");
    add("3.3 V supply", 13, true);                          // bold table cell, breaks the sequence
    add("1.2 Terms", 13, true);
    add("EVSE means electric vehicle supply equipment.");
    add("2 DERATE R Derating is active", 13, true);         // bold table row that would fit the sequence
    lines.back().gaps = {1, 8, 10};
    add("2 Electrical characteristics", 16, true);
    add("More body text follows here to set the body size.");

    const HeadingResult result = detect_headings(lines, {}, {});
    CHECK(result.source == HeadingSource::Numbering);
    CHECK(result.body_size == 10.0f);
    const std::vector<std::string> expected = {"Revision history", "1 Introduction", "1.1 Scope",
                                               "1.2 Terms", "2 Electrical characteristics"};
    REQUIRE(titles(result.headings) == expected);
    CHECK_EQ(result.headings[0].level, 1);
    CHECK_EQ(result.headings[2].level, 2);
    CHECK_EQ(result.headings[2].number, std::string("1.1"));
}

TEST(bold_at_text_size_marks_a_heading) {
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text, bool bold = false) {
        lines.push_back(make_line(0, std::move(text), y, 10, bold));
        y += 20;
    };
    add("1 Scope", true);
    add("This manual covers the controller board. The contactor has");
    add("2 poles and a coil");                       // a line of text that starts with a number
    add("2 Supply", true);
    add("The logic supply is 3.3 V and the coil supply is 12 V.");
    add("3 Limits", true);
    add("All limits apply over the full temperature range.");

    const HeadingResult result = detect_headings(lines, {}, {});
    const std::vector<std::string> expected = {"1 Scope", "2 Supply", "3 Limits"};
    CHECK(titles(result.headings) == expected);
}

TEST(bold_means_nothing_when_all_the_text_is_bold) {
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text) {
        lines.push_back(make_line(0, std::move(text), y, 10, true));
        y += 20;
    };
    add("1 Scope");
    add("1. Connect the cable and switch the controller on.");
    add("2. Wait until the green lamp is on.");
    add("2 Supply");
    add("The logic supply is 3.3 V and the coil supply is 12 V.");
    add("3 Limits");
    add("All limits apply over the full temperature range.");

    const HeadingResult result = detect_headings(lines, {}, {});
    const std::vector<std::string> expected = {"1 Scope", "2 Supply", "3 Limits"};
    CHECK(titles(result.headings) == expected);
}

TEST(a_heading_right_after_a_numbered_list_is_kept) {
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text, float size = 10, bool bold = false) {
        lines.push_back(make_line(0, std::move(text), y, size, bold));
        y += 20;
    };
    add("1 Introduction", 16, true);
    add("The procedure has five steps, which are listed here in their order.");
    add("1. Open the front cover");
    add("2. Loosen the four screws");
    add("3. Pull out the controller board");
    add("4. Put in the new board");
    add("5. Close the front cover");
    add("1.1 Scope", 13, true);                      // directly below the list
    add("This manual covers the controller board, hardware revision C.");
    add("1.2 Terms", 13, true);
    add("EVSE means electric vehicle supply equipment in this manual.");

    const HeadingResult result = detect_headings(lines, {}, {});
    const std::vector<std::string> expected = {"1 Introduction", "1.1 Scope", "1.2 Terms"};
    CHECK(titles(result.headings) == expected);
}

TEST(a_short_table_of_contents_is_known_by_its_page_numbers) {
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text, float size = 10, bool bold = false) {
        lines.push_back(make_line(0, std::move(text), y, size, bold));
        y += 24;
    };
    // Three entries only, set like headings; a wide gap stands before each page number.
    add("1 Introduction 3", 16, true);
    lines.back().gaps = {14};
    add("2 Supply 4", 16, true);
    lines.back().gaps = {8};
    add("3 Limits 6", 16, true);
    lines.back().gaps = {8};
    add("Plain text between the contents and the first chapter.");
    for (const char* heading : {"1 Introduction", "2 Supply", "3 Limits"}) {
        add(heading, 16, true);
        add("Body text of this chapter, long enough to count as text.");
        add("A second line of body text under the same heading here.");
    }
    const HeadingResult result = detect_headings(lines, {}, {});
    const std::vector<std::string> expected = {"1 Introduction", "2 Supply", "3 Limits"};
    CHECK(titles(result.headings) == expected);

    // The same with dots that lead to the page number instead of a gap.
    lines[0] = make_line(0, "1 Introduction .......... 3", lines[0].baseline, 16, true);
    lines[1] = make_line(0, "2 Supply . . . . . . . . 4", lines[1].baseline, 16, true);
    lines[2] = make_line(0, "3 Limits ................ 6", lines[2].baseline, 16, true);
    CHECK(titles(detect_headings(lines, {}, {}).headings) == expected);
}

TEST(a_title_may_start_with_a_count_when_set_as_a_heading) {
    std::vector<Line> lines;
    float y = 100;
    const auto add_body = [&] {
        lines.push_back(make_line(0, "Body text in the ordinary size, long enough to be the common one.", y));
        y += 20;
        lines.push_back(make_line(0, "A second line of body text keeps ten point the most common size.", y));
        y += 20;
    };
    lines.push_back(make_line(0, "02. HOW IT COMPARES", y, 13, true));
    y += 20;
    add_body();
    lines.push_back(make_line(0, "03. 3 REAL THINGS PEOPLE BUILT", y, 13, true));   // set as a heading
    y += 20;
    add_body();
    lines.push_back(make_line(0, "2. 5 V supply on the board", y));                  // plain text: no heading
    y += 20;
    add_body();
    lines.push_back(make_line(0, "04. THE PATTERN", y, 13, true));
    y += 20;
    add_body();

    const HeadingResult result = detect_headings(lines, {}, {});
    const std::vector<std::string> expected = {"02. HOW IT COMPARES", "03. 3 REAL THINGS PEOPLE BUILT",
                                               "04. THE PATTERN"};
    CHECK(titles(result.headings) == expected);
}

TEST(numbered_sentences_are_not_titles) {
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text) {         // everything here is set like a heading
        lines.push_back(make_line(0, std::move(text), y, 13, true));
        y += 20;
    };
    const auto add_body = [&] {
        lines.push_back(make_line(0, "Body text in the ordinary size, long enough to be the common one.", y));
        y += 20;
        lines.push_back(make_line(0, "A second line of body text keeps ten point the most common size.", y));
        y += 20;
    };
    add("4.1 Outputs");
    add_body();
    // More than twenty words:
    add("4.2 It is the case that an output may be on or off at any time and so is the fan of the unit by the way");
    add("4.2 Inputs");
    add_body();
    add("4.3 The inputs are filtered,");                                  // ends with a comma
    add("4.3 Faults");
    add_body();
    add("4.4 A fault is stored until the next reset of the controller board by the user.");   // a long sentence
    add("4.4 Service");
    add_body();

    const HeadingResult result = detect_headings(lines, {}, {});
    const std::vector<std::string> expected = {"4.1 Outputs", "4.2 Inputs", "4.3 Faults", "4.4 Service"};
    CHECK(titles(result.headings) == expected);
}

TEST(headings_join_wrapped_titles) {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "4.2 Behaviour of the output stage during", 100, 13, true));
    lines.push_back(make_line(0, "a mains failure", 116, 13, true));
    lines.push_back(make_line(0, "Body text starts here and is long enough to matter.", 140));
    lines.push_back(make_line(0, "4.3 Restart", 180, 13, true));
    lines.push_back(make_line(0, "More body text so that ten point is the body size.", 200));
    lines.push_back(make_line(0, "4.4 Faults", 240, 13, true));
    const HeadingResult result = detect_headings(lines, {}, {});
    REQUIRE(result.headings.size() == 3);
    CHECK_EQ(result.headings[0].title, std::string("4.2 Behaviour of the output stage during a mains failure"));
    CHECK_EQ(result.headings[0].line_count, std::size_t{2});
}

TEST(headings_in_unstyled_text_need_a_short_line) {
    // Everything is set in one font, as in a PDF made from plain text.
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text) {
        lines.push_back(make_line(0, std::move(text), y));
        y += 13;
    };
    const auto add_body = [&](int count) {
        for (int i = 0; i < count; ++i) {
            add("Body text line that reaches the right margin of the page, as most lines do here.");
        }
    };
    add("1 Scope");
    add_body(8);
    add("2 Supply");
    add_body(4);
    // A full line of body text that happens to start with the next number.
    add("3 V is the lowest logic supply voltage at which the controller still starts up");
    add_body(8);
    add("3 Limits");
    add_body(4);

    const HeadingResult result = detect_headings(lines, {}, {});
    CHECK(result.source == HeadingSource::Numbering);
    const std::vector<std::string> expected = {"1 Scope", "2 Supply", "3 Limits"};
    REQUIRE(titles(result.headings) == expected);
    CHECK_EQ(result.headings[0].line_count, std::size_t{1});   // body lines are not pulled into the title
}

TEST(a_table_of_contents_is_not_a_run_of_headings) {
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text, float size = 10, bool bold = false) {
        lines.push_back(make_line(0, std::move(text), y, size, bold));
        y += 20;
    };
    // Contents page: bold entries without dot leaders, one per line.
    for (const char* entry : {"1 Introduction 3", "2 Supply 4", "3 Limits 6", "4 Faults 8", "5 Service 9"}) {
        add(entry, 11, true);
    }
    add("Plain text between the contents and the first chapter.");
    for (const char* heading : {"1 Introduction", "2 Supply", "3 Limits"}) {
        add(heading, 16, true);
        add("Body text of this chapter, long enough to count as text.");
        add("A second line of body text under the same heading here.");
    }
    const HeadingResult result = detect_headings(lines, {}, {});
    const std::vector<std::string> expected = {"1 Introduction", "2 Supply", "3 Limits"};
    CHECK(titles(result.headings) == expected);
}

TEST(headings_set_in_another_font_at_text_size) {
    // Font 1 is the text font, font 2 the heading font, font 3 the italic of a note.
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text, std::uint16_t font, float size = 11) {
        Line line = make_line(0, std::move(text), y, size);
        line.font = font;
        lines.push_back(std::move(line));
        y += 14;
    };
    add("9.1. Base Vector Architecture", 2, 20);
    add("Body text of this section, set in the ordinary text font of the document.", 1);
    add("9.1.1. Constant Parameters", 2);                   // heading font, SAME size as the text
    add("Body text under the first subsection, again in the ordinary text font.", 1);
    add("9.1.1.1. Vector Registers", 2, 9);                 // heading font, SMALLER than the text
    add("More body text that is long enough to keep font 1 the most common font.", 1);
    add("More body text in the ordinary text font, so that it stays the common one.", 1);
    add("In particular, when the action is zero:", 3);      // a note in italics ...
    add("1. triggers can cause a breakpoint that is", 3);   // ... with a numbered list inside
    add("taken from the same mode.", 3);
    add("2. counters stop while the hart is halted", 3);
    add("and resume afterwards.", 3);
    add("3. the program buffer is left as it was", 3);
    add("by the last command.", 3);
    add("4. writes to this register are ignored", 3);
    add("until the next reset.", 3);
    add("5. reads return the value that was last", 3);
    add("written by the debugger.", 3);
    add("9.1.2. Programmer's Model", 2);
    add("Closing body text of the example, once more in the ordinary text font.", 1);
    add("And a last line of body text in the ordinary text font of the document.", 1);

    const HeadingResult result = detect_headings(lines, {}, {});
    const std::vector<std::string> expected = {"9.1. Base Vector Architecture", "9.1.1. Constant Parameters",
                                               "9.1.1.1. Vector Registers", "9.1.2. Programmer's Model"};
    REQUIRE(titles(result.headings) == expected);
    CHECK_EQ(result.headings[1].level, 3);
    CHECK_EQ(result.headings[2].level, 4);
}

TEST(titles_may_be_identifiers_but_not_bare_numbers) {
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text, float size = 10, bool bold = false) {
        lines.push_back(make_line(0, std::move(text), y, size, bold));
        y += 20;
    };
    add("8.1 Support routines", 13, true);
    add("Body text that explains what the routines are for.");
    add("8.1.1 __arm_sme_state", 13, true);
    add("Body text that explains the first routine in detail.");
    add("8.1.2 64-bit mode", 13, true);
    add("Body text that explains the second topic in detail.");
    add("8.1.3 5 V", 13, true);                              // a value, not a title
    add("Body text at the end of this small example document.");

    const HeadingResult result = detect_headings(lines, {}, {});
    const std::vector<std::string> expected = {"8.1 Support routines", "8.1.1 __arm_sme_state",
                                               "8.1.2 64-bit mode"};
    CHECK(titles(result.headings) == expected);
}

TEST(a_block_of_large_text_is_not_a_heading) {
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text, float size = 10, bool bold = false) {
        lines.push_back(make_line(0, std::move(text), y, size, bold));
        y += size > 10 ? 18 : 30;
    };
    add("1 Introduction", 14, true);
    add("Body text of the introduction, long enough to count.");
    add("A second line of body text keeps ten point the most common size.");
    add("A third line of body text, for the same reason as the second one.");
    // A quotation set as large as a heading, four lines long.
    add("The noise source state shall be protected from", 14, true);
    add("adversarial knowledge or influence to the greatest", 14, true);
    add("extent possible, and the methods used for this", 14, true);
    add("shall be documented in detail", 14, true);
    add("Body text that follows the quotation in the text font.");
    add("2 Supply", 14, true);
    add("Body text of the second chapter of the example.");
    add("3 Limits", 14, true);
    add("Body text of the third chapter of the example.");

    const HeadingResult result = detect_headings(lines, {}, {});
    CHECK(result.body_size == 10.0f);
    const std::vector<std::string> expected = {"1 Introduction", "2 Supply", "3 Limits"};
    CHECK(titles(result.headings) == expected);
}

TEST(text_headings_are_added_below_the_bookmark_level) {
    std::vector<Line> lines;
    float y = 100;
    const auto add = [&](std::string text, float size = 10, bool bold = false) {
        lines.push_back(make_line(0, std::move(text), y, size, bold));
        y += 20;
    };
    const auto add_body = [&] { add("Body text under the heading above, in the ordinary size."); };
    add("2 Supply", 16, true);          add_body();
    add("2.1 Limits", 13, true);        add_body();
    add("2.2 Contactor", 13, true);     add_body();
    // Numbered like headings and bold, but the numbers do not belong to chapter 2.
    add("7.1 Legacy item one", 13, true);    add_body();
    add("7.2 Legacy item two", 13, true);    add_body();
    add("7.3 Legacy item three", 13, true);  add_body();
    add("7.4 Legacy item four", 13, true);   add_body();
    add("3 Faults", 16, true);          add_body();
    add("3.1 Reset", 13, true);         add_body();
    add("4 Service", 16, true);         add_body();

    // The bookmarks only know the chapters.
    const std::vector<OutlineEntry> outline = {{"2 Supply", 1, 0, {}, {}},
                                               {"3 Faults", 1, 0, {}, {}},
                                               {"4 Service", 1, 0, {}, {}}};
    const std::vector<Affine> transforms(1);

    const HeadingResult result = detect_headings(lines, outline, transforms);
    CHECK(result.source == HeadingSource::Bookmark);
    CHECK_EQ(result.text_added, std::size_t{3});
    const std::vector<std::string> expected = {"2 Supply", "2.1 Limits", "2.2 Contactor",
                                               "3 Faults", "3.1 Reset",  "4 Service"};
    REQUIRE(titles(result.headings) == expected);
    CHECK(result.headings[0].source == HeadingSource::Bookmark);
    CHECK(result.headings[1].source == HeadingSource::Numbering);
    CHECK_EQ(result.headings[1].level, 2);

    HeadingOptions bookmarks_only;
    bookmarks_only.mode = HeadingMode::Bookmarks;
    CHECK_EQ(detect_headings(lines, outline, transforms, bookmarks_only).headings.size(), std::size_t{3});
}

TEST(a_bookmark_matches_the_heading_not_the_same_words_elsewhere) {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "8 Support routines", 100, 24));
    lines.push_back(make_line(0, "8.1.1 __arm_sme_state", 130));           // small contents list of the chapter
    lines.push_back(make_line(0, "__arm_sme_state", 160, 13, true));        // the term alone, set large
    lines.push_back(make_line(0, "Provides a safe way of detecting the state.", 174));
    lines.push_back(make_line(0, "8.1.1 __arm_sme_state", 220, 12.5f));     // the heading itself
    lines.push_back(make_line(0, "Body text under the heading.", 240));
    lines.push_back(make_line(0, "9 Notes", 300, 24));

    const std::vector<OutlineEntry> outline = {{"8 Support routines", 1, 0, {}, {}},
                                               {"8.1.1 __arm_sme_state", 2, 0, {}, {}},
                                               {"9 Notes", 1, 0, {}, {}}};
    const std::vector<Affine> transforms(1);
    HeadingOptions bookmarks_only;
    bookmarks_only.mode = HeadingMode::Bookmarks;
    const HeadingResult result = detect_headings(lines, outline, transforms, bookmarks_only);
    REQUIRE(result.headings.size() == 3);
    CHECK_EQ(result.bookmarks_located, std::size_t{3});
    CHECK_EQ(result.headings[1].line_index, std::size_t{4});
}

TEST(headings_from_bookmarks_are_located_in_the_text) {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "Cover page text", 100));
    lines.push_back(make_line(1, "1 Introduction", 100, 16, true));
    lines.push_back(make_line(1, "Body of the introduction.", 130));
    lines.push_back(make_line(1, "1.1 Scope and", 200, 13, true));
    lines.push_back(make_line(1, "purpose", 216, 13, true));
    lines.push_back(make_line(1, "Body of the scope.", 240));
    lines.push_back(make_line(2, "Figure only page caption", 500));

    std::vector<OutlineEntry> outline;
    outline.push_back({"Introduction", 1, 1, {}, {}});          // bookmark without the number
    outline.push_back({"1.1 Scope and purpose", 2, 1, {}, {}}); // title wrapped over two lines
    outline.push_back({"2 Block diagram", 1, 2, {}, {}});       // not in the page text
    const std::vector<Affine> transforms(3);

    const HeadingResult result = detect_headings(lines, outline, transforms);
    CHECK(result.source == HeadingSource::Bookmark);
    CHECK_EQ(result.bookmarks_total, std::size_t{3});
    CHECK_EQ(result.bookmarks_located, std::size_t{2});
    REQUIRE(result.headings.size() == 3);
    CHECK_EQ(result.headings[0].title, std::string("1 Introduction"));
    CHECK_EQ(result.headings[0].line_index, std::size_t{1});
    CHECK_EQ(result.headings[0].number, std::string("1"));
    CHECK_EQ(result.headings[1].title, std::string("1.1 Scope and purpose"));
    CHECK_EQ(result.headings[1].line_count, std::size_t{2});
    CHECK_EQ(result.headings[2].title, std::string("2 Block diagram"));
    CHECK_EQ(result.headings[2].line_count, std::size_t{0});
    CHECK_EQ(result.headings[2].line_index, std::size_t{6});
}

TEST(a_bookmark_that_is_not_in_the_text_is_placed_by_its_position) {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "1 Overview", 100, 16, true));
    lines.push_back(make_line(0, "Text above the figure.", 130));
    lines.push_back(make_line(0, "Text below the figure, which belongs to the figure.", 400));
    lines.push_back(make_line(0, "2 Details", 500, 16, true));
    lines.push_back(make_line(0, "Text of the details.", 530));

    // The second bookmark points at the title of a figure, which is a picture: y = 300 on the page.
    std::vector<OutlineEntry> outline;
    outline.push_back({"1 Overview", 1, 0, {}, {}});
    outline.push_back({"Block diagram", 2, 0, 72.0f, 300.0f});
    outline.push_back({"2 Details", 1, 0, {}, {}});
    const std::vector<Affine> transforms(1);   // identity: the same coordinates on both sides
    HeadingOptions bookmarks_only;
    bookmarks_only.mode = HeadingMode::Bookmarks;

    const HeadingResult result = detect_headings(lines, outline, transforms, bookmarks_only);
    REQUIRE(result.headings.size() == 3);
    CHECK_EQ(result.bookmarks_located, std::size_t{2});
    CHECK_EQ(result.headings[1].title, std::string("Block diagram"));
    CHECK_EQ(result.headings[1].line_count, std::size_t{0});
    CHECK_EQ(result.headings[1].line_index, std::size_t{2});   // the first line below y = 300

    const std::vector<Section> sections = build_sections(lines, result.headings);
    REQUIRE(sections.size() == 3);
    REQUIRE(sections[1].paragraphs.size() == 1);
    CHECK_EQ(sections[1].paragraphs[0].text, std::string("Text below the figure, which belongs to the figure."));
}

TEST(two_bookmarks_never_share_one_line) {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "Overview", 100, 16, true));
    lines.push_back(make_line(0, "Text of the overview.", 130));
    lines.push_back(make_line(0, "Details", 200, 16, true));
    lines.push_back(make_line(0, "Text of the details.", 230));
    const std::vector<OutlineEntry> outline = {{"Overview", 1, 0, {}, {}},
                                               {"Overview", 1, 0, {}, {}},   // the same title again
                                               {"Details", 1, 0, {}, {}}};
    const std::vector<Affine> transforms(1);
    const HeadingResult result = detect_headings(lines, outline, transforms);
    REQUIRE(result.headings.size() == 3);
    CHECK_EQ(result.bookmarks_located, std::size_t{2});
    CHECK_EQ(result.headings[0].line_count, std::size_t{1});
    CHECK_EQ(result.headings[1].line_count, std::size_t{0});   // not in the text a second time
}

TEST(a_pdf_with_only_one_or_two_bookmarks_is_read_by_its_text) {
    std::vector<Line> lines;
    float y = 100;
    for (const char* heading : {"1 Scope", "2 Supply", "3 Limits"}) {
        lines.push_back(make_line(0, heading, y, 16, true));
        lines.push_back(make_line(0, "Body text of this chapter, long enough to count as text.", y + 24));
        y += 60;
    }
    const std::vector<OutlineEntry> outline = {{"Cover", 1, 0, {}, {}}, {"1 Scope", 1, 0, {}, {}}};
    const std::vector<Affine> transforms(1);
    const HeadingResult result = detect_headings(lines, outline, transforms);
    CHECK(result.source == HeadingSource::Numbering);
    CHECK_EQ(result.headings.size(), std::size_t{3});
}

// ---- sections ---------------------------------------------------------------

TEST(paragraphs_follow_spacing_pages_and_lists) {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "First paragraph, line one", 100));
    lines.push_back(make_line(0, "and line two.", 113));
    lines.push_back(make_line(0, "Second paragraph that runs over", 133));
    lines.push_back(make_line(1, "the page break.", 90));
    lines.push_back(make_line(1, "\xE2\x80\xA2 a bullet", 103));
    lines.push_back(make_line(1, "(1) a numbered item", 116));
    lines.push_back(make_line(1, "An inter-", 140));            // after a gap: new paragraph
    lines.push_back(make_line(1, "rupt and RS-", 153));
    lines.push_back(make_line(1, "485 stay intact.", 166));

    const auto paragraphs = build_paragraphs(lines, 1.3f);
    REQUIRE(paragraphs.size() == 5);
    CHECK_EQ(paragraphs[0].text, std::string("First paragraph, line one and line two."));
    CHECK_EQ(paragraphs[1].text, std::string("Second paragraph that runs over the page break."));
    CHECK_EQ(paragraphs[1].page, 0);
    CHECK_EQ(paragraphs[2].text, std::string("\xE2\x80\xA2 a bullet"));
    // Terms broken at a hyphen are joined without a space; no character is dropped.
    CHECK_EQ(paragraphs[4].text, std::string("An inter-rupt and RS-485 stay intact."));

    SectionOptions joined;
    joined.dehyphenate = true;
    const auto dehyphenated = build_paragraphs(lines, 1.3f, joined);
    REQUIRE(dehyphenated.size() == 5);
    CHECK_EQ(dehyphenated[4].text, std::string("An interrupt and RS-485 stay intact."));
}

TEST(a_change_of_font_size_starts_a_new_paragraph) {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "A note in small print that", 100, 8));
    lines.push_back(make_line(0, "runs over two lines", 110, 8));
    lines.push_back(make_line(0, "Body text in the normal size", 122));      // close below, but larger
    lines.push_back(make_line(0, "which also runs over two lines.", 135));
    const auto paragraphs = build_paragraphs(lines, 1.3f);
    REQUIRE(paragraphs.size() == 2);
    CHECK_EQ(paragraphs[0].text, std::string("A note in small print that runs over two lines"));
    CHECK_EQ(paragraphs[1].text, std::string("Body text in the normal size which also runs over two lines."));
}

TEST(line_spacing_is_measured_before_paragraphs_are_cut) {
    // Double-spaced text: 24 pt from line to line at 10 pt, 48 pt between paragraphs.
    std::vector<Line> lines;
    float y = 100;
    for (int paragraph = 0; paragraph < 2; ++paragraph) {
        for (int i = 0; i < 5; ++i) {
            lines.push_back(make_line(0, "double spaced line " + std::to_string(i), y));
            y += 24;
        }
        y += 24;
    }
    CHECK(std::fabs(typical_line_pitch(lines) - 2.4f) < 0.01f);
    const std::vector<Section> sections = build_sections(lines, {});
    REQUIRE(sections.size() == 1);
    CHECK_EQ(sections[0].paragraphs.size(), std::size_t{2});
}

// Lines in font 1 are ordinary text, lines in font 2 are monospace, 6 pt per character.
SectionOptions monospace_options() {
    SectionOptions options;
    options.fonts = {FontFacts{}, FontFacts{false, 0.5f}, FontFacts{true, 0.6f}};
    return options;
}

Line text_line(int page, std::string text, float baseline) {
    Line line = make_line(page, std::move(text), baseline);
    line.font = 1;
    return line;
}

Line code_line(int page, std::string text, float baseline, float x0 = 72) {
    Line line = make_line(page, std::move(text), baseline, 10, false, x0);
    line.font = 2;
    return line;
}

TEST(monospace_lines_become_a_code_block) {
    std::vector<Line> lines;
    lines.push_back(text_line(0, "The loop is shown below.", 100));
    lines.push_back(code_line(0, "while (running) {", 120));
    lines.push_back(code_line(0, "step(); // one step", 132, 72 + 4 * 6));   // four characters further right
    // On the page three blanks stand before the comment: from ';' to '/' the pen moves four columns.
    lines.back().space_spans = {4 * 6.0f, 2 * 6.0f, 2 * 6.0f};
    lines.push_back(code_line(0, "}", 144));
    lines.push_back(code_line(0, "stop();", 168));                 // after an empty line
    lines.push_back(text_line(0, "The file is called", 200));
    lines.push_back(code_line(0, "charger.conf", 213));            // one monospace line inside a sentence
    lines.push_back(text_line(0, "and is read at start.", 226));

    const auto paragraphs = build_paragraphs(lines, 1.3f, monospace_options());
    REQUIRE(paragraphs.size() == 3);
    CHECK(paragraphs[1].kind == ParagraphKind::Code);
    CHECK_EQ(paragraphs[1].text, std::string("while (running) {\n    step();   // one step\n}\n\nstop();"));
    // A single monospace line is a term in the text, not a listing: the sentence stays whole.
    CHECK(paragraphs[2].kind == ParagraphKind::Text);
    CHECK_EQ(paragraphs[2].text, std::string("The file is called charger.conf and is read at start."));
}

TEST(a_code_block_ends_at_a_large_gap_a_new_column_or_a_new_page) {
    std::vector<Line> lines;
    lines.push_back(code_line(0, "first();", 100));
    lines.push_back(code_line(0, "second();", 112));
    lines.push_back(code_line(0, "third();", 200));            // far below: another listing
    lines.push_back(code_line(0, "fourth();", 212));
    lines.push_back(code_line(0, "right();", 100, 320));       // back up the page: the next column
    lines.push_back(code_line(0, "column();", 112, 320));
    lines.push_back(code_line(0, "alone();", 300, 320));       // far from the others: one line, no listing
    lines.push_back(code_line(1, "next();", 100));             // next page
    lines.push_back(code_line(1, "page();", 112));
    lines.push_back(code_line(2, "third();", 124));            // next page again, a little lower
    lines.push_back(code_line(2, "page();", 136));

    const auto paragraphs = build_paragraphs(lines, 1.3f, monospace_options());
    REQUIRE(paragraphs.size() == 6);
    CHECK_EQ(paragraphs[5].text, std::string("third();\npage();"));
    CHECK_EQ(paragraphs[5].page, 2);
    CHECK_EQ(paragraphs[0].text, std::string("first();\nsecond();"));
    CHECK_EQ(paragraphs[1].text, std::string("third();\nfourth();"));
    CHECK_EQ(paragraphs[2].text, std::string("right();\ncolumn();"));   // not indented by the column offset
    CHECK(paragraphs[2].kind == ParagraphKind::Code);
    CHECK_EQ(paragraphs[3].text, std::string("alone();"));
    CHECK(paragraphs[3].kind == ParagraphKind::Text);
    CHECK_EQ(paragraphs[4].text, std::string("next();\npage();"));
    CHECK_EQ(paragraphs[4].page, 1);
}

TEST(table_rows_stay_on_their_own_lines) {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "Limits are listed below.", 100));
    Line header = make_line(0, "Parameter Min Max", 120);
    header.gaps = {9, 13};
    Line row = make_line(0, "Voltage 3.0 3.6", 133);
    row.gaps = {7, 11};
    lines.push_back(header);
    lines.push_back(row);
    lines.push_back(make_line(0, "Text after the table.", 160));

    const auto paragraphs = build_paragraphs(lines, 1.3f);
    REQUIRE(paragraphs.size() == 3);
    CHECK(paragraphs[1].kind == ParagraphKind::Rows);
    CHECK_EQ(paragraphs[1].text, std::string("Parameter | Min | Max\nVoltage | 3.0 | 3.6"));
}

// A table row with its cells at the given x positions (each character 5.5 pt wide).
Line row_line(std::initializer_list<std::pair<std::string_view, float>> cells, float baseline) {
    Line line;
    line.size = 10;
    line.baseline = baseline;
    line.y0 = baseline - 7;
    line.y1 = baseline + 2;
    for (const auto& [text, x] : cells) {
        if (line.text.empty()) {
            line.x0 = line.origin_x = x;
        } else {
            line.gaps.push_back(static_cast<std::uint32_t>(line.text.size()));
            line.gap_begin.push_back(line.x1);
            line.gap_end.push_back(x);
            line.text.push_back(' ');
        }
        line.text += text;
        line.x1 = x + 5.5f * static_cast<float>(text.size());
    }
    return line;
}

TEST(empty_table_cells_keep_their_place) {
    std::vector<Line> lines;
    lines.push_back(row_line({{"Parameter", 72}, {"Min", 250}, {"Typ", 320}, {"Max", 390}, {"Unit", 460}}, 100));
    lines.push_back(row_line({{"Logic supply voltage", 72}, {"3.0", 250}, {"3.3", 320}, {"3.6", 390}, {"V", 460}}, 113));
    lines.push_back(row_line({{"Output current", 72}, {"0", 250}, {"200", 390}, {"A", 460}}, 126));   // no typical value
    lines.push_back(row_line({{"Standby current", 72}, {"5", 320}, {"mA", 460}}, 139));               // only a typical value
    lines.push_back(row_line({{"1.5", 250}, {"2.5", 390}, {"W", 460}}, 152));                         // no name, no typical value

    const auto paragraphs = build_paragraphs(lines, 1.3f);
    REQUIRE(paragraphs.size() == 1);
    CHECK(paragraphs[0].kind == ParagraphKind::Rows);
    CHECK_EQ(paragraphs[0].text, std::string("Parameter | Min | Typ | Max | Unit\n"
                                             "Logic supply voltage | 3.0 | 3.3 | 3.6 | V\n"
                                             "Output current | 0 |  | 200 | A\n"
                                             "Standby current |  | 5 |  | mA\n"
                                             "| 1.5 |  | 2.5 | W"));
}

TEST(table_rows_are_left_alone_when_the_columns_are_not_certain) {
    std::vector<Line> lines;
    lines.push_back(row_line({{"Bits", 72}, {"Name", 250}, {"Access", 320}, {"Reset", 390}, {"Note", 460}}, 100));
    lines.push_back(row_line({{"0", 72}, {"READY", 250}, {"R", 320}, {"0", 390}, {"-", 460}}, 113));
    // One cell runs across three columns: nobody can say which column it belongs to.
    lines.push_back(row_line({{"7:1", 72}, {"Reserved for later use, do not write", 250}, {"-", 460}}, 126));

    const auto paragraphs = build_paragraphs(lines, 1.3f);
    REQUIRE(paragraphs.size() == 1);
    CHECK_EQ(paragraphs[0].text, std::string("Bits | Name | Access | Reset | Note\n"
                                             "0 | READY | R | 0 | -\n"
                                             "7:1 | Reserved for later use, do not write | -"));

    // Two cells of one row under the same column heading: neither may be lost.
    lines.back() = row_line({{"5", 250}, {"6", 258}, {"-", 460}}, 126);
    const auto doubled = build_paragraphs(lines, 1.3f);
    REQUIRE(doubled.size() == 1);
    CHECK(doubled[0].text.ends_with("\n5 | 6 | -"));

    // The drawing of a register: bit numbers at the edges of the fields, and
    // below them the field names in the middle of each field. The names do
    // not stand under the numbers, so no empty cells are made up.
    std::vector<Line> drawing;
    drawing.push_back(row_line({{"31", 72}, {"29", 150}, {"28", 172}, {"23", 290}, {"22", 312}, {"16", 430}}, 100));
    drawing.push_back(row_line({{"sbversion", 95}, {"reserved", 205}, {"sbaccess", 345}}, 113));
    drawing.push_back(row_line({{"3", 115}, {"6", 230}, {"7", 370}}, 126));
    const auto rows = build_paragraphs(drawing, 1.3f);
    REQUIRE(rows.size() == 1);
    CHECK_EQ(rows[0].text, std::string("31 | 29 | 28 | 23 | 22 | 16\n"
                                       "sbversion | reserved | sbaccess\n"
                                       "3 | 6 | 7"));
}

std::vector<Section> example_sections() {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "Cover text", 100));
    lines.push_back(make_line(1, "1 Intro", 100, 16, true));
    lines.push_back(make_line(1, "Intro body.", 130));
    lines.push_back(make_line(1, "1.1 Scope", 160, 13, true));
    lines.push_back(make_line(2, "Scope body on the next page.", 100));
    lines.push_back(make_line(3, "2 Limits", 100, 16, true));
    lines.push_back(make_line(3, "# not a Markdown heading", 130));
    std::vector<Heading> headings;
    headings.push_back({"1 Intro", "1", 1, 1, 1, 1, HeadingSource::Numbering});
    headings.push_back({"1.1 Scope", "1.1", 2, 1, 3, 1, HeadingSource::Numbering});
    headings.push_back({"2 Limits [draft]", "2", 1, 3, 5, 1, HeadingSource::Numbering});
    return build_sections(lines, headings);
}

TEST(sections_form_a_tree_with_page_ranges) {
    const std::vector<Section> sections = example_sections();
    REQUIRE(sections.size() == 4);
    CHECK_EQ(sections[0].title, std::string("Front matter"));
    CHECK(!sections[1].parent && sections[1].children == std::vector<std::size_t>({2}));
    CHECK(sections[2].parent && *sections[2].parent == 1);
    CHECK_EQ(sections[1].page_end, 1);
    CHECK_EQ(sections[1].span_page_end, 2);     // includes subsection 1.1
    CHECK_EQ(sections[2].page_start, 1);
    CHECK_EQ(sections[2].page_end, 2);
    REQUIRE(sections[2].paragraphs.size() == 1);
    CHECK_EQ(sections[2].paragraphs[0].text, std::string("Scope body on the next page."));
    CHECK_EQ(sections[2].paragraphs[0].page, 2);
}

TEST(document_without_headings_is_split_by_page) {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "Page one text.", 100));
    lines.push_back(make_line(2, "Page three text.", 100));
    const std::vector<Section> sections = build_sections(lines, {});
    REQUIRE(sections.size() == 2);
    CHECK_EQ(sections[1].title, std::string("Page 3"));
}

TEST(a_heading_never_takes_the_title_lines_of_the_one_before) {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "1 A long title that", 100, 16, true));
    lines.push_back(make_line(0, "wraps to a second line", 120, 16, true));
    lines.push_back(make_line(0, "Body text.", 150));
    std::vector<Heading> headings;
    headings.push_back({"1 A long title that wraps to a second line", "1", 1, 0, 0, 2, HeadingSource::Bookmark});
    headings.push_back({"Figure 1", "", 2, 0, 1, 0, HeadingSource::Bookmark});   // placed by position only
    const std::vector<Section> sections = build_sections(lines, headings);
    REQUIRE(sections.size() == 2);
    CHECK(sections[0].paragraphs.empty());
    REQUIRE(sections[1].paragraphs.size() == 1);
    CHECK_EQ(sections[1].paragraphs[0].text, std::string("Body text."));
}

// ---- bundle writer ----------------------------------------------------------

BundleInfo example_info() {
    BundleInfo info;
    info.meta.title = "Example \"Manual\"";
    info.meta.page_count = 4;
    info.source_file = "example.pdf";
    info.source_sha256 = std::string(64, 'a');
    info.source_bytes = 1234;
    info.generated_at = "2026-01-02T03:04:05Z";
    info.heading_source = HeadingSource::Numbering;
    info.body_size = 10;
    return info;
}

TEST(bundle_is_written_and_links_resolve) {
    const fs::path dir = fresh_temp_dir("bundle");
    std::vector<Section> sections = example_sections();
    const auto written = write_bundle(dir, sections, example_info());
    REQUIRE(written.has_value());
    CHECK_EQ(written->files, std::size_t{8});   // 4 sections + sections/index + document + index + log

    for (const char* name : {"index.md", "log.md", "document.md", "sections/index.md"}) {
        CHECK(fs::exists(dir / name));
    }
    CHECK(read_file(dir / "index.md").starts_with("---\nokf_version: \"0.2\"\n---\n"));
    CHECK(read_file(dir / "log.md").contains("## 2026-01-02\n"));
    CHECK(!read_file(dir / "sections/index.md").starts_with("---"));   // OKF: no frontmatter here

    const std::string document = read_file(dir / "document.md");
    std::size_t broken = count_broken_links(dir / "document.md", document);
    for (const Section& s : sections) {
        const std::string md = read_file(dir / s.file);
        CHECK(md.starts_with("---\ntype: Section\n"));
        CHECK(md.contains("htpd:\n  version: \"0.1\"\n  node: section\n"));
        broken += count_broken_links(dir / s.file, md);
    }
    CHECK_EQ(broken, std::size_t{0});

    const std::string scope = read_file(dir / sections[2].file);
    // The page of the source: once as what the file describes, once as where the text comes from.
    CHECK(scope.contains("\nresource: \"example.pdf#page=2\"\n"));
    CHECK(scope.contains("sources:\n  - id: source-pdf\n    resource: \"example.pdf#page=2\"\n"
                         "    title: \"Example \\\"Manual\\\", pages 2-3\"\n"));
    CHECK(document.contains("\nresource: \"example.pdf\"\n"));
    CHECK(scope.contains("  page_start: 2\n  page_end: 3\n"));
    CHECK(scope.contains("<!-- page 3 -->\n\nScope body on the next page.\n"));
    CHECK(scope.contains("> [1 Intro](0002-1-intro.md) > **1.1 Scope**"));
    // The connections between nodes are also in the frontmatter, for programs.
    CHECK(scope.contains("  id: \"sections/0003-1-1-scope\"\n"));
    CHECK(scope.contains("  parent: \"sections/0002-1-intro\"\n  children: []\n"
                         "  previous: \"sections/0002-1-intro\"\n  next: \"sections/0004-2-limits-draft\"\n"));
    const std::string intro = read_file(dir / sections[1].file);
    CHECK(intro.contains("  parent: \"document\"\n  children:\n    - \"sections/0003-1-1-scope\"\n"));
    CHECK(document.contains("  children:\n    - \"sections/0001-front-matter\"\n    - \"sections/0002-1-intro\"\n"
                            "    - \"sections/0004-2-limits-draft\"\n---\n"));

    const std::string limits = read_file(dir / sections[3].file);
    CHECK(limits.contains("title: \"2 Limits [draft]\"\n"));
    CHECK(limits.contains("\n\\# not a Markdown heading\n"));
    CHECK(document.contains("[2 Limits \\[draft\\]](sections/0004-2-limits-draft.md)"));
    CHECK(document.contains("title: \"Example \\\"Manual\\\"\"\n"));
}

// Reads the body of a section file the way docs/FORMAT.md tells a program to:
// a block of literal text runs from a line of three or more backticks to the
// next line with exactly the same backticks; headings and page marks count
// only outside such blocks.
struct ParsedBody {
    std::vector<std::string> headings;
    std::vector<int> pages;
    std::vector<std::string> blocks;   // the literal text of each block
    bool block_left_open = false;
};

ParsedBody parse_body(const std::string& markdown) {
    ParsedBody out;
    const std::size_t end_of_frontmatter = markdown.find("\n---\n", 4);
    if (end_of_frontmatter == std::string::npos) return out;
    std::istringstream in(markdown.substr(end_of_frontmatter + 5));
    std::string line, fence;
    while (std::getline(in, line)) {
        if (!fence.empty()) {
            if (line == fence) fence.clear();
            else out.blocks.back() += line + "\n";
            continue;
        }
        if (line.starts_with("```")) {
            fence = line.substr(0, line.find_first_not_of('`'));
            out.blocks.emplace_back();
        } else if (line.starts_with("# ")) {
            out.headings.push_back(line.substr(2));
        } else if (line.starts_with("<!-- page ") && line.ends_with(" -->")) {
            out.pages.push_back(std::atoi(line.c_str() + 10));
        }
    }
    out.block_left_open = !fence.empty();
    return out;
}

TEST(text_from_the_pdf_cannot_change_the_structure_of_a_file) {
    std::vector<Section> sections(1);
    sections[0].title = "Odd text";
    sections[0].page_start = sections[0].page_end = sections[0].span_page_end = 2;
    const auto add = [&](ParagraphKind kind, std::string text) {
        sections[0].paragraphs.push_back({2, kind, std::move(text)});
    };
    // Paragraphs that look like Markdown structure ...
    add(ParagraphKind::Text, "# Neighbours");
    add(ParagraphKind::Text, "> a quotation mark at the start");
    add(ParagraphKind::Text, "``` three backticks at the start");
    add(ParagraphKind::Text, "<!-- page 99 -->");
    add(ParagraphKind::Text, "[^source-pdf]: another file");
    add(ParagraphKind::Text, "---");
    add(ParagraphKind::Text, "\\# starts with a backslash in the PDF");
    // ... and paragraphs that must stay exactly as they are.
    add(ParagraphKind::Text, "- a list item stays a list item");
    add(ParagraphKind::Text, "x < 5, a_b_c and *p stay as they are");
    // Literal blocks that contain fences and headings themselves.
    add(ParagraphKind::Code, "echo ```\n# not a heading\n````\n<!-- page 98 -->");
    add(ParagraphKind::Rows, "a | b\n```\nc | d");

    BundleInfo info = example_info();
    info.source_file = "odd`name.pdf";
    const fs::path dir = fresh_temp_dir("hostile");
    REQUIRE(write_bundle(dir, sections, info).has_value());
    const std::string md = read_file(dir / sections[0].file);

    for (const char* expected : {"\n\\# Neighbours\n", "\n\\> a quotation mark at the start\n",
                                 "\n\\``` three backticks at the start\n", "\n\\<!-- page 99 -->\n",
                                 "\n\\[^source-pdf]: another file\n", "\n\\---\n",
                                 "\n\\\\# starts with a backslash in the PDF\n",
                                 "\n- a list item stays a list item\n",
                                 "\nx < 5, a_b_c and *p stay as they are\n",
                                 "Source: `` odd`name.pdf ``, page 3."}) {
        if (!md.contains(expected)) std::fprintf(stderr, "    missing: %s\n", expected);
        CHECK(md.contains(expected));
    }

    // A program that follows the reading rules sees exactly the real structure.
    const ParsedBody body = parse_body(md);
    const std::vector<std::string> headings = {"Location", "Text", "Neighbours"};
    CHECK(body.headings == headings);
    CHECK(body.pages == std::vector<int>{3});
    CHECK(!body.block_left_open);
    REQUIRE(body.blocks.size() == 2);
    CHECK_EQ(body.blocks[0], std::string("echo ```\n# not a heading\n````\n<!-- page 98 -->\n"));
    CHECK_EQ(body.blocks[1], std::string("a | b\n```\nc | d\n"));
}

TEST(a_long_description_is_cut_between_characters) {
    std::vector<Section> sections(1);
    sections[0].title = "Long";
    std::string degrees = "a";                       // one byte, then two-byte characters
    for (int i = 0; i < 200; ++i) degrees += "\xC2\xB0";
    sections[0].paragraphs.push_back({0, ParagraphKind::Text, degrees});
    const fs::path dir = fresh_temp_dir("snippet");
    REQUIRE(write_bundle(dir, sections, example_info()).has_value());
    const std::string md = read_file(dir / sections[0].file);
    std::string expected = "description: \"a";
    for (int i = 0; i < 69; ++i) expected += "\xC2\xB0";
    expected += " ...\"\n";
    CHECK(md.contains(expected));
    CHECK(!md.contains("\xEF\xBF\xBD"));            // no replacement character: nothing was cut in half
}

TEST(the_contents_list_is_nested_by_the_tree_not_by_the_heading_level) {
    std::vector<Line> lines;
    lines.push_back(make_line(0, "Deep start", 100, 12, true));
    lines.push_back(make_line(0, "Chapter", 130, 16, true));
    lines.push_back(make_line(0, "Deep again", 160, 12, true));
    std::vector<Heading> headings;
    headings.push_back({"Deep start", "", 3, 0, 0, 1, HeadingSource::Bookmark});   // level 3 comes first
    headings.push_back({"Chapter", "", 1, 0, 1, 1, HeadingSource::Bookmark});
    headings.push_back({"Deep again", "", 3, 0, 2, 1, HeadingSource::Bookmark});   // level 1 -> level 3
    std::vector<Section> sections = build_sections(lines, headings);
    const fs::path dir = fresh_temp_dir("depth");
    REQUIRE(write_bundle(dir, sections, example_info()).has_value());
    CHECK(read_file(dir / "document.md")
              .contains("# Contents\n\n"
                        "* [Deep start](sections/0001-deep-start.md) - page 1\n"
                        "* [Chapter](sections/0002-chapter.md) - page 1\n"
                        "  * [Deep again](sections/0003-deep-again.md) - page 1\n\n"));
}

TEST(the_contents_of_a_very_large_document_stay_short) {
    // 3 chapters, each with 150 sections, each of those with one subsection: 903 sections.
    std::vector<Section> sections;
    const auto add = [&](std::string title, int level, std::optional<std::size_t> parent) {
        Section s;
        s.order = sections.size();
        s.title = std::move(title);
        s.level = level;
        s.parent = parent;
        if (parent) sections[*parent].children.push_back(s.order);
        sections.push_back(std::move(s));
        return sections.size() - 1;
    };
    for (int c = 1; c <= 3; ++c) {
        const std::size_t chapter = add("Chapter " + std::to_string(c), 1, std::nullopt);
        for (int k = 1; k <= 150; ++k) {
            const std::size_t section = add(std::to_string(c) + "." + std::to_string(k) + " Section", 2, chapter);
            add(std::to_string(c) + "." + std::to_string(k) + ".1 Detail", 3, section);
        }
    }
    const fs::path dir = fresh_temp_dir("large");
    REQUIRE(write_bundle(dir, sections, example_info()).has_value());
    const std::string document = read_file(dir / "document.md");

    // Chapters alone are 3 lines, with the sections 453: only the chapters are listed.
    CHECK(document.contains("# Contents\n\n"
                            "* [Chapter 1](sections/0001-chapter-1.md) - page 1 (+300 sections below)\n"
                            "* [Chapter 2](sections/0302-chapter-2.md) - page 1 (+300 sections below)\n"
                            "* [Chapter 3](sections/0603-chapter-3.md) - page 1 (+300 sections below)\n\n"
                            "This list shows 3 of the 903 sections: the upper level of headings."));
    // Nothing is lost: the chapter file lists its sections, and the index lists them all.
    const std::string chapter = read_file(dir / "sections/0001-chapter-1.md");
    CHECK(chapter.contains("# Subsections\n\n* [1.1 Section](0002-1-1-section.md) - page 1\n"));
    const std::string index = read_file(dir / "sections/index.md");
    CHECK_EQ(static_cast<std::size_t>(std::ranges::count(index, '\n')), std::size_t{905});   // heading, empty line, 903 entries

    // With 100 sections per chapter, two levels fit: 3 + 300 lines are over the limit, so one level again;
    // but 3 chapters of 99 sections (300 lines in all) are listed with their sections.
    sections.clear();
    for (int c = 1; c <= 3; ++c) {
        const std::size_t chapter_index = add("Chapter " + std::to_string(c), 1, std::nullopt);
        for (int k = 1; k <= 99; ++k) {
            const std::size_t section = add(std::to_string(c) + "." + std::to_string(k) + " Section", 2, chapter_index);
            add(std::to_string(c) + "." + std::to_string(k) + ".1 Detail", 3, section);
        }
    }
    const fs::path second = fresh_temp_dir("large2");
    REQUIRE(write_bundle(second, sections, example_info()).has_value());
    const std::string two_levels = read_file(second / "document.md");
    CHECK(two_levels.contains("* [Chapter 1](sections/0001-chapter-1.md) - page 1\n"
                              "  * [1.1 Section](sections/0002-1-1-section.md) - page 1 (+1 section below)\n"));
    CHECK(two_levels.contains("This list shows 300 of the 597 sections: the upper 2 levels of headings."));
    CHECK(!two_levels.contains("1.1.1 Detail"));
}

TEST(bundle_writer_protects_existing_folders) {
    const fs::path dir = fresh_temp_dir("protect");
    std::vector<Section> sections = example_sections();
    CHECK(write_bundle(dir, sections, example_info()).has_value());
    CHECK(!write_bundle(dir, sections, example_info()).has_value());            // not empty
    WriteOptions force;
    force.force = true;
    CHECK(write_bundle(dir, sections, example_info(), force).has_value());      // replace own bundle

    const fs::path other = fresh_temp_dir("not_a_bundle");
    fs::create_directories(other);
    std::ofstream(other / "notes.txt") << "keep me";
    CHECK(!write_bundle(other, sections, example_info(), force).has_value());   // never someone else's
    CHECK(fs::exists(other / "notes.txt"));
}

// ---- end to end (needs the PDF library and tests/data) -----------------------

std::string all_section_text(const fs::path& dir) {
    std::string all;
    for (const auto& entry : fs::directory_iterator(dir / "sections")) all += read_file(entry.path());
    return all;
}

std::vector<std::string> texts(const std::vector<Line>& lines) {
    std::vector<std::string> out;
    for (const Line& line : lines) out.push_back(line.text);
    return out;
}

TEST(reader_gives_upright_text_in_reading_order) {
    const auto doc = PdfDocument::open(fs::path(HTPD_TEST_DATA) / "lowlevel.pdf");
    REQUIRE(doc.has_value());
    REQUIRE(doc->page_count() == 4);

    // Page 1: every special case on one page. The vertical label and the
    // diagonal "DRAFT" watermark are left out, letter by letter.
    const auto first = doc->read_page(0);
    REQUIRE(first.has_value());
    CHECK_EQ(first->skipped_rotated, std::size_t{22});
    const std::vector<Line> lines = build_lines(*first);
    const std::vector<std::string> expected = {
        "Plain twelve point line.",         //
        "Scaled heading eighteen",          // font size 1, scaled 18x by the text matrix
        "Kerned words here",                // word gaps made by kerning only
        "Area is 5 m2 in total.",           // superscript 2
        "Slanted synthetic italic line.",   // sheared text matrix
        "1111 and 11.11 stay together."};
    REQUIRE(texts(lines) == expected);
    CHECK(lines[0].size == 12.0f && !lines[0].bold);
    CHECK(lines[1].size == 18.0f && lines[1].bold);
    CHECK(lines[0].baseline < lines[1].baseline);   // y grows downwards
    CHECK(lines[0].font != 0 && lines[0].font != lines[1].font);

    // Page 2 has /Rotate 90: its text must come out upright, top to bottom.
    const auto second = doc->read_page(1);
    REQUIRE(second.has_value());
    CHECK_EQ(second->skipped_rotated, std::size_t{0});
    const std::vector<Line> rotated = build_lines(*second);
    const std::vector<std::string> expected_rotated = {"Rotated page heading", "First line on the rotated page.",
                                                       "Second line on the rotated page."};
    REQUIRE(texts(rotated) == expected_rotated);
    CHECK(rotated[0].baseline < rotated[1].baseline && rotated[1].baseline < rotated[2].baseline);
    CHECK(rotated[0].x1 - rotated[0].x0 > 5 * (rotated[0].y1 - rotated[0].y0));   // wide, not tall

    CHECK(!doc->read_page(7).has_value());   // no such page: an error, not a crash

    // Page 4 holds a listing in Courier. Fonts are measured while reading.
    const auto fourth = doc->read_page(3);
    REQUIRE(fourth.has_value());
    const std::vector<Line> listing = build_lines(*fourth);
    REQUIRE(listing.size() >= 3);
    const std::vector<FontFacts> fonts = doc->fonts();
    REQUIRE(listing[0].font < fonts.size() && listing[1].font < fonts.size());
    CHECK(!fonts[listing[0].font].monospace);                 // "The listing below ..." in Helvetica
    CHECK(fonts[listing[1].font].monospace);                  // "int main(void) {" in Courier
    CHECK(std::fabs(fonts[listing[1].font].advance - 0.6f) < 0.01f);
}

TEST(monospace_is_measured_not_taken_from_the_font_name) {
    const auto doc = PdfDocument::open(fs::path(HTPD_TEST_DATA) / "fonts.pdf");
    REQUIRE(doc.has_value());
    const auto page = doc->read_page(0);
    REQUIRE(page.has_value());
    const std::vector<Line> lines = build_lines(*page);
    REQUIRE(lines.size() == 9);
    const std::vector<FontFacts> fonts = doc->fonts();
    REQUIRE(lines[1].font != 0 && lines[1].font < fonts.size());
    REQUIRE(lines[6].font != 0 && lines[6].font < fonts.size());
    // "ListingSans": no fixed-pitch flag and nothing in the name, but every character is 0.5 em wide.
    CHECK(fonts[lines[1].font].monospace);
    CHECK(std::fabs(fonts[lines[1].font].advance - 0.5f) < 0.005f);
    // "MonotypeCorsiva": fixed-pitch flag set and "mono" in the name, but the widths differ.
    CHECK(!fonts[lines[6].font].monospace);
    CHECK(!fonts[lines[0].font].monospace);   // Helvetica

    ConvertOptions options;
    options.input = fs::path(HTPD_TEST_DATA) / "fonts.pdf";
    options.output_dir = fresh_temp_dir("e2e_fonts");
    options.headings = HeadingMode::None;
    REQUIRE(convert(options).has_value());
    const std::string text = all_section_text(options.output_dir);
    // The listing keeps its columns: the indentation and the aligned operands and comment.
    CHECK(text.contains("```\n"
                        "start:\n"
                        "    mov   r0, #0        ; counter\n"
                        "loop:\n"
                        "    add   r0, r0, #1\n"
                        "    b     loop\n"
                        "```\n"));
    CHECK(text.contains("\n\nThis note is set in a font that claims to be fixed pitch, but its letters have "
                        "different widths, so it is ordinary text and its lines are joined into one paragraph.\n"));
}

TEST(end_to_end_with_bookmarks) {
    ConvertOptions options;
    options.input = fs::path(HTPD_TEST_DATA) / "sample.pdf";
    options.output_dir = fresh_temp_dir("e2e_bookmarks");
    const auto report = convert(options);
    if (!report) std::fprintf(stderr, "    %s\n", report.error().message.c_str());
    REQUIRE(report.has_value());
    CHECK_EQ(report->pages, 5);
    CHECK(report->heading_source == HeadingSource::Bookmark);
    CHECK_EQ(report->bookmarks_total, std::size_t{13});
    CHECK_EQ(report->bookmarks_located, std::size_t{13});
    CHECK_EQ(report->sections, std::size_t{14});          // front matter + 13 headings
    CHECK_EQ(report->furniture.removed_lines, std::size_t{8});

    const fs::path dir = options.output_dir;
    const std::string thermal = read_file(dir / "sections/0011-3-1-thermal-derating.md");
    // The condition AND its exception must survive, with the page it came from.
    CHECK(thermal.contains("<!-- page 3 -->\n\nIf the temperature exceeds 85 \xC2\xB0" "C, the controller shall "
                           "reduce the charging current unless emergency mode is enabled.\n"));
    CHECK(thermal.contains("  number: \"3.1\"\n  level: 2\n  heading_from: bookmark\n  page_start: 3\n  page_end: 4\n"));
    CHECK(thermal.contains("  parent: \"sections/0010-3-charging-sequence\"\n"));
    CHECK(thermal.contains("> [3 Charging sequence](0010-3-charging-sequence.md) > **3.1 Thermal derating**"));

    const std::string limits = read_file(dir / "sections/0008-2-1-1-limits.md");
    CHECK(limits.contains("```text\nParameter | Min | Typ | Max | Unit\nLogic supply voltage | 3.0 | 3.3 | 3.6 | V\n"));
    // The row without a typical value keeps its maximum under "Max".
    CHECK(limits.contains("\nOutput current | 0 |  | 200 | A\n```\n"));

    const std::string terms = read_file(dir / "sections/0005-1-2-terms.md");
    CHECK(terms.contains("The term inter-rupt latency means"));

    const std::string everything = all_section_text(dir);
    CHECK(!everything.contains("ACME Confidential"));          // footer gone from every section
    CHECK(!everything.contains("\n3.1 Thermal derating\n"));   // heading line is not repeated as body
    CHECK(read_file(dir / "document.md").contains("* Removed 8 running header/footer lines (2 patterns):"));
}

TEST(end_to_end_without_bookmarks) {
    ConvertOptions options;
    options.input = fs::path(HTPD_TEST_DATA) / "sample_nobookmarks.pdf";
    options.output_dir = fresh_temp_dir("e2e_text");
    const auto report = convert(options);
    REQUIRE(report.has_value());
    CHECK(report->heading_source == HeadingSource::Numbering);
    CHECK_EQ(report->sections, std::size_t{14});
    CHECK(fs::exists(options.output_dir / "sections/0011-3-1-thermal-derating.md"));
    CHECK(fs::exists(options.output_dir / "sections/0014-a-1-status-register.md"));
}

TEST(end_to_end_rotation_scaling_and_spacing) {
    const auto report = inspect(fs::path(HTPD_TEST_DATA) / "lowlevel.pdf");
    REQUIRE(report.has_value());
    CHECK_EQ(report->meta.page_count, 4);
    CHECK_EQ(report->skipped_rotated, std::size_t{22});   // "VERTICAL SIDE LABEL" + "DRAFT"
    const std::vector<std::string> found = titles(report->headings);
    // Font size 1 scaled 18x by the text matrix is seen as 18 pt; text on a /Rotate 90 page reads upright.
    CHECK(std::ranges::find(found, "Scaled heading eighteen") != found.end());
    CHECK(std::ranges::find(found, "Rotated page heading") != found.end());
    // Without numbers the level follows the font size: 18 pt, 14 pt, 12 pt.
    CHECK(report->heading_source == HeadingSource::FontSize);
    for (const Heading& h : report->headings) {
        if (h.title == "Scaled heading eighteen") CHECK_EQ(h.level, 1);
        if (h.title == "Rotated page heading") CHECK_EQ(h.level, 2);
        if (h.title == "Kerned words here") CHECK_EQ(h.level, 3);
    }

    ConvertOptions options;
    options.input = fs::path(HTPD_TEST_DATA) / "lowlevel.pdf";
    options.output_dir = fresh_temp_dir("e2e_lowlevel");
    options.headings = HeadingMode::None;
    REQUIRE(convert(options).has_value());
    const std::string text = all_section_text(options.output_dir);
    CHECK(text.contains("Kerned words here"));                 // spaces made only by kerning
    CHECK(text.contains("Area is 5 m2 in total."));            // superscript stays in its line
    CHECK(text.contains("1111 and 11.11 stay together."));
    CHECK(text.contains("Slanted synthetic italic line."));
    CHECK(text.contains("Left column line 6 continues here Right column line 1 continues here"));
    CHECK(!text.contains("DRAFT"));
    // A listing keeps its lines, its indentation, its inner spacing and its empty line.
    CHECK(text.contains("The listing below shows the main loop.\n\n"
                        "```\n"
                        "int main(void) {\n"
                        "    int state = 0;\n"
                        "    if (state == 0) {\n"
                        "        start();   // begin\n"
                        "    }\n"
                        "\n"
                        "    return state;\n"
                        "}\n"
                        "```\n"));
    // One monospace line inside a sentence is a term, not a listing.
    CHECK(text.contains("\n\nSet the file name in charger.conf before the first start.\n"));
}

TEST(errors_are_reported_not_thrown) {
    ConvertOptions options;
    options.input = fs::path(HTPD_TEST_DATA) / "does_not_exist.pdf";
    options.output_dir = fresh_temp_dir("e2e_error");
    const auto missing = convert(options);
    CHECK(!missing.has_value());
    CHECK(!fs::exists(options.output_dir));

    const fs::path not_pdf = fresh_temp_dir("not_pdf.txt");
    fs::create_directories(not_pdf.parent_path());
    std::ofstream(not_pdf) << "this is not a PDF";
    CHECK(!inspect(not_pdf).has_value());
}

}  // namespace

int main(int argc, char** argv) {
    const std::string_view filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (const TestCase& test : registry()) {
        if (!filter.empty() && !std::string_view(test.name).contains(filter)) continue;
        const int before = g_failures;
        test.run();
        ++ran;
        std::printf("%s %s\n", g_failures == before ? "ok  " : "FAIL", test.name);
    }
    std::printf("%d tests, %d failed checks\n", ran, g_failures);
    return g_failures == 0 ? 0 : 1;
}
