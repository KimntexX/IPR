#include "DetailTiles.h"

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

using DetailTiles::Tile;

int indexOf(const std::vector<Tile> &tiles, const Tile &tile)
{
    const auto at = std::find(tiles.cbegin(), tiles.cend(), tile);
    return at == tiles.cend() ? -1 : int(at - tiles.cbegin());
}

void gridAndEdges()
{
    const ipr::Size view(1300, 700);
    check(DetailTiles::rectOf({0, 0}, view) == ipr::Recti(0, 0, 512, 512), "a full tile");
    check(DetailTiles::rectOf({2, 1}, view) == ipr::Recti(1024, 512, 276, 188), "a tile cut at the edges");
    check(DetailTiles::rectOf({3, 0}, view).empty() && DetailTiles::rectOf({-1, 0}, view).empty(),
          "no tile beyond the view");
    const std::vector<Tile> all = DetailTiles::touched({0, 0, 1300, 700}, view);
    check(all.size() == 6, "the view's tiles");
    long long area = 0;
    for (const Tile &tile : all)
        area += DetailTiles::rectOf(tile, view).area();
    check(area == 1300LL * 700LL, "the tiles cover the view once");
    check(DetailTiles::touched({511, 511, 2, 2}, view).size() == 4, "a corner touches four tiles");
    check(DetailTiles::touched({2000, 0, 10, 10}, view).empty(), "nothing outside the view");
    check(DetailTiles::touched({-100, -100, 50, 50}, view).empty(), "nothing before the view");
}

void spiralFromTheFocus()
{
    const ipr::Size view(6000, 4000);
    const ipr::Recti visible(1024, 1024, 2560, 1536);   // columns 2..6, rows 2..4
    const std::vector<Tile> order = DetailTiles::order(view, visible, {2400, 1900}, {0, 0});
    const std::vector<Tile> seen = DetailTiles::touched(visible, view);
    check(order.size() > seen.size(), "the ring follows the visible tiles");
    check(order.front() == Tile{4, 3}, "the tile under the focus first");
    // Every visible tile before any of the ring, and each ring of the spiral before the next.
    for (size_t i = 0; i < seen.size(); ++i)
        check(indexOf(seen, order[i]) >= 0, "the visible tiles first");
    int previous = 0;
    for (size_t i = 0; i < seen.size(); ++i) {
        const int step = std::max(std::abs(order[i].column - 4), std::abs(order[i].row - 3));
        check(step >= previous, "a spiral grows ring by ring");
        previous = step;
    }
    // A focus outside the visible area is brought inside it.
    const std::vector<Tile> outside = DetailTiles::order(view, visible, {-500, 9000}, {0, 0});
    check(outside.front() == Tile{2, 4}, "a focus beyond is brought to the nearest visible tile");
    // No tile twice, none beyond the view.
    for (size_t i = 0; i < order.size(); ++i) {
        check(!DetailTiles::rectOf(order[i], view).empty(), "every tile is in the view");
        check(indexOf(order, order[i]) == int(i), "no tile twice");
    }
}

void theRingFollowsTheMotion()
{
    const ipr::Size view(6000, 4000);
    const ipr::Recti visible(1024, 1024, 1024, 1024);   // columns 2..3, rows 2..3
    const std::vector<Tile> right = DetailTiles::order(view, visible, {1536, 1536}, {200, 0});
    const std::vector<Tile> seen = DetailTiles::touched(visible, view);
    check(right.size() == 16, "a ring of one tile around a square of four");
    // Moving right: the first ring tile lies right of the visible area.
    check(DetailTiles::rectOf(right[seen.size()], view).x >= visible.x + visible.width, "ahead of the motion first");
    const std::vector<Tile> left = DetailTiles::order(view, visible, {1536, 1536}, {-200, 0});
    check(DetailTiles::rectOf(left[seen.size()], view).x + DetailTiles::Side <= visible.x, "the other way, the other side");
    // At an edge, the ring stops at the view.
    const std::vector<Tile> corner = DetailTiles::order(view, {0, 0, 512, 512}, {0, 0}, {0, 0});
    check(corner.size() == 4, "no ring beyond the view");
}

void keptAndReleased()
{
    const ipr::Size view(6000, 4000);
    const ipr::Recti visible(2048, 1024, 1024, 1024);   // columns 4..5, rows 2..3
    check(DetailTiles::kept({4, 2}, view, visible), "a visible tile is kept");
    check(DetailTiles::kept({3, 2}, view, visible) && DetailTiles::kept({6, 4}, view, visible), "the ring is kept");
    check(!DetailTiles::kept({2, 2}, view, visible), "beyond the ring, released");
    check(!DetailTiles::kept({4, 2}, view, visible.empty() ? visible : ipr::Recti(5000, 3000, 100, 100)),
          "a tile far behind the move is released");
    check(!DetailTiles::kept({20, 0}, view, visible), "a tile beyond the view is never kept");
}

} // namespace

int main()
{
    gridAndEdges();
    spiralFromTheFocus();
    theRingFollowsTheMotion();
    keptAndReleased();
    if (passed)
        std::cout << "detail tiles: ok\n";
    return passed ? 0 : 1;
}
