#include "CfaNoiseStage.h"
#include "RawDevelopment.h"
#include "image_testing.h"

#include <cmath>
#include <iostream>

namespace {

bool passed = true;

void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        passed = false;
    }
}

using imagetest::at;
using Pattern = CfaNoiseStage::Pattern;

const Pattern Rggb = {{{RawInput::Red, RawInput::Green}, {RawInput::Green2, RawInput::Blue}}};
const Pattern Gbrg = {{{RawInput::Green, RawInput::Blue}, {RawInput::Red, RawInput::Green2}}};

void sitesOfPatterns()
{
    const auto rggb = CfaNoiseStage::planeOfSite(Rggb);
    check(rggb && (*rggb)[0][0] == 0 && (*rggb)[0][1] == 1 && (*rggb)[1][0] == 2 && (*rggb)[1][1] == 3,
          "RGGB sites map to R, G1, G2, B");
    const auto gbrg = CfaNoiseStage::planeOfSite(Gbrg);
    check(gbrg && (*gbrg)[0][0] == 1 && (*gbrg)[0][1] == 3 && (*gbrg)[1][0] == 0 && (*gbrg)[1][1] == 2,
          "GBRG sites map by colour, not by position");
    const Pattern twoReds = {{{RawInput::Red, RawInput::Red}, {RawInput::Green, RawInput::Blue}}};
    check(!CfaNoiseStage::planeOfSite(twoReds), "a pattern without four distinct colours is refused");
}

void packAndUnpack()
{
    // Odd size: the last row and column lie outside whole cells and stay as they are.
    ipr::Image mosaic = ipr::Image::create({9, 7}, ipr::Depth::F32, 1);
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 9; ++x)
            at<float>(mosaic, y, x) = float(100 * y + x) - 50.f;
    const auto planes = CfaNoiseStage::pack(mosaic, Gbrg, 10.0);
    check(planes && planes->width == 4 && planes->height == 3 && planes->values.size() == 48, "pack keeps whole cells");
    if (!planes)
        return;
    // Cell (1, 2) of GBRG: red at (3, 4), green2 at (3, 5), green at (2, 4), blue at (2, 5).
    const std::size_t area = 12, cell = 1 * 4 + 2;
    check(std::abs(planes->values[0 * area + cell] - (300.f + 4.f - 50.f) / 10.f) < 1e-5f, "red plane value");
    check(std::abs(planes->values[1 * area + cell] - (200.f + 4.f - 50.f) / 10.f) < 1e-5f, "green-on-blue-row plane value");
    check(std::abs(planes->values[2 * area + cell] - (300.f + 5.f - 50.f) / 10.f) < 1e-5f, "green-on-red-row plane value");
    check(std::abs(planes->values[3 * area + cell] - (200.f + 5.f - 50.f) / 10.f) < 1e-5f, "blue plane value");

    CfaNoiseStage::Planes doubled = *planes;
    for (float &value : doubled.values)
        value *= 2.f;
    ipr::Image target = mosaic.clone();
    check(CfaNoiseStage::unpack(doubled, target, Gbrg, 10.0), "unpack accepts matching planes");
    bool doubledInside = true, untouchedEdge = true;
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 9; ++x) {
            const float before = at<float>(mosaic, y, x), after = at<float>(target, y, x);
            if (y < 6 && x < 8)
                doubledInside = doubledInside && std::abs(after - 2.f * before) < 1e-3f;
            else
                untouchedEdge = untouchedEdge && after == before;
        }
    check(doubledInside, "unpack writes every whole cell back");
    check(untouchedEdge, "the odd last row and column are left as they were");

    CfaNoiseStage::Planes wrong = *planes;
    wrong.width += 1;
    check(!CfaNoiseStage::unpack(wrong, target, Gbrg, 10.0), "unpack refuses mismatched planes");
    check(!CfaNoiseStage::pack(mosaic, Gbrg, 0.0), "pack refuses a non-positive divisor");
    check(!CfaNoiseStage::pack(ipr::Image::create({8, 8}, ipr::Depth::U16, 1), Gbrg, 10.0), "pack needs a float mosaic");
}

