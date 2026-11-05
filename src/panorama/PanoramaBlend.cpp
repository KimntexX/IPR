#include "PanoramaBlend.h"

#include "OpenCvBridge.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>

namespace PanoramaBlend {
namespace {

constexpr float darkestForGain = 0.005f;   // of white: below it, the noise decides the ratio
constexpr float brightestForGain = 0.9f;   // of white: above it, clipping does
constexpr int fewestForGain = 32;          // cells that make an overlap worth a constraint
constexpr double gainPull = 0.01;          // towards 1, against one overlap's full weight
constexpr float keptWeight = 1e-4f;        // a covered pixel is never left without a source
// A seam's place needs few levels of cost: fine ones only multiply the paths a cut must push.
constexpr int capacityLevels = 32;
constexpr double lowestRectangleShare = 0.9; // of the largest area, to stay on the bottom of the view

inline float perceptual(const cv::Vec3f &colour, const cv::Vec3d &gains)
{
    const float luminance = 0.25f * colour[0] * float(gains[0]) + 0.5f * colour[1] * float(gains[1]) + 0.25f * colour[2] * float(gains[2]);
    return std::sqrt(std::max(0.0f, luminance));
}

// A plain Dinic on a graph with integer capacities: small overlaps only, by construction of
// the reduced canvas. Iterative, a path can be as long as the overlap.
class MaximumFlow
{
public:
    explicit MaximumFlow(int nodes) : m_first(size_t(nodes), -1), m_level(size_t(nodes)), m_next(size_t(nodes)) {}

    void add(int from, int to, int capacity, int back)
    {
        m_edges.push_back({to, capacity, m_first[size_t(from)]});
        m_first[size_t(from)] = int(m_edges.size()) - 1;
        m_edges.push_back({from, back, m_first[size_t(to)]});
        m_first[size_t(to)] = int(m_edges.size()) - 1;
    }

    // false when cancelled.
    bool run(int source, int sink, const std::function<bool()> &cancelled)
    {
        while (buildLevels(source, sink)) {
            if (cancelled && cancelled())
                return false;
            m_next = m_first;
            while (pushOnePath(source, sink)) {
            }
        }
        return true;
    }

    // After run: what the source still reaches.
    bool reachable(int node) const { return m_level[size_t(node)] >= 0; }

private:
    struct Edge
    {
        int to;
        int capacity;
        int next;
    };

    bool buildLevels(int source, int sink)
    {
        std::fill(m_level.begin(), m_level.end(), -1);
        std::queue<int> waiting;
        m_level[size_t(source)] = 0;
        waiting.push(source);
        while (!waiting.empty()) {
            const int node = waiting.front();
            waiting.pop();
            for (int e = m_first[size_t(node)]; e >= 0; e = m_edges[size_t(e)].next) {
                const Edge &edge = m_edges[size_t(e)];
                if (edge.capacity > 0 && m_level[size_t(edge.to)] < 0) {
                    m_level[size_t(edge.to)] = m_level[size_t(node)] + 1;
                    waiting.push(edge.to);
                }
            }
        }
        return m_level[size_t(sink)] >= 0;
    }

    bool pushOnePath(int source, int sink)
    {
        m_path.clear();
        int node = source;
        while (node != sink) {
            int &e = m_next[size_t(node)];
            while (e >= 0 && !(m_edges[size_t(e)].capacity > 0 && m_level[size_t(m_edges[size_t(e)].to)] == m_level[size_t(node)] + 1))
                e = m_edges[size_t(e)].next;
            if (e >= 0) {
                m_path.push_back(e);
                node = m_edges[size_t(e)].to;
            } else {
                // A dead end: never again in this phase.
                m_level[size_t(node)] = -2;
                if (m_path.empty())
                    return false;
                const int back = m_path.back();
                m_path.pop_back();
                node = m_edges[size_t(back ^ 1)].to;
            }
        }
        int smallest = std::numeric_limits<int>::max();
        for (const int e : m_path)
            smallest = std::min(smallest, m_edges[size_t(e)].capacity);
        for (const int e : m_path) {
            m_edges[size_t(e)].capacity -= smallest;
            m_edges[size_t(e ^ 1)].capacity += smallest;
        }
        return true;
    }

