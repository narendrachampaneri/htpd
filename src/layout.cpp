#include "htpd/layout.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "htpd/text.hpp"

namespace htpd {

namespace {

// Latin ligature glyphs are written out so that search and matching work.
const char* ligature_expansion(char32_t cp) {
    switch (cp) {
        case 0xFB00: return "ff";
        case 0xFB01: return "fi";
        case 0xFB02: return "fl";
        case 0xFB03: return "ffi";
        case 0xFB04: return "ffl";
        case 0xFB05: return "st";
        case 0xFB06: return "st";
        default:     return nullptr;
    }
}

// The raised (superscript) and lowered (subscript) form of a character, or 0.
// Only digits, signs and brackets (and a raised n or i): enough for exponents
// and indices (2³¹, m², s⁻¹, H₂O) without guessing about letters.
char32_t raised_form(char32_t c) {
    switch (c) {
        case U'0': return 0x2070;
        case U'1': return 0x00B9;
        case U'2': return 0x00B2;
        case U'3': return 0x00B3;
        case U'+': return 0x207A;
        case U'-': case 0x2212: return 0x207B;
        case U'=': return 0x207C;
        case U'(': return 0x207D;
        case U')': return 0x207E;
        case U'n': return 0x207F;
        case U'i': return 0x2071;
        default: return c >= U'4' && c <= U'9' ? 0x2074 + (c - U'4') : 0;
    }
}

char32_t lowered_form(char32_t c) {
    switch (c) {
        case U'+': return 0x208A;
        case U'-': case 0x2212: return 0x208B;
        case U'=': return 0x208C;
        case U'(': return 0x208D;
        case U')': return 0x208E;
        default: return c >= U'0' && c <= U'9' ? 0x2080 + (c - U'0') : 0;
    }
}

// Letters and signs of the Indian scripts; not the danda (। ॥), which ends a sentence.
constexpr bool is_indic(char32_t c) { return c >= 0x0900 && c <= 0x0DFF && c != 0x0964 && c != 0x0965; }

// Collects glyphs into one Line.
class LineBuilder {
public:
    LineBuilder(int page, const LayoutOptions& options) : options_(&options) { line_.page = page; }

    bool empty() const { return glyph_count_ == 0; }

    // True when `g` continues the line being built.
    bool accepts(const Glyph& g) const {
        const float ref = std::max(main_size_, g.size);
        const bool same_baseline =
            std::fabs(g.baseline - line_.baseline) <= options_->baseline_tolerance * ref;
        const bool jumped_back = g.x0 < last_x1_ - 2.0f * ref;
        if (jumped_back) return false;
        // Tagged PDF: text of another element, or page furniture next to content,
        // never shares a line (both are 0/false in a PDF without tags).
        if (g.tag != line_.tag || g.artifact != line_.artifact) return false;
        if (same_baseline) return true;
        // A raised or lowered mark set higher than the tolerance (a ® or ™ in
        // "Cortex®-M33", an exponent, an index): smaller than the line, right
        // after the last glyph, and touching the line's height.
        const bool smaller = g.size <= 0.85f * main_size_;
        const bool adjacent = g.x0 >= last_x1_ - 0.5f * main_size_ && g.x0 <= last_x1_ + 0.5f * main_size_;
        const bool touches = g.y1 > line_.y0 && g.y0 < line_.y1;
        const bool near = std::fabs(g.baseline - line_.baseline) <= 0.75f * main_size_;
        return smaller && adjacent && touches && near;
    }

