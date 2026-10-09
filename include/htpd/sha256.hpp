// HTPD - SHA-256, used to tie a bundle to the exact source file it came from.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

#include "htpd/types.hpp"

namespace htpd {

class Sha256 {
public:
    Sha256();
    void update(std::span<const std::uint8_t> data);
    std::array<std::uint8_t, 32> finish();   // call once

private:
    void compress(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t total_bytes_ = 0;
};

std::string to_hex(std::span<const std::uint8_t> bytes);

struct FileDigest {
    std::string sha256_hex;
    std::uintmax_t bytes = 0;
};

Result<FileDigest> sha256_file(const std::filesystem::path& path);

}  // namespace htpd
