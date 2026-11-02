#include "EditHistory.h"
#include "RecipeCodec.h"

#include <QCborArray>
#include <QCborMap>

#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

QCborMap recipe(int contrast, int saturation = 0)
{
    EditRecipe::Settings settings;
    settings.contrast = contrast;
    settings.saturation = saturation;
    return RecipeCodec::encodeSettings(settings);
}

int contrastOf(const std::optional<QCborMap> &map)
{
    EditRecipe::Settings settings;
    return map && RecipeCodec::decodeSettings(*map, settings) ? settings.contrast : -1000;
}
}

int main()
{
    bool ok = true;

    {
        // The codec gives back what it encoded, so a restored step never reads as a new edit.
        EditRecipe::Settings settings;
        settings.brightness = 0.37f;
        settings.sharpeningRadius = 1.3f;
        settings.mixerHue[3] = 12;
        settings.pointColor.count = 1;
        settings.pointColor.points[0].source = {1.2, 0.4, 0.7};
        settings.toneCurves[ToneCurve::Luminance] = {{0.0f, 0.05f}, {0.4f, 0.5f}, {1.0f, 1.0f}};
        const QCborMap encoded = RecipeCodec::encodeSettings(settings);
        EditRecipe::Settings decoded;
        ok &= check(RecipeCodec::decodeSettings(encoded, decoded) && RecipeCodec::encodeSettings(decoded) == encoded,
                    "encoding a decoded recipe gives the same map");
    }

    {
        EditHistory history;
        ok &= check(history.isEmpty() && !history.canUndo() && !history.canRedo() && !history.undo(),
                    "an empty history has nothing to undo");
        history.reset(recipe(0));
        ok &= check(!history.record(recipe(0)) && history.size() == 1, "an unchanged recipe is not a step");
        ok &= check(history.record(recipe(10)) && history.record(recipe(20)) && history.size() == 3 &&
                    history.position() == 2, "each change is a step");
        ok &= check(history.changes(2) == QStringList{QStringLiteral("contrast")} && history.changes(0).isEmpty(),
                    "a step names the fields it changed");
        ok &= check(contrastOf(history.undo()) == 10 && contrastOf(history.undo()) == 0 && !history.undo() &&
                    history.canRedo(), "undo walks back to the opening state");
        ok &= check(contrastOf(history.redo()) == 10 && history.position() == 1, "redo walks forward");
        ok &= check(history.record(recipe(10, 30)) && history.size() == 3 && !history.canRedo(),
                    "a new edit after undo drops the steps that could be redone");
        ok &= check(contrastOf(history.moveTo(0)) == 0 && history.position() == 0 && !history.moveTo(7),
                    "any step can be reached directly");
    }

    {
        // Only the last steps are kept.
        EditHistory history;
        history.reset(recipe(-1));
        for (int step = 0; step < EditHistory::MaxSteps + 20; ++step)
            history.record(recipe(step));
        ok &= check(history.size() == EditHistory::MaxSteps && history.position() == EditHistory::MaxSteps - 1,
                    "the history holds at most the last steps");
        ok &= check(contrastOf(history.moveTo(0)) == 20, "the oldest steps are the ones dropped");
    }

    {
        // Removing a step takes its edit out; what later steps changed themselves stays.
        const auto settingsOf = [](const EditHistory &history) {
            EditHistory copy = history;
            EditRecipe::Settings settings;
            RecipeCodec::decodeSettings(*copy.moveTo(copy.position()), settings);
            return settings;
        };
        EditHistory history;
        history.reset(recipe(0));
        history.record(recipe(10));
        history.record(recipe(10, 30));
        history.record(recipe(50, 30));
        ok &= check(!history.removeStep(0) && !history.removeStep(4) && history.size() == 4,
                    "the opening state and absent steps cannot be removed");
        ok &= check(history.removeStep(1) && history.size() == 3 && history.position() == 2,
                    "a removed step leaves the history");
        ok &= check(settingsOf(history).contrast == 50 && settingsOf(history).saturation == 30,
                    "what later steps changed themselves stays");
        ok &= check(history.changes(1) == QStringList{QStringLiteral("saturation")} &&
                    contrastOf(history.moveTo(1)) == 0, "the next step no longer carries the removed edit");
        history.moveTo(2);
        ok &= check(history.removeStep(1) && history.size() == 2 && settingsOf(history).saturation == 0 &&
                    settingsOf(history).contrast == 50, "the removed edit leaves the current recipe");

        EditHistory undone;
        undone.reset(recipe(0));
        undone.record(recipe(10));
        undone.record(recipe(0));
        ok &= check(undone.removeStep(1) && undone.size() == 1 && undone.position() == 0,
                    "a step left with nothing of its own disappears");

        EditHistory positions;
        positions.reset(recipe(0));
        positions.record(recipe(1));
        positions.record(recipe(2));
        positions.record(recipe(3));
        positions.moveTo(1);
        ok &= check(positions.removeStep(3) && positions.position() == 1 && positions.size() == 3,
                    "a step that could be redone goes without moving the current one");
        ok &= check(positions.removeStep(1) && positions.position() == 0 && positions.canRedo(),
                    "removing the current step goes back to the previous state");

        // Inside a list, as the mixer's bands, each element keeps its own story.
        EditRecipe::Settings first, second;
        first.mixerHue[1] = 5;
        second = first;
        second.mixerHue[2] = 7;
        EditHistory bands;
        bands.reset(RecipeCodec::encodeSettings({}));
        bands.record(RecipeCodec::encodeSettings(first));
        bands.record(RecipeCodec::encodeSettings(second));
        ok &= check(bands.removeStep(1) && settingsOf(bands).mixerHue[1] == 0 && settingsOf(bands).mixerHue[2] == 7,
                    "the edit of one band is removed without the next band's");
    }

    {
        // Named snapshots outlive the steps they were taken from.
        EditHistory history;
        history.addSnapshot(QStringLiteral("empty"), 1);
        ok &= check(history.snapshots().isEmpty(), "an empty history has nothing to snapshot");
        history.reset(recipe(5));
        history.addSnapshot(QStringLiteral("before"), 1000);
        history.record(recipe(50));
        history.addSnapshot(QStringLiteral("after"), 2000);
        history.reset(recipe(99));
        ok &= check(history.snapshots().size() == 2 && history.snapshots()[0].name == QStringLiteral("before") &&
                    history.snapshots()[1].createdMs == 2000 && contrastOf(history.snapshot(0)) == 5 &&
                    !history.snapshot(2), "snapshots keep their name, date and recipe");
        ok &= check(history.removeSnapshot(0) && !history.removeSnapshot(3) && history.snapshots().size() == 1 &&
                    contrastOf(history.snapshot(0)) == 50, "a snapshot can be removed");
    }

    {
        // The history round-trips through CBOR, and a damaged one is refused as a whole.
        EditHistory history;
        history.reset(recipe(1));
        history.record(recipe(2));
        history.record(recipe(3));
        history.undo();
        history.addSnapshot(QStringLiteral("mid"), 42);
        const auto copy = EditHistory::fromCbor(history.toCbor());
        ok &= check(copy && copy->size() == 3 && copy->position() == 1 && copy->canRedo() &&
                    copy->snapshots().size() == 1 && copy->snapshots()[0].createdMs == 42 &&
                    contrastOf(copy->snapshot(0)) == 2, "the history round-trips");
        QCborMap damaged = history.toCbor();
        damaged.insert(QStringLiteral("position"), 9);
        ok &= check(!EditHistory::fromCbor(damaged), "a position outside the steps is refused");
        damaged.insert(QStringLiteral("position"), 0);
        damaged.insert(QStringLiteral("steps"), QCborArray());
        ok &= check(!EditHistory::fromCbor(damaged), "a history without steps is refused");
    }

    std::cout << (ok ? "edit history: ok\n" : "edit history: FAILED\n");
    return ok ? 0 : 1;
}
