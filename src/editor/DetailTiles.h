#ifndef IPR_EDITOR_DETAILTILES_H
#define IPR_EDITOR_DETAILTILES_H

#include "Types.h"

#include <vector>

// The zoom's full-resolution tiles: which ones a view needs, and in which order. The view is
// the displayed image (the crop) in pixels of the full resolution; a tile is a square of it
// on a grid from its top left corner, cut at its right and bottom edges. No state.
namespace DetailTiles {

constexpr int Side = 512;
// The band around the visible part rendered next, so that a move finds its tiles ready.
constexpr int Ring = 512;

struct Tile
{
    int column = 0;
    int row = 0;
    friend bool operator==(const Tile &a, const Tile &b) { return a.column == b.column && a.row == b.row; }
    friend bool operator!=(const Tile &a, const Tile &b) { return !(a == b); }
};

// The pixels of a tile in the view; empty for a tile beyond it.
ipr::Recti rectOf(const Tile &tile, const ipr::Size &view);
// The tiles an area of the view touches, row by row; none for an area outside the view.
std::vector<Tile> touched(const ipr::Recti &area, const ipr::Size &view);
// Whether a tile is still wanted for this visible area: it touches the area grown by the ring.
bool kept(const Tile &tile, const ipr::Size &view, const ipr::Recti &visible, int ring = Ring);
// The order of the renders: the visible tiles in a spiral from the one under the focus (a
// point of the view, brought inside the visible area), then the ring's tiles, the furthest
// along the motion (the visible area's move since the last view) first, so straight ahead
// before aside and behind last; without a motion, or along it equally, the nearest first.
std::vector<Tile> order(const ipr::Size &view, const ipr::Recti &visible, const ipr::Point2d &focus,
                        const ipr::Point2d &motion, int ring = Ring);

} // namespace DetailTiles

#endif // IPR_EDITOR_DETAILTILES_H
