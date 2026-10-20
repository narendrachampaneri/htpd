#include "htpd/links.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace htpd {

namespace {

// One space between words, none at the ends.
std::string tidy(std::string_view s) {
    std::string out;
    bool space = false;
    for (const char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space = !out.empty();
            continue;
        }
        if (space) out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    return out;
}

}  // namespace

void attach_links(std::vector<Section>& sections, std::span<const Line> lines,
                  std::span<const std::vector<PageLink>> page_links, std::span<const Affine> page_transforms) {
    if (sections.empty() || lines.empty()) return;

    // The section of each line: sections own the lines from their first line
    // to the first line of the next one.
    std::vector<std::size_t> section_of(lines.size(), 0);
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const std::size_t begin = sections[i].first_line;
        const std::size_t end = i + 1 < sections.size() ? sections[i + 1].first_line : lines.size();
        for (std::size_t k = begin; k < std::min(end, lines.size()); ++k) section_of[k] = i;
    }
    // The lines of each page, as a range of indices.
    int last_page = 0;
    for (const Line& l : lines) last_page = std::max(last_page, l.page);
    std::vector<std::pair<std::size_t, std::size_t>> page_range(static_cast<std::size_t>(last_page) + 1,
                                                                {lines.size(), lines.size()});
    for (std::size_t k = 0; k < lines.size(); ++k) {
        auto& [first, end] = page_range[static_cast<std::size_t>(lines[k].page)];
        if (first == lines.size()) first = k;
        end = k + 1;
    }
    const auto lines_of = [&](int page) -> std::pair<std::size_t, std::size_t> {
        if (page < 0 || page > last_page) return {0, 0};
        return page_range[static_cast<std::size_t>(page)];
    };

    for (std::size_t p = 0; p < page_links.size(); ++p) {
        for (const PageLink& link : page_links[p]) {
            // Where the link stands: the first line of its page under its box.
            const auto [from, from_end] = lines_of(static_cast<int>(p));
            std::size_t source = sections.size();
            for (std::size_t k = from; k < from_end; ++k) {
                const Line& l = lines[k];
                if (l.y1 >= link.y0 && l.y0 <= link.y1 && l.x1 >= link.x0 && l.x0 <= link.x1) {
                    source = section_of[k];
                    break;
                }
            }
            if (source == sections.size()) continue;   // its words were taken out (a header, a footer)

            // Where it jumps to.
            const auto [to, to_end] = lines_of(link.target_page);
            if (to >= to_end) continue;   // a page without text
            std::size_t target_line = to;
            if (link.has_target_point && static_cast<std::size_t>(link.target_page) < page_transforms.size()) {
                float x = 0, y = 0;
                page_transforms[static_cast<std::size_t>(link.target_page)].apply(link.target_x, link.target_y, x, y);
                // the line closest below the point (lines need not be stored top to bottom)
                target_line = to_end - 1;
                float best = 1e30f;
                for (std::size_t k = to; k < to_end; ++k) {
                    const float below = lines[k].y0 - (y - 2.0f);
                    if (below >= 0 && below < best) {
                        best = below;
                        target_line = k;
                    }
                }
            }
            const std::size_t target = section_of[target_line];
            if (target == source) continue;

            // A link whose words hold no letter (a page number in a list of
            // contents, a box over a picture) tells a reader nothing.
            std::string text = tidy(link.text);
            if (std::ranges::none_of(text, [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || static_cast<unsigned char>(c) >= 0x80;
                })) {
                continue;
            }
            auto& out = sections[source].links;
            const bool known = std::ranges::any_of(out, [&](const SectionLink& l) {
                return l.target == target && l.text == text;
            });
            if (!known) out.push_back({std::move(text), target, lines[target_line].page});
        }
    }
}

}  // namespace htpd
