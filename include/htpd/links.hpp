#pragma once

// Links between sections: which section a link of a page stands in, and which
// section it jumps to.

#include <span>
#include <vector>

#include "htpd/types.hpp"

namespace htpd {

// Fills Section::links. `page_links[p]` are the links of page p, and
// `page_transforms[p]` turns PDF user space of page p into display space.
//
// * A link stands in the section whose text holds the first line under its box.
// * It jumps to the section of the first line at or below the point it jumps
//   to (a heading right below that point wins); a link to a page as a whole
//   jumps to the section of the first line of that page.
// * Links to the section they stand in, and links whose words hold no letter
//   (page numbers of a list of contents), are left out; the same words
//   linking to the same section are kept once.
void attach_links(std::vector<Section>& sections, std::span<const Line> lines,
                  std::span<const std::vector<PageLink>> page_links, std::span<const Affine> page_transforms);

}  // namespace htpd
