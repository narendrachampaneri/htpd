// HTPD - PDF reader implemented with PDFium (the PDF engine used by Chrome).
//
// Everything PDFium-specific is in this one file. It turns each page into a
// list of glyphs in "display space" (see types.hpp) so that no other part of
// HTPD has to know about page rotation, text matrices or PDF coordinates.
#include "htpd/pdf_reader.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include <fpdf_doc.h>
#include <fpdf_text.h>
#include <fpdfview.h>

#include "htpd/text.hpp"

namespace htpd {

namespace {

// PDFium must be initialised once per process and destroyed at exit.
struct Library {
    Library() { FPDF_InitLibrary(); }
    ~Library() { FPDF_DestroyLibrary(); }
};

void ensure_library() {
    static Library library;
}

struct PageCloser {
    void operator()(FPDF_PAGE p) const { FPDF_ClosePage(p); }
};
struct TextPageCloser {
    void operator()(FPDF_TEXTPAGE p) const { FPDFText_ClosePage(p); }
};
using PagePtr = std::unique_ptr<std::remove_pointer_t<FPDF_PAGE>, PageCloser>;
using TextPagePtr = std::unique_ptr<std::remove_pointer_t<FPDF_TEXTPAGE>, TextPageCloser>;

std::string last_error_text() {
    switch (FPDF_GetLastError()) {
        case FPDF_ERR_FILE:     return "the file was not found or could not be opened";
        case FPDF_ERR_FORMAT:   return "this is not a PDF file, or it is damaged";
        case FPDF_ERR_PASSWORD: return "a password is required, or the password is wrong";
        case FPDF_ERR_SECURITY: return "the PDF uses an unsupported security scheme";
        case FPDF_ERR_PAGE:     return "a page was not found or has a content error";
        default:                return "unknown PDFium error";
    }
}

// Glyphs may be tilted by up to this angle before they count as "rotated
// text" (vertical labels, diagonal watermarks). 30 degrees keeps slanted fonts.
constexpr float kMaxTilt = 0.5236f;

constexpr std::size_t kMaxOutlineEntries = 200000;
constexpr int kMaxOutlineDepth = 32;

// The page's user-space -> display-space transform. PDFium is asked for it
// (via FPDF_PageToDevice) so that /Rotate and the crop box are handled by the
// same code that Chrome uses to draw the page.
Affine page_affine(FPDF_PAGE page, float width, float height) {
    constexpr double kScale = 100.0;    // device units per point, for precision
    constexpr double kProbe = 1000.0;
    const int size_x = std::max(1, static_cast<int>(std::lround(width * kScale)));
    const int size_y = std::max(1, static_cast<int>(std::lround(height * kScale)));

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    const bool ok =
        FPDF_PageToDevice(page, 0, 0, size_x, size_y, 0, 0.0, 0.0, &x0, &y0) &&
        FPDF_PageToDevice(page, 0, 0, size_x, size_y, 0, kProbe, 0.0, &x1, &y1) &&
        FPDF_PageToDevice(page, 0, 0, size_x, size_y, 0, 0.0, kProbe, &x2, &y2);
    if (!ok) return Affine{1, 0, 0, -1, 0, height};   // plain upright page

    // Without scaling the linear part is a pure rotation/flip: snap to it.
    const auto unit = [&](int delta) {
        const double v = delta / (kProbe * kScale);
        const double r = std::round(v);
        return static_cast<float>(std::fabs(v - r) < 1e-3 ? r : v);
    };
    Affine t;
    t.a = unit(x1 - x0);
    t.b = unit(y1 - y0);
    t.c = unit(x2 - x0);
    t.d = unit(y2 - y0);
    t.e = static_cast<float>(x0 / kScale);
    t.f = static_cast<float>(y0 / kScale);
    return t;
}

// A then B: the transform that applies `first`, then `second`.
Affine then(const Affine& first, const Affine& second) {
    Affine r;
    r.a = second.a * first.a + second.c * first.b;
    r.b = second.b * first.a + second.d * first.b;
    r.c = second.a * first.c + second.c * first.d;
    r.d = second.b * first.c + second.d * first.d;
    r.e = second.a * first.e + second.c * first.f + second.e;
    r.f = second.b * first.e + second.d * first.f + second.f;
    return r;
}

// The quarter turn (0 to 3, clockwise on the screen) in which most of the
// text of a page runs, or 0 when most of it is upright or it is not clear.
// A landscape table printed sideways on a portrait page, or a page whose
// /Rotate turns its text, is then read like any other page; only text that
// runs another way than the rest of its page counts as rotated.
int main_text_direction(FPDF_TEXTPAGE tp, int count, const Affine& to_display) {
    std::size_t quarters[4] = {0, 0, 0, 0};
    std::size_t counted = 0;
    constexpr double kQuarter = 1.5707963267948966;
    for (int i = 0; i < count; ++i) {
        FS_MATRIX m{1, 0, 0, 1, 0, 0};
        if (!FPDFText_GetMatrix(tp, i, &m)) continue;
        const double dx = to_display.a * m.a + to_display.c * m.b;
        const double dy = to_display.b * m.a + to_display.d * m.b;
        if (dx == 0 && dy == 0) continue;
        const double angle = std::atan2(dy, dx);              // y grows downwards: +90 = runs down
        const double q = std::round(angle / kQuarter);
        if (std::fabs(angle - q * kQuarter) > kMaxTilt) continue;
        ++quarters[(static_cast<int>(q) % 4 + 4) % 4];
        ++counted;
    }
    int best = 0;
    for (int q = 1; q < 4; ++q) {
        if (quarters[q] > quarters[best]) best = q;
    }
    return best != 0 && quarters[best] * 2 > counted ? best : 0;
}

// The transform that turns a page by `quarters` so that its main text runs
// left to right; it also gives the size of the turned page.
Affine turn(int quarters, float& width, float& height) {
    const float w = width, h = height;
    Affine r;
    switch (quarters) {
        case 1: r = {0, -1, 1, 0, 0, w}; std::swap(width, height); break;   // text ran down
        case 2: r = {-1, 0, 0, -1, w, h}; break;                             // upside down
        case 3: r = {0, 1, -1, 0, h, 0}; std::swap(width, height); break;   // text ran up
        default: break;
    }
    return r;
}

bool name_says_bold(std::string_view font_name) {
    const std::string n = text::to_lower_ascii(font_name);
    return n.contains("bold") || n.contains("black") || n.contains("heavy") ||
           n.contains("demi") || n.ends_with("-bd") || n.ends_with(",bd");
}

constexpr bool is_space_like(char32_t cp) {
    return cp == U' ' || cp == U'\t' || cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A) ||
           cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

constexpr bool is_invisible(char32_t cp) {
    return cp == 0x00AD || (cp >= 0x200B && cp <= 0x200F) || cp == 0x2060 || cp == 0xFEFF;
}

std::string meta_text(FPDF_DOCUMENT doc, const char* tag) {
    const unsigned long needed = FPDF_GetMetaText(doc, tag, nullptr, 0);
    if (needed <= 2) return {};
    std::vector<unsigned char> buffer(needed);
    FPDF_GetMetaText(doc, tag, buffer.data(), needed);
    return text::collapse_spaces(text::sanitize_utf8(text::utf16le_to_utf8(buffer)));
}

std::string bookmark_title(FPDF_BOOKMARK bookmark) {
    const unsigned long needed = FPDFBookmark_GetTitle(bookmark, nullptr, 0);
    if (needed <= 2) return {};
    std::vector<unsigned char> buffer(needed);
    FPDFBookmark_GetTitle(bookmark, buffer.data(), needed);
    // Bookmark titles may contain line breaks and tabs: make them spaces first.
    std::string title = text::utf16le_to_utf8(buffer);
    for (char& c : title) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    return text::collapse_spaces(text::sanitize_utf8(title));
}

void walk_outline(FPDF_DOCUMENT doc, FPDF_BOOKMARK parent, int level,
                  std::vector<OutlineEntry>& out, std::unordered_set<const void*>& seen) {
    if (level > kMaxOutlineDepth) return;
    for (FPDF_BOOKMARK bm = FPDFBookmark_GetFirstChild(doc, parent); bm != nullptr;
         bm = FPDFBookmark_GetNextSibling(doc, bm)) {
        // Damaged files can contain loops; never visit a bookmark twice.
        if (!seen.insert(bm).second || out.size() >= kMaxOutlineEntries) return;

        OutlineEntry entry;
        entry.level = level;
        entry.title = bookmark_title(bm);

        FPDF_DEST dest = FPDFBookmark_GetDest(doc, bm);
        if (dest == nullptr) {
            const FPDF_ACTION action = FPDFBookmark_GetAction(bm);
            if (action != nullptr && FPDFAction_GetType(action) == PDFACTION_GOTO) {
                dest = FPDFAction_GetDest(doc, action);
            }
        }
        if (dest != nullptr) {
            entry.page = FPDFDest_GetDestPageIndex(doc, dest);
            FPDF_BOOL has_x = 0, has_y = 0, has_zoom = 0;
            FS_FLOAT x = 0, y = 0, zoom = 0;
            if (FPDFDest_GetLocationInPage(dest, &has_x, &has_y, &has_zoom, &x, &y, &zoom)) {
                if (has_x) entry.user_x = x;
                if (has_y) entry.user_y = y;
            }
        }
        out.push_back(std::move(entry));
        walk_outline(doc, bm, level + 1, out, seen);
    }
}

bool name_says_monospace(std::string_view font_name) {
    const std::string n = text::to_lower_ascii(font_name);
    for (const char* hint : {"mono", "courier", "consol", "typewriter", "cmtt", "menlo", "monaco"}) {
        if (n.contains(hint)) return true;
    }
    return false;
}

// One font of the document. Whether it is monospace is MEASURED: the PDF
// flag for it is often missing or wrong, and font names say little
// ("Monotype Corsiva" is not a monospace font).
//
// What is measured is how far the pen moves from one character to the next
// one in the same word. That is the width a character takes on the line, and
// in a monospace font it is the same for every character.
struct FontState {
    bool decided = false;
    bool monospace = false;          // until decided: the guess from flag and name
    float min_advance = 1e9f;
    float max_advance = 0;
    double sum_advance = 0;
    int samples = 0;
    std::vector<char32_t> seen;      // different characters measured so far

