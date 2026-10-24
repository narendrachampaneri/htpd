// HTPD - the table engine.
//
// Three ways to know where the cells of a table are, used in this order:
//
//   Tags     the structure tree of a tagged PDF (tags.hpp) - exact, when it can be trusted.
//   Lattice  the lines drawn on the page: a grid of horizontal and vertical rules and the
//            cells it makes, with cells that span rows or columns. The text is put into
//            the cells by position. It is used only when the grid is consistent (every
//            word lies inside one cell, most cells hold text); otherwise the page is left
//            to the next method.
//   Stream   the positions of the text alone: columns from the strips the rows leave free
//            (columns.cpp), rows from bands across the page (sections.cpp, using
//            `Line::continues`, set here: a line that holds only the rest of cells of the
//            row above).
//
// Whenever neither method is certain, the lines stay as they stand (nothing is moved into a
// cell it does not stand in, nothing is left out).
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "htpd/types.hpp"

namespace htpd {

// Which methods read tables. Auto: tags, lattice and stream as described above. Text: never the
// lines drawn on the page (the table engine reads the text alone, as before it had the lattice
// method).
enum class TableMode { Auto, Text };

// What becomes of a table that goes on over a page break. Split: one block per page, each under the
// mark of its page. Join: one block, with a row-level page mark (`<!-- page N -->` as a line of its
// own between two rows) where the page changes - when the columns match and the part on the next page
// begins with the header again, or the table ends at the bottom of its page and goes on at the top of
// the next one (FORMAT.md 4.2).
enum class TablePages { Split, Join };

// ---- the text of a cell --------------------------------------------------------------------

// Whether the next line of a table cell continues the last word of the cell without a space: a
// name broken inside itself because the column is too narrow ("UART2_" / "RX", "LCD_P2" / "4",
// "field" / "_2", "($1,433,600.00" / ")", "US-GAAP" / "-1010", "($1,433,600." / "00)"). Only when
// that is certain; otherwise the line break is a space, as everywhere. `fills` tells that the line
// before is one word as wide as its column: then a very short rest ("DEFAU" / "LT") is the end
// of that word.
bool continues_word(std::string_view cell, std::string_view next, bool fills);

// Appends the next line of a table cell (see continues_word).
void join_cell_line(std::string& cell, std::string_view next, bool fills);

// ---- lattice -------------------------------------------------------------------------------

struct LatticeOptions {
    float snap = 2.0f;             // rules closer than this (points) are one line
    float reach = 2.0f;            // a rule this near a crossing rule touches it
    float min_filled = 0.4f;       // at least this share of the cells must hold text
    std::size_t max_cells = 4000;  // a bigger grid is not read
};

struct LatticeReport {
    std::size_t tables = 0;        // tables made from rules
    std::size_t rows = 0;
    std::size_t spans = 0;         // cells that span rows or columns
    std::size_t left = 0;          // rule grids that were not consistent and left to the next method
};

// The first id of a table made here; a tagged table has a smaller one.
inline constexpr std::int32_t kLatticeTableId = 1 << 20;

// Finds the tables the rules of one page draw and replaces their lines with one line per row
// (`Line::cells`, `Line::tag_table` as for the tables of the tags). `lines` are the lines of the
// page in reading order; `next_id` numbers the tables (kept between pages).
LatticeReport find_lattice_tables(std::vector<Line>& lines, std::span<const Rule> rules, std::int32_t& next_id,
                                  const LatticeOptions& options = {});

// Lines in a monospace font that are the rows of a table and not a listing: at least four lines
// one under the other, each with two or more sure column gaps, and the first two gaps of all of
// them have a stretch of the page in common. `mono[i]` tells the monospace lines. Returns a mask
// of the lines that are table rows (the others are left to the listing rules).
std::vector<char> monospace_table_rows(const std::vector<Line>& lines, std::span<const char> mono);

// ---- stream ---------------------------------------------------------------------------------

// The lines [begin, end) are the rows of a table set by its text, whose columns are separated at
// the places `cuts` (x, ascending). A line with nothing in the first column that holds only the
// rest of cells of the row above (cells that wrap: `Line::continues` = 1), or the beginning of
// cells whose row comes below it (cells set in the middle of tall rows: 2) is marked. Evidence,
// all of it needed: the first column is empty in the line and is filled in most lines that have
// cells; no piece is a number (a number under a number is a row, not the rest of a cell); the line
// stands as close to its row as the lines of one cell do - closer than rows do when the rows are
// set apart. Returns the number of lines marked.
std::size_t mark_row_continuations(std::vector<Line>& lines, std::size_t begin, std::size_t end,
                                   std::span<const float> cuts);

}  // namespace htpd
