#include "EditHistory.h"

#include <QCborArray>
#include <QCborValue>

void EditHistory::reset(const QCborMap &recipe)
{
    m_steps = {recipe};
    m_position = 0;
}

bool EditHistory::record(const QCborMap &recipe)
{
    if (m_steps.isEmpty()) {
        reset(recipe);
        return true;
    }
    if (m_steps.at(m_position) == recipe)
        return false;
    m_steps.resize(m_position + 1);
    m_steps.append(recipe);
    if (m_steps.size() > MaxSteps)
        m_steps.remove(0, m_steps.size() - MaxSteps);
    m_position = int(m_steps.size()) - 1;
    return true;
}

std::optional<QCborMap> EditHistory::undo()
{
    return canUndo() ? moveTo(m_position - 1) : std::nullopt;
}

std::optional<QCborMap> EditHistory::redo()
{
    return canRedo() ? moveTo(m_position + 1) : std::nullopt;
}

std::optional<QCborMap> EditHistory::moveTo(int step)
{
    if (step < 0 || step >= m_steps.size())
        return std::nullopt;
    m_position = step;
    return m_steps.at(step);
}

namespace {
QCborValue replayed(const QCborValue &before, const QCborValue &after, const QCborValue &newBefore);

// What a step changed, set on the state it now follows; maps and lists field by field.
QCborMap replayedMap(const QCborMap &before, const QCborMap &after, const QCborMap &newBefore)
{
    QCborMap result = newBefore;
    for (auto it = after.constBegin(); it != after.constEnd(); ++it)
        result.insert(it.key(), replayed(before.value(it.key()), it.value(), newBefore.value(it.key())));
    for (auto it = before.constBegin(); it != before.constEnd(); ++it)
        if (!after.contains(it.key())) result.remove(it.key());
    return result;
}

QCborValue replayed(const QCborValue &before, const QCborValue &after, const QCborValue &newBefore)
{
    if (before == after)
        return newBefore;
    if (before.isMap() && after.isMap() && newBefore.isMap())
        return replayedMap(before.toMap(), after.toMap(), newBefore.toMap());
    if (before.isArray() && after.isArray() && newBefore.isArray()) {
        const QCborArray from = before.toArray(), to = after.toArray(), onto = newBefore.toArray();
        if (from.size() == to.size() && from.size() == onto.size()) {
            QCborArray result;
            for (qsizetype index = 0; index < to.size(); ++index)
                result.append(replayed(from.at(index), to.at(index), onto.at(index)));
            return result;
        }
    }
    return after;
}
}

bool EditHistory::removeStep(int step)
{
    if (step <= 0 || step >= m_steps.size())
        return false;
    QVector<QCborMap> steps = m_steps.mid(0, step);
    int position = m_position < step ? m_position : step - 1;
    for (int index = step + 1; index < m_steps.size(); ++index) {
        const QCborMap kept = replayedMap(m_steps.at(index - 1), m_steps.at(index), steps.constLast());
        if (kept != steps.constLast())
            steps.append(kept);
        if (index == m_position)
            position = int(steps.size()) - 1;
    }
    m_steps = std::move(steps);
    m_position = position;
    return true;
}

QStringList EditHistory::changes(int step) const
{
    if (step <= 0 || step >= m_steps.size())
        return {};
    const QCborMap &before = m_steps.at(step - 1);
    const QCborMap &after = m_steps.at(step);
    QStringList keys;
    for (auto it = after.constBegin(); it != after.constEnd(); ++it)
        if (before.value(it.key()) != it.value())
            keys.append(it.key().toString());
    for (auto it = before.constBegin(); it != before.constEnd(); ++it)
        if (!after.contains(it.key()))
            keys.append(it.key().toString());
    return keys;
}

void EditHistory::addSnapshot(const QString &name, qint64 createdMs)
{
    if (!m_steps.isEmpty())
        m_snapshots.append({name, createdMs, m_steps.at(m_position)});
}

bool EditHistory::removeSnapshot(int index)
{
    if (index < 0 || index >= m_snapshots.size())
        return false;
    m_snapshots.removeAt(index);
    return true;
}

std::optional<QCborMap> EditHistory::snapshot(int index) const
{
    if (index < 0 || index >= m_snapshots.size())
        return std::nullopt;
    return m_snapshots.at(index).recipe;
}

QCborMap EditHistory::toCbor() const
{
    QCborArray steps;
    for (const QCborMap &step : m_steps)
        steps.append(step);
    QCborArray snapshots;
    for (const Snapshot &snapshot : m_snapshots)
        snapshots.append(QCborMap{{QStringLiteral("name"), snapshot.name},
                                  {QStringLiteral("created"), snapshot.createdMs},
                                  {QStringLiteral("recipe"), snapshot.recipe}});
    return {{QStringLiteral("position"), m_position},
            {QStringLiteral("steps"), steps},
            {QStringLiteral("snapshots"), snapshots}};
}

std::optional<EditHistory> EditHistory::fromCbor(const QCborMap &map)
{
    const QCborArray steps = map.value(QStringLiteral("steps")).toArray();
    const qint64 position = map.value(QStringLiteral("position")).toInteger(-1);
    if (steps.isEmpty() || steps.size() > MaxSteps || position < 0 || position >= steps.size())
        return std::nullopt;
    EditHistory history;
    for (const QCborValue &step : steps) {
        if (!step.isMap())
            return std::nullopt;
        history.m_steps.append(step.toMap());
    }
    history.m_position = int(position);
    for (const QCborValue &entry : map.value(QStringLiteral("snapshots")).toArray()) {
        const QCborMap snapshot = entry.toMap();
        const QCborValue recipe = snapshot.value(QStringLiteral("recipe"));
        if (!recipe.isMap())
            return std::nullopt;
        history.m_snapshots.append({snapshot.value(QStringLiteral("name")).toString(),
                                    snapshot.value(QStringLiteral("created")).toInteger(), recipe.toMap()});
    }
    return history;
}