    std::vector<int> m_first;
    std::vector<int> m_level;
    std::vector<int> m_next;
    std::vector<int> m_path;
    std::vector<Edge> m_edges;
};

} // namespace

void levelGains(std::vector<View> &views)
{
    const int count = int(views.size());
    if (count == 0)
        return;
    for (int channel = 0; channel < 3; ++channel) {
        cv::Mat normal = cv::Mat::zeros(count, count, CV_64F), right = cv::Mat::zeros(count, 1, CV_64F);
        for (int k = 0; k < count; ++k)
            normal.at<double>(k, k) = gainPull;
        std::vector<float> ratios;
        for (int a = 0; a < count; ++a) {
            for (int b = a + 1; b < count; ++b) {
                const cv::Rect common = views[size_t(a)].area & views[size_t(b)].area;
                if (common.empty())
                    continue;
                ratios.clear();
                for (int y = common.y; y < common.y + common.height; ++y) {
                    const cv::Vec3f *ca = views[size_t(a)].colour.ptr<cv::Vec3f>(y - views[size_t(a)].area.y);
                    const cv::Vec3f *cb = views[size_t(b)].colour.ptr<cv::Vec3f>(y - views[size_t(b)].area.y);
                    const uchar *wa = views[size_t(a)].whole.ptr<uchar>(y - views[size_t(a)].area.y);
                    const uchar *wb = views[size_t(b)].whole.ptr<uchar>(y - views[size_t(b)].area.y);
                    for (int x = common.x; x < common.x + common.width; ++x) {
                        const int xa = x - views[size_t(a)].area.x, xb = x - views[size_t(b)].area.x;
                        if (!wa[xa] || !wb[xb])
                            continue;
                        const float va = ca[xa][channel], vb = cb[xb][channel];
                        if (va > darkestForGain && vb > darkestForGain && va < brightestForGain && vb < brightestForGain)
                            ratios.push_back(va / vb);
                    }
                }
                if (int(ratios.size()) < fewestForGain)
                    continue;
                const auto middle = ratios.begin() + std::ptrdiff_t(ratios.size() / 2);
                std::nth_element(ratios.begin(), middle, ratios.end());
                // gain(a) * a = gain(b) * b on the overlap: log gain(a) - log gain(b) = -log(a / b).
                const double difference = -std::log(double(*middle));
                normal.at<double>(a, a) += 1.0;
                normal.at<double>(b, b) += 1.0;
                normal.at<double>(a, b) -= 1.0;
                normal.at<double>(b, a) -= 1.0;
                right.at<double>(a) += difference;
                right.at<double>(b) -= difference;
            }
        }
        cv::Mat solved;
        if (!cv::solve(normal, right, solved, cv::DECOMP_CHOLESKY))
            continue;
        for (int k = 0; k < count; ++k)
            views[size_t(k)].gains[channel] = std::exp(solved.at<double>(k));
    }
}

cv::Mat seams(const std::vector<View> &views, const cv::Size &canvas, const std::function<bool()> &cancelled)
{
    cv::Mat labels(canvas, CV_16SC1, cv::Scalar(-1));
    cv::Mat composite(canvas, CV_32FC1, cv::Scalar(0));
    const cv::Rect whole(0, 0, canvas.width, canvas.height);
    for (int index = 0; index < int(views.size()); ++index) {
        if (cancelled && cancelled())
            return {};
        const View &view = views[size_t(index)];
        const cv::Rect area = view.area & whole;
        if (area.empty())
            continue;
        const auto cellOf = [&](int x, int y) { return view.whole.at<uchar>(y - view.area.y, x - view.area.x) != 0; };
        const auto covered = [&](int x, int y) {
            return x >= area.x && y >= area.y && x < area.x + area.width && y < area.y + area.height && cellOf(x, y);
        };

        // The overlap's cells, numbered on its bounding box.
        cv::Mat node(area.size(), CV_32SC1, cv::Scalar(-1));
        int nodes = 0;
        for (int y = area.y; y < area.y + area.height; ++y)
            for (int x = area.x; x < area.x + area.width; ++x)
                if (cellOf(x, y) && labels.at<short>(y, x) >= 0)
                    node.at<int>(y - area.y, x - area.x) = nodes++;

        std::vector<float> difference(static_cast<size_t>(nodes));
        cv::Mat mine(area.size(), CV_32FC1, cv::Scalar(0));
        for (int y = area.y; y < area.y + area.height; ++y) {
            const cv::Vec3f *source = view.colour.ptr<cv::Vec3f>(y - view.area.y) + (area.x - view.area.x);
            float *out = mine.ptr<float>(y - area.y);
            for (int x = 0; x < area.width; ++x)
                out[x] = perceptual(source[x], view.gains);
        }

        std::vector<char> takesNew(static_cast<size_t>(nodes), 0);
        if (nodes > 0) {
            const int source = nodes, sink = nodes + 1;
            MaximumFlow flow(nodes + 2);
            bool holdsOld = false, holdsNew = false;
            constexpr int neighbourX[4] = {1, -1, 0, 0};
            constexpr int neighbourY[4] = {0, 0, 1, -1};
            const int hold = std::numeric_limits<int>::max() / 4;
            for (int y = 0; y < area.height; ++y) {
                for (int x = 0; x < area.width; ++x) {
                    const int here = node.at<int>(y, x);
                    if (here < 0)
                        continue;
                    const float herePair = std::abs(mine.at<float>(y, x) - composite.at<float>(y + area.y, x + area.x));
                    for (int side = 0; side < 4; ++side) {
                        const int nx = x + neighbourX[side], ny = y + neighbourY[side];
                        const int ax = area.x + nx, ay = area.y + ny;
                        const bool inCanvas = ax >= 0 && ay >= 0 && ax < canvas.width && ay < canvas.height;
                        const bool oldThere = inCanvas && labels.at<short>(ay, ax) >= 0;
                        const bool newThere = covered(ax, ay);
                        if (oldThere && !newThere) {
                            flow.add(source, here, hold, 0);
                            holdsOld = true;
                        } else if (newThere && !oldThere) {
                            flow.add(here, sink, hold, 0);
                            holdsNew = true;
                        } else if (oldThere && newThere && (side == 0 || side == 2)) {
                            // Each edge of the grid once: right and down.
                            const int there = node.at<int>(ny, nx);
                            const float therePair = std::abs(mine.at<float>(ny, nx) - composite.at<float>(ay, ax));
                            const float gradient = std::abs(mine.at<float>(y, x) - mine.at<float>(ny, nx)) +
                                                   std::abs(composite.at<float>(y + area.y, x + area.x) - composite.at<float>(ay, ax));
                            // A difference costs less where the image itself varies a lot.
                            const float cost = (herePair + therePair) / (gradient + 0.02f);
                            const int capacity = 1 + int(std::min(cost, 64.0f) * float(capacityLevels) / 64.0f);
                            flow.add(here, there, capacity, capacity);
                        }
                    }
                }
            }
            if (!holdsNew) {
                // Nothing of this view stands alone: it adds nothing.
            } else if (!holdsOld) {
                std::fill(takesNew.begin(), takesNew.end(), char(1));
            } else {
                if (!flow.run(source, sink, cancelled))
                    return {};
                for (int k = 0; k < nodes; ++k)
                    takesNew[size_t(k)] = flow.reachable(k) ? 0 : 1;
            }
        }
        for (int y = 0; y < area.height; ++y) {
            for (int x = 0; x < area.width; ++x) {
                if (!cellOf(area.x + x, area.y + y))
                    continue;
                const int here = node.at<int>(y, x);
                if (here < 0 || takesNew[size_t(here)]) {
                    labels.at<short>(y + area.y, x + area.x) = short(index);
                    composite.at<float>(y + area.y, x + area.x) = mine.at<float>(y, x);
                }
            }
        }
    }
    return labels;
}

cv::Rect lowRectangle(const cv::Mat &mask)
{
    if (mask.empty() || mask.type() != CV_8UC1)
        return {};
    // The largest rectangle standing on each row.
    std::vector<cv::Rect> standing(static_cast<size_t>(mask.rows));
    std::vector<int> heights(size_t(mask.cols) + 1, 0);
    std::vector<int> stack;
    std::int64_t bestArea = 0;
    for (int y = 0; y < mask.rows; ++y) {
        const uchar *row = mask.ptr<uchar>(y);
        for (int x = 0; x < mask.cols; ++x)
            heights[size_t(x)] = row[x] ? heights[size_t(x)] + 1 : 0;
        stack.clear();
        std::int64_t rowArea = 0;
        for (int x = 0; x <= mask.cols; ++x) {
            while (!stack.empty() && heights[size_t(stack.back())] >= heights[size_t(x)]) {
                const int height = heights[size_t(stack.back())];
                stack.pop_back();
                const int left = stack.empty() ? 0 : stack.back() + 1;
                const std::int64_t area = std::int64_t(height) * std::int64_t(x - left);
                if (height > 0 && area > rowArea) {
                    rowArea = area;
                    standing[size_t(y)] = cv::Rect(left, y - height + 1, x - left, height);
                }
            }
            stack.push_back(x);
        }
        bestArea = std::max(bestArea, rowArea);
    }
    for (int y = mask.rows - 1; y >= 0; --y)
        if (bestArea > 0 && double(standing[size_t(y)].area()) >= lowestRectangleShare * double(bestArea))
            return standing[size_t(y)];
    return {};
}

Layer prepareLayer(const View &view, const cv::Mat &labels, int label, double narrowSigma, double wideSigma)
{
    Layer layer;
    layer.area = view.area;
    layer.gains = cv::Vec3f(float(view.gains[0]), float(view.gains[1]), float(view.gains[2]));

    cv::Mat mine(view.area.size(), CV_32FC1, cv::Scalar(0));
    const cv::Rect inCanvas = view.area & cv::Rect(0, 0, labels.cols, labels.rows);
    for (int y = inCanvas.y; y < inCanvas.y + inCanvas.height; ++y) {
        const short *row = labels.ptr<short>(y);
        float *out = mine.ptr<float>(y - view.area.y);
        for (int x = inCanvas.x; x < inCanvas.x + inCanvas.width; ++x)
            if (row[x] == label)
                out[x - view.area.x] = 1.0f;
    }
    layer.narrow = ipr::gaussianBlurred(mine, narrowSigma, ipr::BlurBorder::LastPixel);
    layer.wide = ipr::gaussianBlurred(mine, wideSigma, ipr::BlurBorder::LastPixel);

    // The blur of the frame's own pixels, measured on them alone: what lies outside it says nothing.
    cv::Mat present, weighted;
    view.any.convertTo(present, CV_32FC1, 1.0 / 255.0);
    std::vector<cv::Mat> three{present, present, present};
    cv::Mat presentColour;
    presentColour = ipr::merged(three);
    weighted = ipr::multiplied(view.colour, presentColour);
    cv::Mat blurredColour, blurredPresent;
    blurredColour = ipr::gaussianBlurred(weighted, wideSigma, ipr::BlurBorder::LastPixel);
    blurredPresent = ipr::gaussianBlurred(present, wideSigma, ipr::BlurBorder::LastPixel);
    layer.low.create(view.area.size(), CV_32FC3);
    for (int y = 0; y < layer.low.rows; ++y) {
        const cv::Vec3f *colour = blurredColour.ptr<cv::Vec3f>(y);
        const float *amount = blurredPresent.ptr<float>(y);
        cv::Vec3f *out = layer.low.ptr<cv::Vec3f>(y);
        for (int x = 0; x < layer.low.cols; ++x) {
            const float divide = amount[x] > 1e-6f ? 1.0f / amount[x] : 0.0f;
            out[x] = cv::Vec3f(colour[x][0] * divide * layer.gains[0], colour[x][1] * divide * layer.gains[1], colour[x][2] * divide * layer.gains[2]);
        }
    }
    return layer;
}

void blendBand(const std::vector<Pixels> &pixels, int scale, const cv::Rect &band, cv::Mat &target)
{
    target.create(band.size(), CV_32FC3);
    cv::parallel_for_(cv::Range(0, band.height), [&](const cv::Range &range) {
        std::vector<cv::Vec3f> lowSum(static_cast<size_t>(band.width)), detailSum(static_cast<size_t>(band.width)), lowRow;
        std::vector<float> wideSum(static_cast<size_t>(band.width)), narrowSum(static_cast<size_t>(band.width)), wideRow, narrowRow;
        for (int row = range.start; row < range.end; ++row) {
            const int canvasY = band.y + row;
            std::fill(lowSum.begin(), lowSum.end(), cv::Vec3f(0, 0, 0));
            std::fill(detailSum.begin(), detailSum.end(), cv::Vec3f(0, 0, 0));
            std::fill(wideSum.begin(), wideSum.end(), 0.0f);
            std::fill(narrowSum.begin(), narrowSum.end(), 0.0f);
            for (const Pixels &source : pixels) {
                if (canvasY < source.area.y || canvasY >= source.area.y + source.area.height || !source.layer)
                    continue;
                const Layer &layer = *source.layer;
                const int cells = layer.area.width;
                // The row of the reduced canvas this pixel row falls on, once per layer.
                const float cellY = std::clamp((float(canvasY) + 0.5f) / float(scale) - 0.5f - float(layer.area.y), 0.0f, float(layer.area.height - 1));
                const int topRow = std::min(int(cellY), layer.area.height - 1), bottomRow = std::min(topRow + 1, layer.area.height - 1);
                const float fy = cellY - float(topRow);
                lowRow.resize(size_t(cells));
                wideRow.resize(size_t(cells));
                narrowRow.resize(size_t(cells));
                const cv::Vec3f *lowTop = layer.low.ptr<cv::Vec3f>(topRow), *lowBottom = layer.low.ptr<cv::Vec3f>(bottomRow);
                const float *wideTop = layer.wide.ptr<float>(topRow), *wideBottom = layer.wide.ptr<float>(bottomRow);
                const float *narrowTop = layer.narrow.ptr<float>(topRow), *narrowBottom = layer.narrow.ptr<float>(bottomRow);
                for (int c = 0; c < cells; ++c) {
                    lowRow[size_t(c)] = lowTop[c] * (1.0f - fy) + lowBottom[c] * fy;
                    wideRow[size_t(c)] = wideTop[c] * (1.0f - fy) + wideBottom[c] * fy;
                    narrowRow[size_t(c)] = narrowTop[c] * (1.0f - fy) + narrowBottom[c] * fy;
                }
                const int first = std::max(band.x, source.area.x), last = std::min(band.x + band.width, source.area.x + source.area.width);
                const cv::Vec3f *colour = source.colour.ptr<cv::Vec3f>(canvasY - source.area.y);
                const uchar *valid = source.valid.ptr<uchar>(canvasY - source.area.y);
                for (int x = first; x < last; ++x) {
                    if (!valid[x - source.area.x])
                        continue;
                    const float cellX = std::clamp((float(x) + 0.5f) / float(scale) - 0.5f - float(layer.area.x), 0.0f, float(cells - 1));
                    const int left = std::min(int(cellX), cells - 1), right = std::min(left + 1, cells - 1);
                    const float fx = cellX - float(left);
                    const float wide = wideRow[size_t(left)] * (1.0f - fx) + wideRow[size_t(right)] * fx + keptWeight;
                    const float narrow = narrowRow[size_t(left)] * (1.0f - fx) + narrowRow[size_t(right)] * fx + keptWeight;
                    const cv::Vec3f low = lowRow[size_t(left)] * (1.0f - fx) + lowRow[size_t(right)] * fx;
                    const cv::Vec3f &value = colour[x - source.area.x];
                    const cv::Vec3f levelled(value[0] * layer.gains[0], value[1] * layer.gains[1], value[2] * layer.gains[2]);
                    const size_t at = size_t(x - band.x);
                    lowSum[at] += low * wide;
                    wideSum[at] += wide;
                    detailSum[at] += (levelled - low) * narrow;
                    narrowSum[at] += narrow;
                }
            }
            cv::Vec3f *out = target.ptr<cv::Vec3f>(row);
            for (int x = 0; x < band.width; ++x) {
                const size_t at = size_t(x);
                out[x] = narrowSum[at] > 0.0f ? lowSum[at] * (1.0f / wideSum[at]) + detailSum[at] * (1.0f / narrowSum[at]) : cv::Vec3f(0, 0, 0);
            }
        }
    });
}

} // namespace PanoramaBlend
