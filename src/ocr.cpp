#include "htpd/ocr.hpp"

#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <tuple>

#include "htpd/text.hpp"

extern char** environ;

namespace htpd {

namespace fs = std::filesystem;

namespace {

// Runs `argv` without a shell; its standard output goes to `out_file` (or
// nowhere). OMP_THREAD_LIMIT=1: the pool runs one process per core, so each
// process must not start threads of its own.
bool run_program(const std::vector<std::string>& argv, const fs::path& out_file = {}) {
    std::vector<char*> args;
    for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    std::vector<std::string> env_strings;
    for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
        if (std::string_view(*e).starts_with("OMP_THREAD_LIMIT=")) continue;
        env_strings.emplace_back(*e);
    }
    env_strings.emplace_back("OMP_THREAD_LIMIT=1");
    std::vector<char*> env;
    for (std::string& e : env_strings) env.push_back(e.data());
    env.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    const std::string out = out_file.empty() ? std::string("/dev/null") : out_file.string();
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    const int spawned = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), env.data());
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) return false;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

std::string read_whole(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

fs::path make_temp_dir() {
    static std::atomic<unsigned> counter{0};
    const fs::path dir = fs::temp_directory_path() /
                         std::format("htpd-ocr-{}-{}", static_cast<long>(getpid()), counter.fetch_add(1));
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

bool has_letter_or_digit(std::string_view s) {
    return text::letter_count(s) > 0 || std::ranges::any_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

}  // namespace

std::string pgm_bytes(const Image& image) {
    std::string out = std::format("P5\n{} {}\n255\n", image.width, image.height);
    out.append(reinterpret_cast<const char*>(image.pixels.data()), image.pixels.size());
    return out;
}

TsvLines lines_from_tsv(std::string_view tsv, int page, const Box& region, float scale, int min_word_confidence,
                        bool whole_page) {
    struct Word {
        int left, top, width, height;
        std::string text;
    };
    TsvLines result;
    std::map<std::tuple<int, int, int, int>, std::vector<Word>> by_line;   // page, block, paragraph, line
    std::size_t pos = 0;
    bool header = true;
    while (pos < tsv.size()) {
        std::size_t end = tsv.find('\n', pos);
        if (end == std::string_view::npos) end = tsv.size();
        std::string_view row = tsv.substr(pos, end - pos);
        pos = end + 1;
        if (!row.empty() && row.back() == '\r') row.remove_suffix(1);
        if (header) {   // "level page_num block_num ..."
            header = false;
            if (row.starts_with("level")) continue;
        }
        std::vector<std::string_view> f;
        for (std::size_t start = 0;;) {
            const std::size_t tab = row.find('\t', start);
            f.push_back(row.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
            if (tab == std::string_view::npos) break;
            start = tab + 1;
        }
        if (f.size() < 12 || f[0] != "5") continue;   // level 5: a word
        int v[10] = {};
        bool ok = true;
        for (int i = 0; i < 10 && ok; ++i) {
            const std::string_view s = f[static_cast<std::size_t>(i + 1)];
            ok = std::from_chars(s.data(), s.data() + s.size(), v[i]).ec == std::errc{};
        }
        if (!ok) continue;
        // v: page block paragraph line word left top width height confidence (a confidence
        // with decimals: its whole part is enough)
        const std::string word = text::collapse_spaces(text::sanitize_utf8(f[11]));
        if (word.empty()) continue;
        if (v[9] < min_word_confidence || !has_letter_or_digit(word)) {
            ++result.words_dropped;
            continue;
        }
        by_line[{v[0], v[1], v[2], v[3]}].push_back({v[5], v[6], v[7], v[8], word});
    }

    for (auto& [key, words] : by_line) {
        std::ranges::sort(words, {}, &Word::left);
        Line line;
        line.page = page;
        line.ocr = whole_page ? Line::Ocr::Page : Line::Ocr::Picture;
        int top = words.front().top, bottom = 0, height = 0;
        for (const Word& w : words) {
            top = std::min(top, w.top);
            bottom = std::max(bottom, w.top + w.height);
            height = std::max(height, w.height);
        }
        int previous_right = -1;
        for (const Word& w : words) {
            if (previous_right >= 0) {
                // a wide gap between two words: a gap between table columns
                if (whole_page && w.left - previous_right > 2 * height) {
                    line.gaps.push_back(static_cast<std::uint32_t>(line.text.size()));
                    line.gap_begin.push_back(region.x0 + static_cast<float>(previous_right) / scale);
                    line.gap_end.push_back(region.x0 + static_cast<float>(w.left) / scale);
                }
                line.text.push_back(' ');
                line.space_spans.push_back(static_cast<float>(w.left - previous_right) / scale);
            }
            line.text += w.text;
            previous_right = w.left + w.width;
        }
        line.x0 = region.x0 + static_cast<float>(words.front().left) / scale;
        line.x1 = region.x0 + static_cast<float>(previous_right) / scale;
        line.y0 = region.y0 + static_cast<float>(top) / scale;
        line.y1 = region.y0 + static_cast<float>(bottom) / scale;
        line.baseline = line.y1;
        line.origin_x = line.x0;
        line.size = static_cast<float>(height) / scale;
        result.lines.push_back(std::move(line));
    }
    // Reading order: top to bottom (Tesseract numbers blocks in its own order).
    std::ranges::stable_sort(result.lines, [](const Line& a, const Line& b) {
        return a.y0 + 0.5f * a.size < b.y0 ? true : (b.y0 + 0.5f * b.size < a.y0 ? false : a.x0 < b.x0);
    });
    return result;
}

void drop_known_text(std::vector<Line>& found, const std::vector<Line>& page) {
    std::erase_if(found, [&](const Line& f) {
        const float area = std::max(1.0f, (f.x1 - f.x0) * (f.y1 - f.y0));
        return std::ranges::any_of(page, [&](const Line& l) {
            if (l.figure > 0) return false;   // the place of a figure is no text
            const float w = std::min(f.x1, l.x1) - std::max(f.x0, l.x0);
            const float h = std::min(f.y1, l.y1) - std::max(f.y0, l.y0);
            return w > 0 && h > 0 && w * h > 0.3f * area;
        });
    });
}

void merge_page_lines(std::vector<Line>& page, std::vector<Line> found) {
    drop_known_text(found, page);
    page.insert(page.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
    std::ranges::stable_sort(page, [](const Line& a, const Line& b) {
        const float ha = 0.5f * std::max(a.y1 - a.y0, 1.0f);
        return a.y1 - ha < b.y0 ? true : (b.y1 - 0.5f * std::max(b.y1 - b.y0, 1.0f) < a.y0 ? false : a.x0 < b.x0);
    });
}

void insert_picture_lines(std::vector<Line>& page, std::vector<Line> found, const Box& box) {
    drop_known_text(found, page);
    // Each line after the last line of the page that stands above it, among
    // the lines above the picture and on it (text of the PDF on a picture).
    for (Line& f : found) {
        const float centre = (f.y0 + f.y1) / 2;
        std::size_t at = 0;
        for (std::size_t i = 0; i < page.size(); ++i) {
            // the place of a figure counts at its top: the words of the figure follow it
            const float c = page[i].figure > 0 ? page[i].y0 : (page[i].y0 + page[i].y1) / 2;
            if (c < centre && c < box.y1) at = i + 1;
        }
        page.insert(page.begin() + static_cast<std::ptrdiff_t>(at), std::move(f));
    }
}

OcrEngine find_ocr(const OcrOptions& options) {
    OcrEngine engine;
    const fs::path dir = make_temp_dir();
    const fs::path out = dir / "version.txt";
    if (run_program({options.program, "--version"}, out)) {
        const std::string v = read_whole(out);
        engine.version = text::collapse_spaces(v.substr(0, v.find('\n')));
        // every language asked for must be installed
        const fs::path langs_file = dir / "langs.txt";
        if (run_program({options.program, "--list-langs"}, langs_file)) {
            const std::string langs = read_whole(langs_file);
            engine.available = true;
            std::string_view want = options.language;
            while (!want.empty()) {
                const std::size_t plus = want.find('+');
                const std::string one(want.substr(0, plus));
                if (langs.find("\n" + one + "\n") == std::string::npos) engine.available = false;
                want = plus == std::string_view::npos ? std::string_view{} : want.substr(plus + 1);
            }
        }
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    return engine;
}

// ---- the pool ----------------------------------------------------------------

struct OcrPool::Impl {
    OcrOptions options;
    fs::path dir;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::pair<std::size_t, OcrJob>> waiting;
    std::vector<std::vector<Line>> results;
    std::size_t added = 0;
    std::size_t failed = 0;
    std::size_t dropped = 0;
    bool closing = false;
    std::vector<std::thread> workers;
    std::size_t max_waiting = 2;

    void work() {
        for (;;) {
            std::pair<std::size_t, OcrJob> item;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return closing || !waiting.empty(); });
                if (waiting.empty()) return;
                item = std::move(waiting.front());
                waiting.pop_front();
            }
            changed.notify_all();
            const auto [index, job] = std::move(item);
            const fs::path image = dir / std::format("{}.pgm", index);
            const fs::path base = dir / std::format("{}", index);
            bool ok = false;
            TsvLines lines;
            {
                std::ofstream f(image, std::ios::binary);
                const std::string bytes = pgm_bytes(job.image);
                f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                ok = static_cast<bool>(f);
            }
            if (ok) {
                ok = run_program({options.program, image.string(), base.string(), "-l", options.language, "--dpi",
                                  std::format("{}", static_cast<int>(job.scale * 72.0f)), "tsv"});
            }
            if (ok) {
                lines = lines_from_tsv(read_whole(fs::path(base.string() + ".tsv")), job.page, job.region, job.scale,
                                       options.min_word_confidence, job.whole_page);
            }
            std::error_code ec;
            fs::remove(image, ec);
            fs::remove(fs::path(base.string() + ".tsv"), ec);
            std::lock_guard lock(mutex);
            if (!ok) ++failed;
            dropped += lines.words_dropped;
            results[index] = std::move(lines.lines);
        }
    }
};

OcrPool::OcrPool(const OcrOptions& options) : impl_(new Impl) {
    impl_->options = options;
    impl_->dir = make_temp_dir();
    unsigned n = options.threads != 0 ? options.threads : std::max(1u, std::thread::hardware_concurrency());
    impl_->max_waiting = 2 * static_cast<std::size_t>(n);
    for (unsigned i = 0; i < n; ++i) impl_->workers.emplace_back([this] { impl_->work(); });
}

OcrPool::~OcrPool() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->closing = true;
        impl_->waiting.clear();
    }
    impl_->changed.notify_all();
    for (std::thread& t : impl_->workers) {
        if (t.joinable()) t.join();
    }
    std::error_code ec;
    fs::remove_all(impl_->dir, ec);
    delete impl_;
}

void OcrPool::add(OcrJob job) {
    std::unique_lock lock(impl_->mutex);
    // A drawn page takes megabytes: do not draw far ahead of the readers.
    impl_->changed.wait(lock, [&] { return impl_->waiting.size() < impl_->max_waiting; });
    const std::size_t index = impl_->added++;
    impl_->results.emplace_back();
    impl_->waiting.emplace_back(index, std::move(job));
    lock.unlock();
    impl_->changed.notify_all();
}

OcrPool::Done OcrPool::finish() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->closing = true;
    }
    impl_->changed.notify_all();
    for (std::thread& t : impl_->workers) {
        if (t.joinable()) t.join();
    }
    Done done;
    done.lines = std::move(impl_->results);
    done.failed = impl_->failed;
    done.words_dropped = impl_->dropped;
    return done;
}

}  // namespace htpd