    void add(const Glyph& g, bool space_before) {
        // A smaller glyph set clearly higher or lower than the line: part of a
        // raised or lowered group (an exponent, an index).
        int script = 0;
        if (!empty() && g.size <= 0.85f * main_size_) {
            if (g.baseline <= line_.baseline - 0.25f * main_size_) script = 1;
            else if (g.baseline >= line_.baseline + 0.12f * main_size_) script = 2;
        }
        if (empty()) {
            line_.x0 = g.x0;
            line_.y0 = g.y0;
            line_.x1 = g.x1;
            line_.y1 = g.y1;
            line_.baseline = g.baseline;
            line_.origin_x = g.origin_x;
            line_.tag = g.tag;
            line_.artifact = g.artifact;
            main_size_ = g.size;
        } else {
            const float ref = std::max(main_size_, g.size);
            const float gap = g.x0 - last_x1_;
            const bool column_gap = gap > options_->column_gap * ref;
            // Indian scripts (Devanagari to Malayalam): letters of a word touch or
            // overlap, and some PDFs give no space between words: a gap of a
            // fifth of the font size is a space there.
            // (Next to a span its place is only roughly known: a wider gap is needed.)
            const float indic_space = (g.span || last_span_) ? 0.22f : 0.18f;
            const bool indic_gap = is_indic(g.cp) && is_indic(last_cp_) && gap > indic_space * ref;
            const bool spaced = column_gap || space_before || indic_gap || gap > options_->forced_space_gap * ref;
            if (spaced || script != pending_kind_) flush_pending();
            if (column_gap) {
                line_.gaps.push_back(static_cast<std::uint32_t>(line_.text.size()));
                line_.gap_begin.push_back(last_x1_);
                line_.gap_end.push_back(g.x0);
            }
            if (spaced) {
                line_.text.push_back(' ');
                line_.space_spans.push_back(std::max(0.0f, g.origin_x - last_origin_x_));
                line_.space_begin.push_back(last_x1_);
                line_.space_end.push_back(g.x0);
            }
            line_.x0 = std::min(line_.x0, g.x0);
            line_.y0 = std::min(line_.y0, g.y0);
            line_.x1 = std::max(line_.x1, g.x1);
            line_.y1 = std::max(line_.y1, g.y1);
            // The baseline of the line is the baseline of its largest font,
            // so a leading superscript does not define it.
            if (g.size > main_size_ * 1.05f) {
                main_size_ = g.size;
                line_.baseline = g.baseline;
            }
        }
        if (script != 0) {
            pending_.push_back(g.cp);
            pending_kind_ = script;
        } else {
            append(g.cp);
        }
        last_x1_ = g.x1;
        last_origin_x_ = g.origin_x;
        last_cp_ = g.cp;
        last_span_ = g.span;
        ++glyph_count_;
        if (g.bold) ++bold_count_;
        count(sizes_, static_cast<int>(std::lround(g.size * 4.0f)));   // quarter points
        count(fonts_, g.font);
        count(colors_, static_cast<int>(g.color));
    }

    Line finish() {
        flush_pending();
        line_.size = static_cast<float>(most_common(sizes_)) / 4.0f;
        line_.font = static_cast<std::uint16_t>(most_common(fonts_));
        line_.color = static_cast<std::uint32_t>(most_common(colors_));
        line_.bold = bold_count_ * 10 >= glyph_count_ * 6;   // at least 60 % bold
        return std::move(line_);
    }

private:
    void append(char32_t cp) {
        if (const char* expanded = ligature_expansion(cp)) {
            line_.text += expanded;
        } else {
            text::append_utf8(line_.text, cp);
        }
    }

    // A raised or lowered group is written in raised or lowered characters
    // when every character of it has such a form (2³¹, H₂O); otherwise as it
    // stands (a ™ written as "TM", the "th" of "4th").
    void flush_pending() {
        if (pending_.empty()) return;
        const auto form = pending_kind_ == 1 ? raised_form : lowered_form;
        // ... and when it holds a digit (or a raised n or i: xⁿ): a sign alone in
        // the small font of the indices is an operator ("tSCKD - tSCKDQS").
        const bool all = std::ranges::all_of(pending_, [&](char32_t c) { return form(c) != 0; }) &&
                         std::ranges::any_of(pending_, [&](char32_t c) {
                             return (c >= U'0' && c <= U'9') || (pending_kind_ == 1 && (c == U'n' || c == U'i'));
                         });
        for (const char32_t c : pending_) append(all ? form(c) : c);
        pending_.clear();
        pending_kind_ = 0;
    }

