#include "htpd/png.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace htpd {

namespace {

// Bits are written from the lowest bit of each byte up (RFC 1951 3.1.1).
class BitWriter {
public:
    explicit BitWriter(std::string& out) : out_(out) {}

    void bits(std::uint32_t value, int count) {   // `count` low bits of value, low bit first
        acc_ |= static_cast<std::uint64_t>(value) << used_;
        used_ += count;
        while (used_ >= 8) {
            out_.push_back(static_cast<char>(acc_ & 0xFF));
            acc_ >>= 8;
            used_ -= 8;
        }
    }
    void code(std::uint32_t code, int length) {    // a Huffman code: highest bit first
        std::uint32_t reversed = 0;
        for (int i = 0; i < length; ++i) reversed |= ((code >> i) & 1u) << (length - 1 - i);
        bits(reversed, length);
    }
    void flush() {
        if (used_ > 0) out_.push_back(static_cast<char>(acc_ & 0xFF));
        acc_ = 0;
        used_ = 0;
    }

private:
    std::string& out_;
    std::uint64_t acc_ = 0;
    int used_ = 0;
};

// The fixed literal/length code (RFC 1951 3.2.6).
void literal(BitWriter& w, int symbol) {
    if (symbol < 144) w.code(static_cast<std::uint32_t>(0x30 + symbol), 8);
    else if (symbol < 256) w.code(static_cast<std::uint32_t>(0x190 + symbol - 144), 9);
    else if (symbol < 280) w.code(static_cast<std::uint32_t>(symbol - 256), 7);
    else w.code(static_cast<std::uint32_t>(0xC0 + symbol - 280), 8);
}

constexpr std::array<int, 29> kLengthBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<int, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                              2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<int, 30> kDistBase = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                           33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                           1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<int, 30> kDistExtra = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void match(BitWriter& w, int length, int distance) {
    int l = 28;
    while (kLengthBase[static_cast<std::size_t>(l)] > length) --l;
    literal(w, 257 + l);
    w.bits(static_cast<std::uint32_t>(length - kLengthBase[static_cast<std::size_t>(l)]),
           kLengthExtra[static_cast<std::size_t>(l)]);
    int d = 29;
    while (kDistBase[static_cast<std::size_t>(d)] > distance) --d;
    w.code(static_cast<std::uint32_t>(d), 5);
    w.bits(static_cast<std::uint32_t>(distance - kDistBase[static_cast<std::size_t>(d)]),
           kDistExtra[static_cast<std::size_t>(d)]);
}

std::uint32_t adler32(std::string_view data) {
    std::uint32_t a = 1, b = 0;
    for (const char c : data) {
        a = (a + static_cast<unsigned char>(c)) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void put32(std::string& out, std::uint32_t v) {   // big-endian
    out.push_back(static_cast<char>(v >> 24));
    out.push_back(static_cast<char>(v >> 16));
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v));
}

void chunk(std::string& png, std::string_view type, std::string_view data) {
    put32(png, static_cast<std::uint32_t>(data.size()));
    std::string body(type);
    body += data;
    png += body;
    put32(png, crc32(body));
}

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

}  // namespace