    // Enough different characters to compare their widths.
    static constexpr std::size_t kCharacters = 12;
    // Largest width difference (in em) a monospace font shows after rounding.
    static constexpr float kTolerance = 0.03f;

    void measure(char32_t cp, float advance_em) {
        if (advance_em < 0.05f || advance_em > 2.0f) return;   // combining marks, broken fonts
        min_advance = std::min(min_advance, advance_em);
        max_advance = std::max(max_advance, advance_em);
        sum_advance += advance_em;
        ++samples;
        if (std::ranges::find(seen, cp) == seen.end()) seen.push_back(cp);
        if (seen.size() >= kCharacters) {
            decided = true;
            monospace = max_advance - min_advance < kTolerance;
        }
    }
};

}  // namespace

namespace {

// The words under a box of the page, in the order of the text: what a link
// says. `by_height` holds (centre y, index) of every glyph that is not a space,
// sorted, so only the glyphs at the height of the box are looked at.
std::string words_in(const std::vector<Glyph>& glyphs, const std::vector<std::pair<float, std::size_t>>& by_height,
                     float x0, float y0, float x1, float y1) {
    auto it = std::ranges::lower_bound(by_height, std::pair<float, std::size_t>{y0 - 1, 0});
    std::vector<std::size_t> inside;
    for (; it != by_height.end() && it->first <= y1 + 1; ++it) {
        const Glyph& g = glyphs[it->second];
        const float cx = (g.x0 + g.x1) / 2;
        if (cx >= x0 - 1 && cx <= x1 + 1) inside.push_back(it->second);
    }
    std::ranges::sort(inside);
    std::string text;
    for (std::size_t k = 0; k < inside.size(); ++k) {
        // anything between two glyphs of the link (a space, a glyph outside the box) is a gap
        if (k > 0 && inside[k] != inside[k - 1] + 1) text.push_back(' ');
        text::append_utf8(text, glyphs[inside[k]].cp);
    }
    return text;
}

// The links of a page that jump to another place of the same document. Links
// to the web, to other files and actions are not kept.
void read_links(FPDF_DOCUMENT doc, FPDF_PAGE page, PageText& out) {
    int position = 0;
    FPDF_LINK link = nullptr;
    std::vector<std::pair<float, std::size_t>> by_height;
    bool indexed = false;
    while (FPDFLink_Enumerate(page, &position, &link)) {
        FPDF_DEST dest = FPDFLink_GetDest(doc, link);
        if (dest == nullptr) {
            const FPDF_ACTION action = FPDFLink_GetAction(link);
            if (action != nullptr && FPDFAction_GetType(action) == PDFACTION_GOTO) dest = FPDFAction_GetDest(doc, action);
        }
        if (dest == nullptr) continue;
        const int target = FPDFDest_GetDestPageIndex(doc, dest);
        if (target < 0) continue;
        FS_RECTF rect;
        if (!FPDFLink_GetAnnotRect(link, &rect)) continue;
        PageLink pl;
        float xs[4], ys[4];
        out.user_to_display.apply(rect.left, rect.bottom, xs[0], ys[0]);
        out.user_to_display.apply(rect.right, rect.bottom, xs[1], ys[1]);
        out.user_to_display.apply(rect.left, rect.top, xs[2], ys[2]);
        out.user_to_display.apply(rect.right, rect.top, xs[3], ys[3]);
        pl.x0 = std::min({xs[0], xs[1], xs[2], xs[3]});
        pl.x1 = std::max({xs[0], xs[1], xs[2], xs[3]});
        pl.y0 = std::min({ys[0], ys[1], ys[2], ys[3]});
        pl.y1 = std::max({ys[0], ys[1], ys[2], ys[3]});
        if (!indexed) {
            for (std::size_t i = 0; i < out.glyphs.size(); ++i) {
                const Glyph& g = out.glyphs[i];
                if (!g.is_space) by_height.emplace_back((g.y0 + g.y1) / 2, i);
            }
            std::ranges::sort(by_height);
            indexed = true;
        }
        pl.text = words_in(out.glyphs, by_height, pl.x0, pl.y0, pl.x1, pl.y1);
        pl.target_page = target;
        FPDF_BOOL has_x = 0, has_y = 0, has_zoom = 0;
        FS_FLOAT x = 0, y = 0, zoom = 0;
        if (FPDFDest_GetLocationInPage(dest, &has_x, &has_y, &has_zoom, &x, &y, &zoom) && has_y) {
            pl.has_target_point = true;
            pl.target_x = has_x ? x : 0;
            pl.target_y = y;
        }
        out.links.push_back(std::move(pl));
    }
}

}  // namespace

struct PdfDocument::Impl {
    FPDF_DOCUMENT doc = nullptr;
    // Font name -> position in `fonts`, filled while pages are read. Position
    // 0 stands for "unknown font". Fonts are often embedded as subsets named
    // "ABCDEF+Name"; the tag before '+' is dropped so that all subsets of one
    // font share a position.
    std::unordered_map<std::string, std::uint16_t> font_ids;
    std::vector<FontState> fonts = std::vector<FontState>(1);