    std::u32string pending_;   // glyphs of a raised or lowered group, not yet written
    int pending_kind_ = 0;     // 1 raised, 2 lowered

    using Tally = std::vector<std::pair<int, std::size_t>>;   // value -> number of glyphs

    static void count(Tally& tally, int value) {
        for (auto& [v, n] : tally) {
            if (v == value) {
                ++n;
                return;
            }
        }
        tally.emplace_back(value, 1);
    }

    // The value most glyphs have; the larger value wins a tie.
    static int most_common(const Tally& tally) {
        int best = 0;
        std::size_t best_count = 0;
        for (const auto& [v, n] : tally) {
            if (n > best_count || (n == best_count && v > best)) {
                best = v;
                best_count = n;
            }
        }
        return best;
    }

    const LayoutOptions* options_;
    Line line_;
    float main_size_ = 0;
    float last_x1_ = 0;
    float last_origin_x_ = 0;
    char32_t last_cp_ = 0;
    bool last_span_ = false;
    std::size_t glyph_count_ = 0;
    std::size_t bold_count_ = 0;
    Tally sizes_;
    Tally fonts_;
    Tally colors_;
};

// PDFs normally store text in reading order. Two kinds of pages do not, and
// only those are re-ordered (a page that is merely untidy in one place, such
// as a diagram with labels, is better left as it is stored):
//   - pages stored from the bottom to the top: almost every step goes up;
//   - pages stored in no order at all: many jumps far back up the page.
// A step to a column further right is normal and never counts.
bool order_is_scrambled(const std::vector<Line>& lines, bool turned) {
    // A page that was turned to read sideways text has no reliable stored
    // order: there even two lines are enough to judge.
    if (lines.size() < (turned ? 2u : 8u)) return false;
    std::size_t steps_up = 0;   // to a line above (less than 0.8 of a line is a subscript or a fraction)
    std::size_t jumps = 0;      // far back up the page
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const Line& prev = lines[i - 1];
        const Line& cur = lines[i];
        const bool new_column = cur.x0 > prev.x1 - 0.5f * prev.size;
        if (new_column) continue;
        const float up = prev.baseline - cur.baseline;
        const float size = std::max(prev.size, cur.size);
        if (up > 0.8f * size) ++steps_up;
        if (up > 1.5f * size) ++jumps;
    }
    const std::size_t steps = lines.size() - 1;
    if (steps_up * 10 > steps * 7) return true;
    return jumps > 2 && jumps * 100 > lines.size() * 15;
}

}  // namespace

std::vector<Line> build_lines(const PageText& page, const LayoutOptions& options) {
    std::vector<Line> lines;
    LineBuilder builder(page.index, options);
    bool pending_space = false;

    const auto flush = [&] {
        if (!builder.empty()) lines.push_back(builder.finish());
        builder = LineBuilder(page.index, options);
    };

    for (const Glyph& g : page.glyphs) {
        if (g.is_space) {
            pending_space = true;
            continue;
        }
        if (!builder.empty() && !builder.accepts(g)) flush();
        builder.add(g, pending_space);
        pending_space = false;
    }
    flush();

    if (order_is_scrambled(lines, page.turned != 0)) {
        // Fall back to plain top-to-bottom, left-to-right order.
        std::ranges::stable_sort(lines, {}, &Line::baseline);
        for (std::size_t begin = 0; begin < lines.size();) {
            std::size_t end = begin + 1;
            while (end < lines.size() &&
                   lines[end].baseline - lines[begin].baseline <= 0.5f * lines[begin].size) {
                ++end;
            }
            std::ranges::stable_sort(lines.begin() + static_cast<std::ptrdiff_t>(begin),
                                     lines.begin() + static_cast<std::ptrdiff_t>(end), {}, &Line::x0);
            begin = end;
        }
    }
    return lines;
}

}  // namespace htpd
