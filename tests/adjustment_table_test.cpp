#include "AdjustmentTable.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using AdjustmentTable::Written;
using Settings = EditRecipe::Settings;

namespace {

void check(bool condition, const std::string &what)
{
    if (!condition) throw std::runtime_error(what);
}

double valueOf(const Settings &settings, const QString &name)
{
    double value = std::numeric_limits<double>::quiet_NaN();
    check(AdjustmentTable::read(settings, name, value), name.toStdString() + " reads");
    return value;
}

// The range the interface and the recipes have always had: a change here is a change of contract.
struct Range
{
    const char *name;
    double minimum;
    double maximum;
};

const Range ranges[] = {
    {"brightness", -5, 5}, {"contrast", -100, 100}, {"blur", 0, 20}, {"shadows", -100, 100}, {"blacks", -100, 100},
    {"highlights", -100, 100}, {"whites", -100, 100}, {"temperature", 2000, 50000}, {"tint", -150, 150},
    {"vibrance", -100, 100}, {"saturation", -100, 100}, {"texture", -100, 100}, {"clarity", -100, 100},
    {"dehaze", -100, 100}, {"grain", 0, 100}, {"denoiseLuminance", 0, 100}, {"denoiseChrominance", 0, 100},
    {"sharpeningAmount", 0, 200}, {"sharpeningRadius", 0.3, 3}, {"sharpeningMasking", 0, 100},
};

void everyRangeHolds()
{
    for (const Range &range : ranges) {
        const QString name = QString::fromLatin1(range.name);
        Settings settings;
        AdjustmentTable::write(settings, name, range.maximum + 1000);
        check(std::abs(valueOf(settings, name) - range.maximum) < 1e-6, name.toStdString() + " stops at its maximum");
        AdjustmentTable::write(settings, name, range.minimum - 1000);
        check(std::abs(valueOf(settings, name) - range.minimum) < 1e-6, name.toStdString() + " stops at its minimum");
    }
}

void eachNameReachesItsOwnField()
{
    Settings settings;
    AdjustmentTable::write(settings, QStringLiteral("contrast"), 11);
    AdjustmentTable::write(settings, QStringLiteral("shadows"), 12);
    AdjustmentTable::write(settings, QStringLiteral("blacks"), 13);
    AdjustmentTable::write(settings, QStringLiteral("highlights"), 14);
    AdjustmentTable::write(settings, QStringLiteral("whites"), 15);
    AdjustmentTable::write(settings, QStringLiteral("temperature"), 6100);
    AdjustmentTable::write(settings, QStringLiteral("tint"), 16);
    AdjustmentTable::write(settings, QStringLiteral("vibrance"), 17);
    AdjustmentTable::write(settings, QStringLiteral("saturation"), 18);
    AdjustmentTable::write(settings, QStringLiteral("texture"), 19);
    AdjustmentTable::write(settings, QStringLiteral("clarity"), 20);
    AdjustmentTable::write(settings, QStringLiteral("dehaze"), 21);
    AdjustmentTable::write(settings, QStringLiteral("grain"), 22);
    AdjustmentTable::write(settings, QStringLiteral("blur"), 3);
    AdjustmentTable::write(settings, QStringLiteral("denoiseLuminance"), 24);
    AdjustmentTable::write(settings, QStringLiteral("denoiseChrominance"), 25);
    AdjustmentTable::write(settings, QStringLiteral("sharpeningAmount"), 26);
    AdjustmentTable::write(settings, QStringLiteral("sharpeningMasking"), 27);
    AdjustmentTable::write(settings, QStringLiteral("sharpeningRadius"), 1.5);
    AdjustmentTable::write(settings, QStringLiteral("brightness"), 0.5);
    AdjustmentTable::write(settings, QStringLiteral("grayscale"), 1);
    check(settings.contrast == 11 && settings.shadows == 12 && settings.blacks == 13 && settings.highlights == 14, "tones");
    check(settings.whites == 15 && settings.temperature == 6100 && settings.tint == 16, "balance");
    check(settings.vibrance == 17 && settings.saturation == 18, "colour");
    check(settings.texture == 19 && settings.clarity == 20 && settings.dehaze == 21 && settings.grain == 22, "effects");
    check(settings.blur == 3 && settings.denoiseLuminance == 24 && settings.denoiseChrominance == 25, "noise");
    check(settings.sharpeningAmount == 26 && settings.sharpeningMasking == 27 && settings.sharpeningRadius == 1.5f, "sharpening");
    check(settings.brightness == 0.5f && settings.grayscale, "the rest");
}

void whatItAnswers()
{
    Settings settings;
    check(AdjustmentTable::write(settings, QStringLiteral("contrast"), 10) == Written::Changed, "a new value");
    check(AdjustmentTable::write(settings, QStringLiteral("contrast"), 10) == Written::Unchanged, "the same value");
    check(AdjustmentTable::write(settings, QStringLiteral("contrast"), 10.9) == Written::Unchanged, "a whole value truncates");
    check(AdjustmentTable::write(settings, QStringLiteral("contrast"), 500) == Written::Changed, "bounded, new");
    check(AdjustmentTable::write(settings, QStringLiteral("contrast"), 600) == Written::Unchanged, "bounded, the same");
    check(AdjustmentTable::write(settings, QStringLiteral("nothing"), 1) == Written::Unknown, "an unknown name");
    check(AdjustmentTable::write(settings, QString(), 1) == Written::Unknown, "no name");

    const Settings before = settings;
    for (const double invalid : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity()})
        check(AdjustmentTable::write(settings, QStringLiteral("contrast"), invalid) == Written::Unknown, "not a number");
    check(settings.contrast == before.contrast, "and nothing changes");

    double value = 7;
    check(!AdjustmentTable::read(settings, QStringLiteral("nothing"), value) && value == 7, "an unknown name reads nothing");
}

void decimalsAndSwitches()
{
    Settings settings;
    AdjustmentTable::write(settings, QStringLiteral("sharpeningRadius"), 1.5);
    check(AdjustmentTable::write(settings, QStringLiteral("sharpeningRadius"), 1.50001) == Written::Unchanged, "a tiny change is none");
    check(AdjustmentTable::write(settings, QStringLiteral("sharpeningRadius"), 1.6) == Written::Changed, "a real change");
    check(AdjustmentTable::write(settings, QStringLiteral("brightness"), 0.1) == Written::Changed, "exposure");
    check(valueOf(settings, QStringLiteral("brightness")) == double(0.1f), "stored in single precision");
    check(AdjustmentTable::write(settings, QStringLiteral("brightness"), 0.1) == Written::Unchanged, "the same exposure");

    check(valueOf(settings, QStringLiteral("grayscale")) == 0.0, "off");
    check(AdjustmentTable::write(settings, QStringLiteral("grayscale"), 1) == Written::Changed, "on");
    check(valueOf(settings, QStringLiteral("grayscale")) == 1.0, "reads on");
    check(AdjustmentTable::write(settings, QStringLiteral("grayscale"), 5) == Written::Unchanged, "any value but zero is on");
    check(AdjustmentTable::write(settings, QStringLiteral("grayscale"), 0) == Written::Changed, "off again");
}

} // namespace

int main()
{
    try {
        everyRangeHolds();
        eachNameReachesItsOwnField();
        whatItAnswers();
        decimalsAndSwitches();
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    std::cout << "adjustment table: all passed\n";
    return 0;
}
