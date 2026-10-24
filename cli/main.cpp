// htpd - command-line tool. A thin wrapper around the HTPD library.
#include <charconv>
#include <cstdio>
#include <format>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>   // isatty

#include "htpd/converter.hpp"

namespace {

constexpr std::string_view kUsage = R"(htpd - convert a PDF into an AI-readable HTPD bundle

Usage:
  htpd convert <file.pdf> [options]   Convert a PDF
  htpd info <file.pdf> [options]      Show what would be found, write nothing
  htpd --version
  htpd --help

Options for convert:
  -o, --output <path>       Output folder, or file with --single
                            (default: <file name>.htpd)
      --single              Write ONE file instead of a folder: the whole
                            document with reading rules for AI models, to
                            hand to any AI chat as it is
      --force               Replace a previous bundle in the output folder
      --keep-headers        Keep running headers and footers in the text
      --dehyphenate         Join words split at a line end ("inter-" "rupt")
      --figures <on|off>    Store figures (pictures and drawings) as PNG
                            pictures in the file, with their caption
                            and page (default on)
      --figure-dpi <n>      Resolution of the stored figures, 50 to 300
                            (default 120)
      --tags <mode>         The structure tags of a tagged PDF (headings, tables,
                            lists, figures, page furniture), used where they
                            agree with the page text:
                              auto  when the headings found without the
                                    tags are poor: none, few for the
                                    pages, or all on one level (default)
                              on    whenever the PDF has tags
                              off   never
      --ocr <mode>          Text in pictures and scanned pages, read with the
                            free Tesseract program:
                              auto  when Tesseract is installed (default)
                              on    always; an error without Tesseract
                              off   never
      --ocr-lang <langs>    Tesseract language(s), e.g. eng+deu (default eng)
  -q, --quiet               Print errors only

Options for convert and info:
      --headings <mode>     Where headings come from:
                              auto       bookmarks, plus numbered headings
                                         found in the text below them (default)
                              bookmarks  bookmarks only
                              text       ignore bookmarks, read the text
                              none       no headings: one section per page
      --margin-band <f>     Header/footer zone as a fraction of the page
                            height, 0 to 0.3 (default 0.09)
      --password <text>     Password of an encrypted PDF
)";

struct Args {
    std::string command;
    std::string input;
    htpd::ConvertOptions convert;
    bool quiet = false;
};

void print(std::string_view text) {
    std::cout << text;
}

[[nodiscard]] int usage_error(std::string_view message) {
    std::cerr << "htpd: " << message << "\nTry 'htpd --help'.\n";
    return 2;
}

bool parse_fraction(std::string_view text, double& value) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc{} && end == text.data() + text.size() && value >= 0.0 && value <= 0.3;
}

bool parse_heading_mode(std::string_view text, htpd::HeadingMode& mode) {
    if (text == "auto") mode = htpd::HeadingMode::Auto;
    else if (text == "bookmarks") mode = htpd::HeadingMode::Bookmarks;
    else if (text == "text") mode = htpd::HeadingMode::Text;
    else if (text == "none") mode = htpd::HeadingMode::None;
    else return false;
    return true;
}

