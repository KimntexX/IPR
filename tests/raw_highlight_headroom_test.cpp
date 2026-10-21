#include "raw_highlight_headroom.h"
#include <iostream>
#include <limits>

using namespace RawHighlightStudy;
namespace {
void check(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
void close(double actual, double expected, double tolerance, const char *message)
{
    check(std::abs(actual - expected) <= tolerance, message);
}
template<class Action> void rejects(Action action)
{
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument &) { rejected = true; }
    check(rejected, "Invalid input accepted");
}
}

int main()
{
    try {
        const auto identity = [](const Rgb &q) { return q; };
        const auto squared = [](const Rgb &q) {
            return Rgb{q[0]*q[0], q[1]*q[1], q[2]*q[2]};
        };
        // No-op lookup must preserve dark, signed and extended values exactly.
        for (int i = 0; i <= 4000; ++i) {
            const double x = -1.0 + i / 1000.0;
            const Rgb p{x, .37*x, -.23*x};
            check(extendTableHeadroom(p, identity) == p, "Identity lost signed headroom");
            extendTableHeadroom(p, [](const Rgb &query) {
                for (double c : query) check(c >= 0 && c <= 1, "Unbounded table query");
                return query;
            });
        }
        const Rgb interior{.2,.4,.8};
        const auto interiorResult=extendTableHeadroom(interior,squared),interiorExpected=squared(interior);
        for(int c=0;c<3;++c)close(interiorResult[c],interiorExpected[c],1e-15,"In-domain lookup changed");
        const auto above = extendTableHeadroom(Rgb{1.2,1.2,1.2}, squared);
        for (double c : above) close(c, 1.2, 1e-14, "Neutral reserve clipped");
        // C0 continuity at the normalization and negative-component boundaries.
        // This does NOT assert C1 continuity for arbitrary table contents.
        for (double boundary : {0.0,1.0}) {
            const auto a = extendTableHeadroom(Rgb{boundary-1e-8,.3,.1}, squared);
            const auto b = extendTableHeadroom(Rgb{boundary+1e-8,.3,.1}, squared);
            for (int c=0;c<3;++c) close(a[c],b[c],1e-6,"Table extension discontinuity");
        }
        double previous = -1;
        for (int i=0;i<=4000;++i) {
            const double x=i/1000.0;
            const auto neutral=extendTableHeadroom(Rgb{x,x,x},squared);
            check(neutral[0]>=previous,"Synthetic neutral ramp reversed");
            close(neutral[0],neutral[1],0,"Neutral axis changed");previous=neutral[0];
        }
        constexpr double knee=.85; // Study value, no production default.
        previous=-1;
        for(int i=0;i<=4000;++i) {
            const double x=i/1000.0,y=softShoulder(x,knee);
            check(y>=previous && y<=1,"Shoulder reversed or exceeds output bound");
            if(x<=knee)close(y,x,0,"Shoulder modified lower tones");
            previous=y;
        }
        const double epsilon=1e-6;
        close((softShoulder(knee+epsilon,knee)-softShoulder(knee,knee))/epsilon,1,1e-5,"Right shoulder slope");
        close((softShoulder(knee,knee)-softShoulder(knee-epsilon,knee))/epsilon,1,1e-8,"Left shoulder slope");
        const double nan=std::numeric_limits<double>::quiet_NaN();
        rejects([&]{extendTableHeadroom(Rgb{nan,0,0},identity);});
        rejects([&]{extendTableHeadroom(Rgb{0,0,0},[&](const Rgb&){return Rgb{0,nan,0};});});
        rejects([&]{softShoulder(nan,knee);});
        rejects([&]{softShoulder(1,1);});
        rejects([&]{softShoulder(1,0);});
        rejects([&]{softShoulder(1,-1);});
        close(highlightWeight(.1,.25,.75),0,0,"Shadow gate changed");
        close(highlightWeight(.25,.25,.75),0,0,"Gate start changed");
        close(highlightWeight(.5,.25,.75),.5,1e-15,"Gate midpoint changed");
        close(highlightWeight(.75,.25,.75),1,0,"Gate end changed");
        close(highlightWeight(1,.25,.75),1,0,"Highlight gate changed");
        rejects([&]{highlightWeight(.5,.75,.25);});
        previous=0;
        for(int i=0;i<=1000;++i){const double w=highlightWeight(i/1000.,.25,.75);check(w>=previous&&w<=1,"Gate reversed");previous=w;}
        std::cout << "Experimental headroom scaffold passed: identity, signed residual, bounded query, C0 continuity, synthetic ramps, C1 shoulder, invalid input\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';return 1;
    }
}