    std::uint16_t font_id(std::string_view name, bool fixed_pitch_flag) {
        if (name.size() > 7 && name[6] == '+') name.remove_prefix(7);
        if (name.empty()) return 0;
        const auto [it, inserted] = font_ids.try_emplace(std::string(name), std::uint16_t{0});
        if (inserted) {
            if (fonts.size() >= 0xFFFF) return 0;   // absurd number of fonts: treat as unknown
            it->second = static_cast<std::uint16_t>(fonts.size());
            fonts.emplace_back().monospace = fixed_pitch_flag || name_says_monospace(name);
        }
        return it->second;
    }

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() {
        if (doc != nullptr) FPDF_CloseDocument(doc);
    }
};

PdfDocument::PdfDocument(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PdfDocument::PdfDocument(PdfDocument&&) noexcept = default;
PdfDocument& PdfDocument::operator=(PdfDocument&&) noexcept = default;
PdfDocument::~PdfDocument() = default;

Result<PdfDocument> PdfDocument::open(const std::filesystem::path& path,
                                      const std::string& password) {
    ensure_library();
    auto impl = std::make_unique<Impl>();
    const std::string native = path.string();
    impl->doc = FPDF_LoadDocument(native.c_str(), password.empty() ? nullptr : password.c_str());
    if (impl->doc == nullptr) {
        return fail(std::format("cannot open '{}': {}", native, last_error_text()));
    }
    return PdfDocument(std::move(impl));
}

int PdfDocument::page_count() const {
    return FPDF_GetPageCount(impl_->doc);
}

DocumentMeta PdfDocument::meta() const {
    DocumentMeta meta;
    meta.title = meta_text(impl_->doc, "Title");
    meta.author = meta_text(impl_->doc, "Author");
    meta.subject = meta_text(impl_->doc, "Subject");
    meta.page_count = page_count();
    return meta;
}

std::vector<OutlineEntry> PdfDocument::outline() const {
    std::vector<OutlineEntry> out;
    std::unordered_set<const void*> seen;
    walk_outline(impl_->doc, nullptr, 1, out, seen);
    return out;
}

std::vector<FontFacts> PdfDocument::fonts() const {
    std::vector<FontFacts> facts(impl_->fonts.size());
    for (std::size_t i = 1; i < facts.size(); ++i) {
        const FontState& font = impl_->fonts[i];
        facts[i].monospace = font.monospace;
        if (font.samples > 0) facts[i].advance = static_cast<float>(font.sum_advance / font.samples);
    }
    return facts;
}

Result<PageText> PdfDocument::read_page(int index) const {
    const PagePtr page(FPDF_LoadPage(impl_->doc, index));
    if (!page) {
        return fail(std::format("cannot load page {}: {}", index + 1, last_error_text()));
    }

    PageText out;
    out.index = index;
    out.width = FPDF_GetPageWidthF(page.get());
    out.height = FPDF_GetPageHeightF(page.get());
    out.user_to_display = page_affine(page.get(), out.width, out.height);
    // The size above is the size before /Rotate; a quarter turn swaps it.
    if (std::fabs(out.user_to_display.a) < 0.5f && std::fabs(out.user_to_display.b) > 0.5f) {
        std::swap(out.width, out.height);
    }

    const TextPagePtr text_page(FPDFText_LoadPage(page.get()));
    if (!text_page) return out;   // a page without a text layer is not an error
    const FPDF_TEXTPAGE tp = text_page.get();

    const int count = FPDFText_CountChars(tp);
    if (const int quarters = main_text_direction(tp, count, out.user_to_display); quarters != 0) {
        out.user_to_display = then(out.user_to_display, turn(quarters, out.width, out.height));
        out.turned = quarters;
    }
    const Affine& to_display = out.user_to_display;
    if (count > 0) out.glyphs.reserve(static_cast<std::size_t>(count));

    // The last character that was kept, for measuring the fonts.
    struct Previous {
        int last_index = -2;      // position in PDFium's list of characters
        std::uint16_t font = 0;
        char32_t cp = 0;
        float origin_x = 0, baseline = 0;
        float em = 0;             // font size along the line, in points
    } previous;

    for (int i = 0; i < count; ++i) {
        const int first_index = i;
        char32_t cp = FPDFText_GetUnicode(tp, i);

        // PDFium may report UTF-16 units; join a surrogate pair into one code point.
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < count) {
            const char32_t low = FPDFText_GetUnicode(tp, i + 1);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;   // geometry of the pair is taken from the low half below
            }
        }
        // A hyphen that ends a line is flagged by PDFium; keep it as a hyphen.
        if (FPDFText_IsHyphen(tp, i) == 1) cp = U'-';

        if (is_space_like(cp)) {
            Glyph space;
            space.cp = U' ';
            space.is_space = true;
            out.glyphs.push_back(space);
            continue;
        }
        // Line breaks are rebuilt from geometry, so PDFium's are dropped, as
        // are other control and zero-width characters.
        if (cp < 0x20 || cp == 0x7F || cp == 0xFFFE || cp == 0xFFFF || is_invisible(cp)) continue;
        if (FPDFText_HasUnicodeMapError(tp, i) == 1) ++out.unmapped;

        double left = 0, right = 0, bottom = 0, top = 0, origin_x = 0, origin_y = 0;
        const bool has_box = FPDFText_GetCharBox(tp, i, &left, &right, &bottom, &top) != 0;
        const bool has_origin = FPDFText_GetCharOrigin(tp, i, &origin_x, &origin_y) != 0;
        if (!has_box && !has_origin) continue;
        if (!has_box) {
            left = right = origin_x;
            bottom = top = origin_y;
        }
        if (!has_origin) {
            origin_x = left;
            origin_y = bottom;
        }

        // The text matrix gives the real size and direction of the glyph;
        // FPDFText_GetFontSize alone ignores any scaling done by the matrix.
        const float nominal_size = static_cast<float>(FPDFText_GetFontSize(tp, i));
        float size = nominal_size;
        FS_MATRIX m{1, 0, 0, 1, 0, 0};
        if (FPDFText_GetMatrix(tp, i, &m)) {
            const float vertical_scale = std::hypot(m.c, m.d);
            if (vertical_scale > 0) size *= vertical_scale;

            const float dx = to_display.a * m.a + to_display.c * m.b;
            const float dy = to_display.b * m.a + to_display.d * m.b;
            if ((dx != 0 || dy != 0) && std::fabs(std::atan2(dy, dx)) > kMaxTilt) {
                ++out.skipped_rotated;
                continue;
            }
        }

        Glyph g;
        g.cp = cp;
        const auto fl = [](double v) { return static_cast<float>(v); };
        float xs[4], ys[4];
        to_display.apply(fl(left), fl(bottom), xs[0], ys[0]);
        to_display.apply(fl(right), fl(bottom), xs[1], ys[1]);
        to_display.apply(fl(left), fl(top), xs[2], ys[2]);
        to_display.apply(fl(right), fl(top), xs[3], ys[3]);
        g.x0 = std::min({xs[0], xs[1], xs[2], xs[3]});
        g.x1 = std::max({xs[0], xs[1], xs[2], xs[3]});
        g.y0 = std::min({ys[0], ys[1], ys[2], ys[3]});
        g.y1 = std::max({ys[0], ys[1], ys[2], ys[3]});
        to_display.apply(fl(origin_x), fl(origin_y), g.origin_x, g.baseline);
        g.size = size > 0.01f ? size : std::max(1.0f, (g.y1 - g.y0) * 1.4f);

        char font_name[160] = {};
        int font_flags = 0;
        const unsigned long name_bytes =
            FPDFText_GetFontInfo(tp, i, font_name, sizeof font_name, &font_flags);
        constexpr int kForceBold = 1 << 18;   // PDF font descriptor flag, bit 19
        const std::string_view name =
            (name_bytes > 0 && name_bytes <= sizeof font_name) ? std::string_view(font_name)
                                                               : std::string_view{};
        g.bold = FPDFText_GetFontWeight(tp, i) >= 600 || (font_flags & kForceBold) != 0 ||
                 name_says_bold(name);
        constexpr int kFixedPitch = 1 << 0;   // PDF font descriptor flag, bit 1
        g.font = impl_->font_id(name, (font_flags & kFixedPitch) != 0);

        // Measure the font until it is clear whether it is monospace: when
        // this character directly follows the previous one, on the same line
        // and in the same font, the pen moved by the width of the previous one.
        const float horizontal_scale = std::hypot(m.a, m.b);
        const float em = nominal_size * (horizontal_scale > 0 ? horizontal_scale : 1.0f);
        if (g.font != 0 && previous.font == g.font && previous.last_index + 1 == first_index &&
            previous.em > 0.01f && std::fabs(g.baseline - previous.baseline) < 0.05f * previous.em) {
            FontState& font = impl_->fonts[g.font];
            if (!font.decided) font.measure(previous.cp, (g.origin_x - previous.origin_x) / previous.em);
        }
        previous = {i, g.font, cp, g.origin_x, g.baseline, em};

        out.glyphs.push_back(g);
    }
    read_links(impl_->doc, page.get(), out);
    return out;
}

}  // namespace htpd
