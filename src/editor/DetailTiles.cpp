#include "DetailTiles.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace DetailTiles {

namespace {

ipr::Recti grown(const ipr::Recti &area, int by)
{
    return {area.x - by, area.y - by, area.width + 2 * by, area.height + 2 * by};
}

ipr::Point2d centreOf(const ipr::Recti &area)
{
    return {area.x + area.width / 2.0, area.y + area.height / 2.0};
}

bool contains(const std::vector<Tile> &tiles, const Tile &tile)
{
    return std::find(tiles.cbegin(), tiles.cend(), tile) != tiles.cend();
}

} // namespace

ipr::Recti rectOf(const Tile &tile, const ipr::Size &view)
{
    const ipr::Recti whole(0, 0, view.width, view.height);
    if (tile.column < 0 || tile.row < 0)
        return {};
    const ipr::Recti square(tile.column * Side, tile.row * Side, Side, Side);
    const ipr::Recti cut = square & whole;
    return cut.empty() ? ipr::Recti() : cut;
}

std::vector<Tile> touched(const ipr::Recti &area, const ipr::Size &view)
{
    const ipr::Recti inside = area & ipr::Recti(0, 0, view.width, view.height);
    std::vector<Tile> tiles;
    if (inside.empty())
        return tiles;
    for (int row = inside.y / Side; row <= (inside.y + inside.height - 1) / Side; ++row)
        for (int column = inside.x / Side; column <= (inside.x + inside.width - 1) / Side; ++column)
            tiles.push_back({column, row});
    return tiles;
}

bool kept(const Tile &tile, const ipr::Size &view, const ipr::Recti &visible, int ring)
{
    const ipr::Recti rect = rectOf(tile, view);
    return !rect.empty() && !(rect & grown(visible, std::max(0, ring))).empty();
}

std::vector<Tile> order(const ipr::Size &view, const ipr::Recti &visible, const ipr::Point2d &focus,
                        const ipr::Point2d &motion, int ring)
{
    const std::vector<Tile> seen = touched(visible, view);
    if (seen.empty())
        return {};
    const ipr::Recti inside = visible & ipr::Recti(0, 0, view.width, view.height);
    const double fx = std::clamp(focus.x, double(inside.x), double(inside.x + inside.width - 1));
    const double fy = std::clamp(focus.y, double(inside.y), double(inside.y + inside.height - 1));
    const Tile first{int(fx) / Side, int(fy) / Side};

    // A square spiral: by ring around the first tile, then clockwise from the top.
    const auto spiralKey = [&](const Tile &tile) {
        const int dc = tile.column - first.column, dr = tile.row - first.row;
        const int step = std::max(std::abs(dc), std::abs(dr));
        double angle = std::atan2(double(dc), double(-dr));   // 0 straight up, growing clockwise
        if (angle < 0)
            angle += 2 * 3.14159265358979323846;
        return std::make_tuple(step, angle);
    };
    std::vector<Tile> result = seen;
    std::stable_sort(result.begin(), result.end(),
                     [&](const Tile &a, const Tile &b) { return spiralKey(a) < spiralKey(b); });

    std::vector<Tile> around;
    for (const Tile &tile : touched(grown(inside, std::max(0, ring)), view))
        if (!contains(seen, tile))
            around.push_back(tile);
    const ipr::Point2d centre = centreOf(inside);
    const double length = std::hypot(motion.x, motion.y);
    const auto ringKey = [&](const Tile &tile) {
        const ipr::Point2d at = centreOf(rectOf(tile, view));
        const double dx = at.x - centre.x, dy = at.y - centre.y;
        // Along the motion: straight ahead first, then aside, then behind.
        const double along = length > 0 ? (dx * motion.x + dy * motion.y) / length : 0.0;
        return std::make_tuple(-along, std::hypot(dx, dy));
    };
    std::stable_sort(around.begin(), around.end(),
                     [&](const Tile &a, const Tile &b) { return ringKey(a) < ringKey(b); });
    result.insert(result.end(), around.cbegin(), around.cend());
    return result;
}

} // namespace DetailTiles
