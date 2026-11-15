#ifndef IPR_CORE_EDITHISTORY_H
#define IPR_CORE_EDITHISTORY_H

#include <QCborMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

// Undo history of an image's edits (ADR-0021): each step is the complete recipe
// after an edit, in its CBOR form (RecipeCodec), so that undoing reloads a state
// instead of replaying operations. The last MaxSteps steps are kept; named
// snapshots keep a recipe beyond them. Saved in the .ipr with the recipe.
class EditHistory
{
public:
    static constexpr int MaxSteps = 100;

    struct Snapshot
    {
        QString name;
        qint64 createdMs = 0;   // milliseconds since the epoch, UTC
        QCborMap recipe;
    };

    // Starts over from one step, the given recipe; snapshots are kept.
    void reset(const QCborMap &recipe);
    // The recipe after an edit becomes a step unless it equals the current one.
    // The steps that could have been redone are dropped. True when a step was added.
    bool record(const QCborMap &recipe);
    // The recipe of the step reached, or nothing when there is no such step.
    std::optional<QCborMap> undo();
    std::optional<QCborMap> redo();
    std::optional<QCborMap> moveTo(int step);
    // Takes an edit out of the history: the following steps are replayed without
    // it, each keeping what it changed itself, and one left with nothing of its own
    // disappears. The opening state stays. False for no such step. The current
    // recipe changes when the step was at or before the current one.
    bool removeStep(int step);
    // The current step's recipe, empty without steps; replaceCurrent() corrects it
    // without an edit having happened (the development resolved at opening).
    QCborMap current() const { return m_steps.isEmpty() ? QCborMap() : m_steps.at(m_position); }
    void replaceCurrent(const QCborMap &recipe);

    bool isEmpty() const { return m_steps.isEmpty(); }
    int size() const { return int(m_steps.size()); }
    int position() const { return m_position; }
    bool canUndo() const { return m_position > 0; }
    bool canRedo() const { return m_position + 1 < m_steps.size(); }
    // The top-level fields a step changed from the step before it.
    QStringList changes(int step) const;

    // A snapshot of the current recipe; nothing when the history is empty.
    void addSnapshot(const QString &name, qint64 createdMs);
    bool removeSnapshot(int index);
    std::optional<QCborMap> snapshot(int index) const;
    const QVector<Snapshot> &snapshots() const { return m_snapshots; }

    QCborMap toCbor() const;
    // Nothing when the history is inconsistent: it is then dropped as a whole.
    static std::optional<EditHistory> fromCbor(const QCborMap &map);

private:
    QVector<QCborMap> m_steps;
    int m_position = -1;
    QVector<Snapshot> m_snapshots;
};

#endif // IPR_CORE_EDITHISTORY_H