unsigned crc32(std::string_view data) {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (const char ch : data) c = table[(c ^ static_cast<unsigned char>(ch)) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string zlib_deflate(std::string_view data) {
    std::string out;
    out.push_back(static_cast<char>(0x78));   // deflate, 32 KB window
    out.push_back(static_cast<char>(0x01));   // no dictionary, check bits: 0x7801 % 31 == 0
    BitWriter w(out);
    w.bits(1, 1);   // the last block
    w.bits(1, 2);   // fixed Huffman codes

    constexpr std::size_t kWindow = 32768, kHashSize = 1u << 15, kMaxChain = 48;
    constexpr int kMin = 3, kMax = 258;
    std::vector<std::int32_t> head(kHashSize, -1), prev(kWindow, -1);
    const auto hash = [&](std::size_t i) {
        return ((static_cast<std::uint32_t>(static_cast<unsigned char>(data[i])) << 10) ^
                (static_cast<std::uint32_t>(static_cast<unsigned char>(data[i + 1])) << 5) ^
                static_cast<std::uint32_t>(static_cast<unsigned char>(data[i + 2]))) & (kHashSize - 1);
    };
    const auto insert = [&](std::size_t i) {
        if (i + 2 >= data.size()) return;
        const std::uint32_t h = hash(i);
        prev[i % kWindow] = head[h];
        head[h] = static_cast<std::int32_t>(i);
    };

    std::size_t i = 0;
    while (i < data.size()) {
        int best_len = 0, best_dist = 0;
        if (i + kMin <= data.size()) {
            const std::size_t limit = std::min<std::size_t>(kMax, data.size() - i);
            std::int32_t candidate = head[hash(i)];
            for (std::size_t chain = 0; candidate >= 0 && chain < kMaxChain; ++chain) {
                const auto c = static_cast<std::size_t>(candidate);
                if (i - c > kWindow - 1) break;
                std::size_t len = 0;
                while (len < limit && data[c + len] == data[i + len]) ++len;
                if (static_cast<int>(len) > best_len) {
                    best_len = static_cast<int>(len);
                    best_dist = static_cast<int>(i - c);
                    if (len == limit) break;
                }
                const std::int32_t next = prev[c % kWindow];
                if (next >= candidate) break;   // the slot was reused for a newer position
                candidate = next;
            }
        }
        if (best_len >= kMin) {
            match(w, best_len, best_dist);
            for (int k = 0; k < best_len; ++k) insert(i + static_cast<std::size_t>(k));
            i += static_cast<std::size_t>(best_len);
        } else {
            literal(w, static_cast<unsigned char>(data[i]));
            insert(i);
            ++i;
        }
    }
    literal(w, 256);   // end of the block
    w.flush();
    put32(out, adler32(data));
    return out;
}

std::string png_bytes(const Image& image) {
    const int bpp = image.channels == 3 ? 3 : 1;
    const std::size_t row = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(bpp);

    // Each row with the filter whose output is smallest (sum of |value|):
    // the usual rule of thumb, cheap and good for drawings.
    std::string filtered;
    filtered.reserve((row + 1) * static_cast<std::size_t>(image.height));
    std::vector<std::uint8_t> cand[5];
    for (auto& c : cand) c.resize(row);
    const std::vector<std::uint8_t> zero(row, 0);
    for (int y = 0; y < image.height; ++y) {
        const std::uint8_t* cur = image.pixels.data() + static_cast<std::size_t>(y) * row;
        const std::uint8_t* up = y > 0 ? cur - row : zero.data();
        long best_sum = -1;
        int best = 0;
        for (int f = 0; f < 5; ++f) {
            long sum = 0;
            for (std::size_t x = 0; x < row; ++x) {
                const int a = x >= static_cast<std::size_t>(bpp) ? cur[x - static_cast<std::size_t>(bpp)] : 0;
                const int b = up[x];
                const int c = x >= static_cast<std::size_t>(bpp) ? up[x - static_cast<std::size_t>(bpp)] : 0;
                int pred = 0;
                if (f == 1) pred = a;
                else if (f == 2) pred = b;
                else if (f == 3) pred = (a + b) / 2;
                else if (f == 4) pred = paeth(a, b, c);
                const auto v = static_cast<std::uint8_t>(cur[x] - pred);
                cand[f][x] = v;
                sum += v < 128 ? v : 256 - v;
            }
            if (best_sum < 0 || sum < best_sum) {
                best_sum = sum;
                best = f;
            }
        }
        filtered.push_back(static_cast<char>(best));
        filtered.append(reinterpret_cast<const char*>(cand[best].data()), row);
    }

    std::string png("\x89PNG\r\n\x1a\n", 8);
    std::string header;
    put32(header, static_cast<std::uint32_t>(image.width));
    put32(header, static_cast<std::uint32_t>(image.height));
    header.push_back(8);                                   // bits per channel
    header.push_back(static_cast<char>(bpp == 3 ? 2 : 0)); // RGB or grey
    header.push_back(0);                                   // deflate
    header.push_back(0);                                   // adaptive filters
    header.push_back(0);                                   // no interlace
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", zlib_deflate(filtered));
    chunk(png, "IEND", {});
    return png;
}

Image grey_if_possible(Image image) {
    if (image.channels != 3) return image;
    const std::size_t n = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* p = image.pixels.data() + 3 * i;
        if (std::abs(p[0] - p[1]) > 2 || std::abs(p[1] - p[2]) > 2) return image;   // colour carries meaning
    }
    Image grey;
    grey.width = image.width;
    grey.height = image.height;
    grey.channels = 1;
    grey.pixels.resize(n);
    for (std::size_t i = 0; i < n; ++i) grey.pixels[i] = image.pixels[3 * i + 1];
    return grey;
}

std::string base64(std::string_view bytes) {
    static constexpr char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const std::uint32_t v = (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << 16) |
                                (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8) |
                                static_cast<unsigned char>(bytes[i + 2]);
        out += kDigits[(v >> 18) & 63];
        out += kDigits[(v >> 12) & 63];
        out += kDigits[(v >> 6) & 63];
        out += kDigits[v & 63];
    }
    if (i < bytes.size()) {
        std::uint32_t v = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << 16;
        if (i + 1 < bytes.size()) v |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8;
        out += kDigits[(v >> 18) & 63];
        out += kDigits[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? kDigits[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

}  // namespace htpd