void normalizedLaw()
{
    NoiseLaw::Law law;
    for (int site = 0; site < 4; ++site)
        law.positions[size_t(site)] = {double(site + 1), double(10 * (site + 1))};
    const auto rggb = CfaNoiseStage::normalize(law, Rggb, 100.0);
    check(rggb && std::abs(rggb->a[2] - 0.03f) < 1e-7f && std::abs(rggb->b[3] - 0.004f) < 1e-9f,
          "an RGGB law keeps its order, divided by the divisor and its square");
    const auto gbrg = CfaNoiseStage::normalize(law, Gbrg, 100.0);
    check(gbrg && std::abs(gbrg->a[0] - 0.03f) < 1e-7f && std::abs(gbrg->a[3] - 0.02f) < 1e-7f,
          "a GBRG law follows the colour of each measured site");
    NoiseLaw::Law broken = law;
    broken.positions[1].readVariance = 0.0;
    check(!CfaNoiseStage::normalize(broken, Rggb, 100.0), "a non-positive law is refused");
}

RawInput::Input sample()
{
    RawInput::Input input;
    input.pattern = Rggb;
    input.black = {2048, 2048, 2048, 2048};
    input.white = 11222;
    input.asShotMultipliers = {2076, 1024, 1611, 1024};
    input.mosaic = ipr::Image::create({16, 12}, ipr::Depth::U16, 1);
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 16; ++x)
            at<std::uint16_t>(input.mosaic, y, x) = std::uint16_t((x + y) % 5 == 0 ? 1000 : 5000);
    return input;
}

void developmentHook()
{
    RawDevelopment::Options options;
    options.adjustWhiteThreshold = 0.0;
    options.median = Demosaic::ChromaMedian::Off;
    options.policy = RawDevelopment::Policy::Signed;
    float smallestSeen = 0.f;
    int calls = 0;
    options.cfaStage = [&](ipr::Image &mosaic, const Pattern &pattern, QString *) {
        ++calls;
        for (int y = 0; y < mosaic.height(); ++y)
            for (int x = 0; x < mosaic.width(); ++x) {
                smallestSeen = std::min(smallestSeen, at<float>(mosaic, y, x));
                at<float>(mosaic, y, x) = 2952.f;   // a flat, denoised mosaic
            }
        return pattern == Rggb;
    };
    const RawDevelopment::Result result = RawDevelopment::develop(sample(), options);
    check(calls == 1 && result.error.isEmpty() && !result.cameraRgb.empty(), "the stage runs once and the development completes");
    check(smallestSeen == -1048.f, "the stage receives signed counts below the black");
    float spread = 0.f;
    for (int y = 2; y < 10; ++y)
        for (int x = 2; x < 14; ++x)
            spread = std::max(spread, std::abs(at<ipr::Vec3f>(result.cameraRgb, y, x)[1] - at<ipr::Vec3f>(result.cameraRgb, 6, 8)[1]));
    check(spread < 1e-6f, "what the stage writes is what the demosaic reads");

    options.cfaStage = [](ipr::Image &, const Pattern &, QString *error) {
        *error = QStringLiteral("runtime failed");
        return false;
    };
    const RawDevelopment::Result failed = RawDevelopment::develop(sample(), options);
    check(failed.cameraRgb.empty() && failed.error == QStringLiteral("runtime failed"), "a failing stage fails the development");

    bool cancelled = false;
    options.cfaStage = [&](ipr::Image &, const Pattern &, QString *) { cancelled = true; return true; };
    options.cancelled = [&] { return cancelled; };
    const RawDevelopment::Result stopped = RawDevelopment::develop(sample(), options);
    check(stopped.cameraRgb.empty() && !stopped.error.isEmpty(), "a cancellation during the stage stops the development");
}

