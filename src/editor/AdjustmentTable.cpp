#include "AdjustmentTable.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace AdjustmentTable {

namespace {

using Settings = EditRecipe::Settings;

enum class Kind { Integer, Decimal, Switch };

struct Entry
{
    const char *name;
    Kind kind;
    int Settings::*integer;
    float Settings::*decimal;
    bool Settings::*toggle;
    double minimum;
    double maximum;
    double tolerance;   // a smaller change of a decimal is no change
};

constexpr Entry whole(const char *name, int Settings::*field, double minimum, double maximum)
{
    return {name, Kind::Integer, field, nullptr, nullptr, minimum, maximum, 0.0};
}

constexpr Entry decimal(const char *name, float Settings::*field, double minimum, double maximum, double tolerance)
{
    return {name, Kind::Decimal, nullptr, field, nullptr, minimum, maximum, tolerance};
}

constexpr Entry toggle(const char *name, bool Settings::*field)
{
    return {name, Kind::Switch, nullptr, nullptr, field, 0.0, 1.0, 0.0};
}

const Entry entries[] = {
    decimal("brightness", &Settings::brightness, -5.0, 5.0, 0.0),
    whole("contrast", &Settings::contrast, -100, 100),
    whole("blur", &Settings::blur, 0, 20),
    toggle("grayscale", &Settings::grayscale),
    whole("shadows", &Settings::shadows, -100, 100),
    whole("blacks", &Settings::blacks, -100, 100),
    whole("highlights", &Settings::highlights, -100, 100),
    whole("whites", &Settings::whites, -100, 100),
    whole("temperature", &Settings::temperature, 2000, 50000),
    whole("tint", &Settings::tint, -150, 150),
    whole("vibrance", &Settings::vibrance, -100, 100),
    whole("saturation", &Settings::saturation, -100, 100),
    whole("texture", &Settings::texture, -100, 100),
    whole("clarity", &Settings::clarity, -100, 100),
    whole("dehaze", &Settings::dehaze, -100, 100),
    whole("grain", &Settings::grain, 0, 100),
    whole("denoiseLuminance", &Settings::denoiseLuminance, 0, 100),
    whole("denoiseChrominance", &Settings::denoiseChrominance, 0, 100),
    whole("sharpeningAmount", &Settings::sharpeningAmount, 0, 200),
    decimal("sharpeningRadius", &Settings::sharpeningRadius, 0.3, 3.0, 0.0001),
    whole("sharpeningMasking", &Settings::sharpeningMasking, 0, 100),
};

const Entry *find(const QString &name)
{
    const auto found = std::find_if(std::begin(entries), std::end(entries),
                                    [&name](const Entry &entry) { return name == QLatin1String(entry.name); });
    return found == std::end(entries) ? nullptr : found;
}

} // namespace

Written write(Settings &settings, const QString &name, double value)
{
    const Entry *entry = find(name);
    if (!entry || !std::isfinite(value))
        return Written::Unknown;
    const double bounded = std::clamp(value, entry->minimum, entry->maximum);
    switch (entry->kind) {
    case Kind::Integer: {
        const int next = static_cast<int>(bounded);
        if (settings.*(entry->integer) == next)
            return Written::Unchanged;
        settings.*(entry->integer) = next;
        return Written::Changed;
    }
    case Kind::Decimal: {
        const float next = static_cast<float>(bounded);
        const float current = settings.*(entry->decimal);
        if (current == next || std::abs(double(current) - double(next)) < entry->tolerance)
            return Written::Unchanged;
        settings.*(entry->decimal) = next;
        return Written::Changed;
    }
    case Kind::Switch: {
        const bool next = value != 0.0;
        if (settings.*(entry->toggle) == next)
            return Written::Unchanged;
        settings.*(entry->toggle) = next;
        return Written::Changed;
    }
    }
    return Written::Unknown;
}

bool read(const Settings &settings, const QString &name, double &value)
{
    const Entry *entry = find(name);
    if (!entry)
        return false;
    switch (entry->kind) {
    case Kind::Integer: value = settings.*(entry->integer); break;
    case Kind::Decimal: value = settings.*(entry->decimal); break;
    case Kind::Switch: value = settings.*(entry->toggle) ? 1.0 : 0.0; break;
    }
    return true;
}

} // namespace AdjustmentTable
