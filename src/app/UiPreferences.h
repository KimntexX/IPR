#ifndef UIPREFERENCES_H
#define UIPREFERENCES_H

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

class QWindow;

// Global application preferences; never stores photographic adjustments.
class UiPreferences : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString filePath READ filePath CONSTANT)
    Q_PROPERTY(bool writable READ writable NOTIFY writableChanged)
    Q_PROPERTY(QVariantMap theme READ theme NOTIFY preferencesChanged)
    Q_PROPERTY(QVariantMap layout READ layout NOTIFY preferencesChanged)

public:
    explicit UiPreferences(const QString &filePath = {}, QObject *parent = nullptr);
    ~UiPreferences() override;

    static QString defaultFilePath();
    QString filePath() const;
    bool writable() const { return m_writable; }
    QVariantMap theme() const;
    QVariantMap layout() const;

    Q_INVOKABLE QVariant value(const QString &key) const;
    Q_INVOKABLE bool setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE bool sectionExpanded(const QString &section) const;
    Q_INVOKABLE bool setSectionExpanded(const QString &section, bool expanded);
    Q_INVOKABLE void restoreWindow(QWindow *window);
    Q_INVOKABLE void saveWindow(QWindow *window);
    Q_INVOKABLE bool sync();

    // Pure geometry helper, also tested for monitor removal and invalid input.
    static QRect fitGeometry(const QRect &requested, const QRect &available,
                             const QSize &minimumSize, bool positionSaved);

signals:
    void preferencesChanged();
    void writableChanged();

private:
    QVariant normalized(const QString &key, const QVariant &input, bool *ok) const;
    void rememberGeometry();
    void rememberWindowState();

    QSettings m_settings;
    QVariantMap m_defaults;
    QVariantMap m_values;
    QSet<QString> m_pending;
    QTimer m_saveTimer;
    QPointer<QWindow> m_window;
    bool m_restoringWindow = false;
    bool m_writable = true;
};

#endif