const char *Catalogue = R"({"schema_version": 1, "cameras": [{"make": "Canon", "model": "EOS R6 Mark III",
  "provenance": {"proof": "bank.json"}, "isos": [
  {"iso": 3200, "positions": [{"shot_slope_dn": 6, "read_variance_dn2": 150}, {"shot_slope_dn": 7, "read_variance_dn2": 160},
                              {"shot_slope_dn": 8, "read_variance_dn2": 170}, {"shot_slope_dn": 9, "read_variance_dn2": 180}]},
  {"iso": 6400, "positions": [{"shot_slope_dn": 12, "read_variance_dn2": 500}, {"shot_slope_dn": 13, "read_variance_dn2": 510},
                              {"shot_slope_dn": 14, "read_variance_dn2": 520}, {"shot_slope_dn": 15, "read_variance_dn2": 530}]}]}]})";

void stageFactory()
{
    QString reason;
    const auto catalog = NoiseLaw::parseCatalog(Catalogue, &reason);
    check(catalog.has_value(), "the factory's catalogue parses");
    if (!catalog)
        return;
    RawInput::Input input = sample();
    input.make = QStringLiteral("Canon");
    input.model = QStringLiteral("EOS R6 Mark III");
    input.iso = 3200.0;
    CfaNoiseStage::NormalizedLaw received;
    CfaNoiseStage::Settings settings;
    settings.catalog = &*catalog;
    settings.measuredPattern = Rggb;
    settings.networkDivisor = 1000.0;
    settings.denoiser = [&](const CfaNoiseStage::Planes &planes, const CfaNoiseStage::NormalizedLaw &law, QString *) {
        received = law;
        CfaNoiseStage::Planes out = planes;
        for (float &value : out.values)
            value += 0.01f;
        return std::optional<CfaNoiseStage::Planes>(out);
    };
    int appliedReports = 0, refusedReports = 0;
    settings.report = [&](bool applied, const QString &) { applied ? ++appliedReports : ++refusedReports; };
    const RawDevelopment::CfaStage stage = CfaNoiseStage::makeStage(input, settings, &reason);
    check(bool(stage), "a measured camera and ISO give a stage");
    if (stage) {
        ipr::Image mosaic = ipr::Image::create({8, 6}, ipr::Depth::F32, 1);
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 8; ++x)
                at<float>(mosaic, y, x) = 100.f;
        check(stage(mosaic, input.pattern, &reason), "the stage succeeds");
        check(appliedReports == 1 && refusedReports == 0, "an applied network is reported once");
        check(std::abs(at<float>(mosaic, 3, 5) - 110.f) < 1e-3f, "the denoiser's planes come back in counts");
        check(std::abs(received.a[1] - 0.007f) < 1e-7f && std::abs(received.b[3] - 0.00018f) < 1e-9f,
              "the denoiser receives the law normalized by the network divisor");
    }
    input.iso = 12800.0;
    check(!CfaNoiseStage::makeStage(input, settings, &reason) && !reason.isEmpty(), "an ISO outside the laws gives no stage");
    input.iso = 3200.0;
    input.model = QStringLiteral("EOS 60D");
    check(!CfaNoiseStage::makeStage(input, settings, &reason), "a camera without laws gives no stage");
    input.model = QStringLiteral("EOS R6 Mark III");
    settings.denoiser = [](const CfaNoiseStage::Planes &, const CfaNoiseStage::NormalizedLaw &, QString *error) {
        *error = QStringLiteral("no GPU");
        return std::optional<CfaNoiseStage::Planes>();
    };
    const RawDevelopment::CfaStage failing = CfaNoiseStage::makeStage(input, settings, &reason);
    ipr::Image mosaic = ipr::Image::create({8, 6}, ipr::Depth::F32, 1);
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 8; ++x)
            at<float>(mosaic, y, x) = 100.f;
    check(failing && failing(mosaic, input.pattern, &reason) && at<float>(mosaic, 2, 2) == 100.f,
          "a failing denoiser leaves the mosaic as decoded and lets the photo open");
    check(refusedReports == 1, "a photo left as decoded is reported as not denoised");
    settings.networkDivisor = 0.0;
    check(!CfaNoiseStage::makeStage(input, settings, &reason), "an incomplete configuration gives no stage");
}

} // namespace

int main()
{
    sitesOfPatterns();
    packAndUnpack();
    normalizedLaw();
    developmentHook();
    stageFactory();
    if (!passed)
        return 1;
    std::cout << "CFA noise stage tests passed\n";
    return 0;
}
