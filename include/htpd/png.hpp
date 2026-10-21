// HTPD - PNG files and base64, for the figures of a document.
//
// A small PNG writer without any library: each row gets the PNG filter that
// suits it best, and the rows are packed with deflate (LZ77 and the fixed
// Huffman codes of RFC 1951) in a zlib stream. Figures are mostly white
// paper with lines and words, which this packs well.
#pragma once

#include <string>
#include <string_view>

#include "htpd/types.hpp"

namespace htpd {

// The image as a PNG file (8 bits per channel: grey or RGB).
std::string png_bytes(const Image& image);

// An RGB image whose pixels are all grey becomes a grey image (a third of
// the size); any other image is returned as it is.
Image grey_if_possible(Image image);

// Standard base64 (RFC 4648), without line breaks.
std::string base64(std::string_view bytes);

// ---- building blocks, exposed for testing --------------------------------

std::string zlib_deflate(std::string_view data);   // a zlib stream (RFC 1950) of fixed-Huffman deflate
unsigned crc32(std::string_view data);

}  // namespace htpd