std::string megabytes(std::uintmax_t bytes) {
    return bytes < 1024 * 1024 ? std::format("{:.1f} kB", static_cast<double>(bytes) / 1024.0)
                               : std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

void print_furniture(const htpd::FurnitureReport& furniture) {
    for (const htpd::FurniturePattern& p : furniture.patterns) {
        const char* kind = p.top ? "header" : "footer";
        if (p.variants > 1) {
            print(std::format("      {} on {} pages, {} different texts, e.g. \"{}\"\n", kind, p.pages,
                              p.variants, p.sample));
        } else {
            print(std::format("      {} on {} pages: \"{}\"\n", kind, p.pages, p.sample));
        }
    }
}

int run_convert(Args& args) {
    const bool show_progress = !args.quiet && isatty(STDERR_FILENO) != 0;
    if (show_progress) {
        args.convert.on_progress = [](int done, int total) {
            if (done % 25 == 0 || done == total) {
                std::fprintf(stderr, "\r  reading page %d of %d", done, total);
                if (done == total) std::fprintf(stderr, "\n");
            }
        };
    }

    const auto report = htpd::convert(args.convert);
    if (!report) {
        std::cerr << "htpd: " << report.error().message << '\n';
        return 1;
    }
    if (args.quiet) return 0;

    const htpd::ConvertReport& r = *report;
    print(std::format("Converted \"{}\"\n", r.title));
    print(std::format("  output {}   {}\n", args.convert.single_file ? "file  " : "folder", r.output_dir.string()));
    print(std::format("  pages           {}\n", r.pages));
    print(std::format("  sections        {}  (headings from: {})\n", r.sections,
                      htpd::to_string(r.heading_source)));
    if (r.heading_source == htpd::HeadingSource::Bookmark) {
        print(std::format("  bookmarks       {} ({} located in the page text)\n", r.bookmarks_total,
                          r.bookmarks_located));
        if (r.headings_from_text > 0) {
            print(std::format("  + from text     {} numbered headings below the bookmark level\n",
                              r.headings_from_text));
        }
    }
    if (r.heading_source == htpd::HeadingSource::Contents) {
        print(std::format("  contents        {} entries of the printed table of contents, {} found in the text\n",
                          r.contents_entries, r.contents_located));
    }
    if (r.running_titles > 0) {
        print(std::format("  + running titles {} sections where the page header changes\n", r.running_titles));
    }
    if (r.page_parts > 0) {
        print(std::format("  + page parts    {} parts of long text, cut by pages\n", r.page_parts));
    }
    print(std::format("  paragraphs      {}\n", r.paragraphs));
    if (r.tags.any()) {
        print(std::format("  PDF tags        {} headings, {} tables ({} rows), {} lists ({} items), {} figures, "
                          "{} artifact lines\n",
                          r.headings_from_tags, r.tags.tables, r.tags.table_rows, r.tags.lists, r.tags.list_items,
                          r.tags.figures, r.tags.artifact_lines));
    }
    if (r.figures > 0) {
        print(std::format("  figures         {} stored as pictures ({})\n", r.figures, megabytes(r.figure_bytes)));
    }
    if (r.ocr.pages_read + r.ocr.pictures_read > 0) {
        print(std::format("  OCR             {} pages and {} pictures read, {} lines ({})\n", r.ocr.pages_read,
                          r.ocr.pictures_read, r.ocr.lines, r.ocr.engine));
    }
    print(std::format("  files written   {} ({}; the PDF is {})\n", r.files, megabytes(r.bundle_bytes),
                      megabytes(r.source_bytes)));
    print(std::format("  time            {:.2f} s\n", r.seconds));

    print("Left out of the text:\n");
    print(std::format("  header/footer lines     {}\n", r.furniture.removed_lines));
    print_furniture(r.furniture);
    print(std::format("  rotated characters      {}\n", r.skipped_rotated));
    if (r.unmapped > 0) {
        print(std::format("Warning: {} characters have no Unicode mapping; some text may be wrong.\n",
                          r.unmapped));
    }
    if (r.ocr.words_dropped > 0) {
        print(std::format("  unsure OCR words        {} (left out, not guessed)\n", r.ocr.words_dropped));
    }
    if (r.ocr.failed > 0) {
        print(std::format("Warning: OCR could not read {} pictures.\n", r.ocr.failed));
    }
    if (r.ocr.pages_unread > 0) {
        print(std::format("Warning: {} of {} pages {} no text layer (scans). Install the free Tesseract OCR "
                          "program to read them (Ubuntu: sudo apt install tesseract-ocr).{}\n",
                          r.ocr.pages_unread, r.pages, r.ocr.pages_unread == 1 ? "has" : "have",
                          r.ocr.page_pictures > 0 ? std::format(" Until then {} kept as pictures of the pages.",
                                                                r.ocr.page_pictures == 1 ? "it is" : "they are")
                                                  : std::string()));
    } else if (r.pages_without_text > 0) {
        print(std::format("Note: {} of {} pages {} no text at all (empty pages or pictures without text).\n",
                          r.pages_without_text, r.pages, r.pages_without_text == 1 ? "has" : "have"));
    }
    if (r.heading_source == htpd::HeadingSource::None) {
        print("Warning: no headings were found; the text was split by page.\n");
    }
    if (args.convert.single_file) {
        print(std::format("Give {} to any AI model and ask your questions about the document.\n",
                          r.output_dir.string()));
    } else {
        print(std::format("Start reading at {}\n", (r.output_dir / "document.md").string()));
    }
    return 0;
}

int run_info(const Args& args) {
    const auto report = htpd::inspect(args.convert.input, args.convert.password, args.convert.headings,
                                      args.convert.margin_band);
    if (!report) {
        std::cerr << "htpd: " << report.error().message << '\n';
        return 1;
    }
    const htpd::InspectReport& r = *report;
    print(std::format("File            {}\n", args.convert.input.string()));
    print(std::format("Title           {}\n", r.meta.title.empty() ? "(none)" : r.meta.title));
    print(std::format("Author          {}\n", r.meta.author.empty() ? "(none)" : r.meta.author));
    print(std::format("Pages           {} ({} without text)\n", r.meta.page_count, r.pages_without_text));
    print(std::format("Bookmarks       {}\n", r.bookmarks));
    print(std::format("Body font size  {:.1f} pt\n", r.body_size));
    print(std::format("Rotated chars   {} (would be skipped)\n", r.skipped_rotated));
    print(std::format("Header/footer   {} lines would be removed\n", r.furniture.removed_lines));
    print_furniture(r.furniture);
    print(std::format("Headings        {} (from: {})\n", r.headings.size(), htpd::to_string(r.heading_source)));
    const bool mixed = r.heading_source == htpd::HeadingSource::Bookmark;
    for (const htpd::Heading& h : r.headings) {
        const std::size_t indent = static_cast<std::size_t>(h.level > 1 ? h.level - 1 : 0) * 2;
        const char* note = h.line_count == 0                                         ? "   [not found in page text]"
                           : mixed && h.source != htpd::HeadingSource::Bookmark ? "   [from text]"
                                                                                 : "";
        print(std::format("  p.{:<5}{}{}{}\n", h.page + 1, std::string(indent, ' '), h.title, note));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> argv_view(argv + 1, argv + argc);
    if (argv_view.empty()) {
        print(kUsage);
        return 2;
    }

    Args args;
    for (std::size_t i = 0; i < argv_view.size(); ++i) {
        const std::string_view arg = argv_view[i];
        const auto value = [&](std::string_view& out) {
            if (i + 1 >= argv_view.size()) return false;
            out = argv_view[++i];
            return true;
        };
        std::string_view v;

        if (arg == "--help" || arg == "-h") {
            print(kUsage);
            return 0;
        } else if (arg == "--version") {
            print(std::format("htpd {} (HTPD format {}, OKF {})\n", htpd::kVersion, htpd::kFormatVersion,
                              htpd::kOkfVersion));
            return 0;
        } else if (arg == "-o" || arg == "--output") {
            if (!value(v)) return usage_error("--output needs a folder name");
            args.convert.output_dir = v;
        } else if (arg == "--password") {
            if (!value(v)) return usage_error("--password needs a value");
            args.convert.password = v;
        } else if (arg == "--headings") {
            if (!value(v) || !parse_heading_mode(v, args.convert.headings)) {
                return usage_error("--headings must be auto, bookmarks, text or none");
            }
        } else if (arg == "--margin-band") {
            if (!value(v) || !parse_fraction(v, args.convert.margin_band)) {
                return usage_error("--margin-band must be a number from 0 to 0.3");
            }
        } else if (arg == "--tags") {
            if (!value(v) || (v != "auto" && v != "on" && v != "off")) return usage_error("--tags must be auto, on or off");
            args.convert.tags = v == "on" ? htpd::TagMode::On : v == "off" ? htpd::TagMode::Off : htpd::TagMode::Auto;
        } else if (arg == "--figures") {
            if (!value(v) || (v != "on" && v != "off")) return usage_error("--figures must be on or off");
            args.convert.figures = v == "on";
        } else if (arg == "--figure-dpi") {
            int dpi = 0;
            if (!value(v) || std::from_chars(v.data(), v.data() + v.size(), dpi).ec != std::errc{} || dpi < 50 ||
                dpi > 300) {
                return usage_error("--figure-dpi must be a number from 50 to 300");
            }
            args.convert.figure_dpi = static_cast<float>(dpi);
        } else if (arg == "--ocr") {
            if (!value(v)) return usage_error("--ocr must be auto, on or off");
            if (v == "auto") args.convert.ocr = htpd::OcrMode::Auto;
            else if (v == "on") args.convert.ocr = htpd::OcrMode::On;
            else if (v == "off") args.convert.ocr = htpd::OcrMode::Off;
            else return usage_error("--ocr must be auto, on or off");
        } else if (arg == "--ocr-lang") {
            if (!value(v) || v.empty() || v.find_first_not_of("abcdefghijklmnopqrstuvwxyz_+") != std::string_view::npos) {
                return usage_error("--ocr-lang needs Tesseract language names such as eng or eng+deu");
            }
            args.convert.ocr_options.language = v;
        } else if (arg == "--single") {
            args.convert.single_file = true;
        } else if (arg == "--force") {
            args.convert.force = true;
        } else if (arg == "--keep-headers") {
            args.convert.keep_furniture = true;
        } else if (arg == "--dehyphenate") {
            args.convert.dehyphenate = true;
        } else if (arg == "-q" || arg == "--quiet") {
            args.quiet = true;
        } else if (arg.starts_with('-') && arg.size() > 1) {
            return usage_error(std::format("unknown option '{}'", arg));
        } else if (args.command.empty()) {
            args.command = arg;
        } else if (args.input.empty()) {
            args.input = arg;
        } else {
            return usage_error(std::format("unexpected argument '{}'", arg));
        }
    }

    if (args.command != "convert" && args.command != "info") {
        return usage_error(std::format("unknown command '{}'", args.command));
    }
    if (args.input.empty()) return usage_error("no PDF file given");
    args.convert.input = args.input;

    return args.command == "convert" ? run_convert(args) : run_info(args);
}
