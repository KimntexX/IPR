#include "UiPreferences.h"

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QWindow>

#include <algorithm>
#include <utility>

namespace {
const QMap<QString, QPair<int, int>> integerRanges{
    {"window/normalWidth", {1000, 8192}}, {"window/normalHeight", {650, 8192}},
    {"window/normalX", {-200000, 200000}}, {"window/normalY", {-200000, 200000}},
    {"layout/settingsWidth", {260, 360}}, {"layout/imageInfoWidth", {210, 300}},
    {"layout/imageInfoFontSize", {10, 20}},
    {"layout/filmstripHeight", {100, 260}}, {"selection/toneCurveChannel", {0, 3}},
    {"selection/mixerBand", {0, 7}}, {"selection/gradingZone", {0, 3}},
    {"export/quality", {1, 100}}, {"meta/schemaVersion", {2, 2}}
};
}

QString UiPreferences::defaultFilePath()
{
    const QString portable = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("config/ipr-ui.ini"));
    if (QFileInfo(portable).isFile())
        return portable;
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("ipr-ui.ini"));
}

UiPreferences::UiPreferences(const QString &filePath, QObject *parent)
    : QObject(parent),
      m_settings(filePath.isEmpty() ? defaultFilePath() : filePath, QSettings::IniFormat)
{
    // A static library's resource object must be pulled in explicitly.
    static const bool resourcesInitialized = [] {
        Q_INIT_RESOURCE(ipr_ui_defaults);
        return true;
    }();
    Q_UNUSED(resourcesInitialized);
    QSettings defaults(QStringLiteral(":/config/ipr-ui.ini"), QSettings::IniFormat);
    for (const QString &key : defaults.allKeys())
        m_defaults.insert(key, defaults.value(key));

    // Force parsing before deciding whether the file can safely be updated.
    m_settings.allKeys();
    const int version = m_settings.value(QStringLiteral("meta/schemaVersion"), 1).toInt();
    m_writable = m_settings.status() == QSettings::NoError && version <= 2;
    if (!m_writable)
        qWarning() << "UI preferences read-only (invalid file or newer schema):" << m_settings.fileName();

    for (auto it = m_defaults.cbegin(); it != m_defaults.cend(); ++it) {
        bool valid = false;
        const QVariant input = m_settings.status() == QSettings::NoError
            ? m_settings.value(it.key(), it.value()) : it.value();
        QVariant loaded = normalized(it.key(), input, &valid);
        if (!valid)
            loaded = normalized(it.key(), it.value(), &valid);
        m_values.insert(it.key(), loaded);
        if (!m_settings.contains(it.key()) || loaded != input)
            m_pending.insert(it.key());
    }
    if (m_writable && version <= 1 &&
        m_values.value(QStringLiteral("selection/mixerBand")).toInt() == 6) {
        // The last band in schema 1 was magenta; violet is newly inserted at 6.
        m_values.insert(QStringLiteral("selection/mixerBand"), 7);
        m_pending.insert(QStringLiteral("selection/mixerBand"));
    }
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &UiPreferences::sync);
    // Materialize defaults once; subsequent edits are debounced and atomic.
    sync();
}

UiPreferences::~UiPreferences()
{
    sync();
}

QString UiPreferences::filePath() const { return m_settings.fileName(); }
QVariant UiPreferences::value(const QString &key) const { return m_values.value(key); }

QVariantMap UiPreferences::theme() const
{
    QVariantMap result;
    for (const QString &key : {QStringLiteral("background"), QStringLiteral("panel"),
                              QStringLiteral("canvas"), QStringLiteral("text"),
                              QStringLiteral("secondaryText")})
        result.insert(key, QColor(value(QStringLiteral("theme/") + key).toString()));
    return result;
}

QVariantMap UiPreferences::layout() const
{
    QVariantMap result;
    for (const QString &key : {QStringLiteral("settingsWidth"), QStringLiteral("imageInfoWidth"),
                              QStringLiteral("filmstripHeight"), QStringLiteral("imageInfoFontSize"), QStringLiteral("showImageInfo"),
                              QStringLiteral("showHistogram"), QStringLiteral("showFilmstrip")})
        result.insert(key, value(QStringLiteral("layout/") + key));
    return result;
}

QVariant UiPreferences::normalized(const QString &key, const QVariant &input, bool *ok) const
{
    *ok = true;
    if (integerRanges.contains(key)) {
        bool parsed = false;
        const qlonglong number = input.toString().toLongLong(&parsed);
        const auto range = integerRanges.value(key);
        *ok = parsed && number >= range.first && number <= range.second;
        return *ok ? QVariant(static_cast<int>(number)) : QVariant{};
    }
    if (key.startsWith(QStringLiteral("sections/")) ||
        key.startsWith(QStringLiteral("layout/show")) || key == QStringLiteral("window/positionSaved")) {
        const QString text = input.toString().trimmed().toLower();
        *ok = text == QStringLiteral("true") || text == QStringLiteral("false") ||
              text == QStringLiteral("1") || text == QStringLiteral("0");
        return *ok ? QVariant(text == QStringLiteral("true") || text == QStringLiteral("1")) : QVariant{};
    }
    if (key.startsWith(QStringLiteral("theme/"))) {
        const QColor color(input.toString());
        *ok = color.isValid() && color.alpha() == 255;
        return *ok ? QVariant(color.name(QColor::HexRgb)) : QVariant{};
    }
    if (key == QStringLiteral("window/state")) {
        const QString text = input.toString().toLower();
        *ok = text == QStringLiteral("normal") || text == QStringLiteral("maximized") ||
              text == QStringLiteral("fullscreen");
        return *ok ? QVariant(text) : QVariant{};
    }
    if (key == QStringLiteral("export/format")) {
        const QString text = input.toString().toUpper();
        *ok = QStringList{"JPEG", "PNG", "TIFF", "DNG"}.contains(text);
        return *ok ? QVariant(text) : QVariant{};
    }
    // Future keys are retained as opaque strings, never interpreted/executed.
    return input.toString();
}

bool UiPreferences::setValue(const QString &key, const QVariant &input)
{
    if (!m_writable || !m_defaults.contains(key) || key == QStringLiteral("meta/schemaVersion"))
        return false;
    bool valid = false;
    const QVariant normalizedValue = normalized(key, input, &valid);
    if (!valid)
        return false;
    if (m_values.value(key) == normalizedValue)
        return true;
    m_values.insert(key, normalizedValue);
    m_pending.insert(key);
    m_saveTimer.start();
    emit preferencesChanged();
    return true;
}

bool UiPreferences::sectionExpanded(const QString &section) const
{
    return value(QStringLiteral("sections/") + section).toBool();
}

bool UiPreferences::setSectionExpanded(const QString &section, bool expanded)
{
    return setValue(QStringLiteral("sections/") + section, expanded);
}

bool UiPreferences::sync()
{
    m_saveTimer.stop();
    if (!m_writable)
        return false;
    if (!m_pending.isEmpty()) {
        const QString directory = QFileInfo(m_settings.fileName()).absolutePath();
        if (!QDir().mkpath(directory)) {
            m_writable = false;
            emit writableChanged();
            qWarning() << "Cannot create UI preferences directory:" << directory;
            return false;
        }
        for (const QString &key : std::as_const(m_pending))
            m_settings.setValue(key, m_values.value(key));
    }
    m_settings.sync(); // QSettings requires atomic synchronization by default.
    if (m_settings.status() != QSettings::NoError) {
        m_writable = false;
        emit writableChanged();
        qWarning() << "Cannot save UI preferences:" << m_settings.fileName();
        return false;
    }
    m_pending.clear();
    return true;
}

QRect UiPreferences::fitGeometry(const QRect &requested, const QRect &available,
                                const QSize &minimumSize, bool positionSaved)
{
    if (!available.isValid())
        return requested;
    // Respect application minima; on a tiny/high-DPI screen the title bar
    // remains reachable even if the whole UI cannot fit without redesign.
    const int width = std::max(minimumSize.width(), std::min(requested.width(), available.width()));
    const int height = std::max(minimumSize.height(), std::min(requested.height(), available.height()));
    QRect result(QPoint{}, QSize(width, height));
    result.moveCenter(available.center());
    if (positionSaved && requested.intersects(available))
        result.moveTopLeft(requested.topLeft());
    result.moveLeft(std::clamp(result.left(), available.left(),
                              std::max(available.left(), available.right() - width + 1)));
    result.moveTop(std::clamp(result.top(), available.top(),
                             std::max(available.top(), available.bottom() - height + 1)));
    return result;
}

void UiPreferences::restoreWindow(QWindow *window)
{
    if (!window || m_window)
        return;
    m_window = window;
    m_restoringWindow = true;
    const QRect requested(value("window/normalX").toInt(), value("window/normalY").toInt(),
                          value("window/normalWidth").toInt(), value("window/normalHeight").toInt());
    QScreen *screen = QGuiApplication::primaryScreen();
    int bestIntersection = 0;
    if (value("window/positionSaved").toBool()) {
        for (QScreen *candidate : QGuiApplication::screens()) {
            const QRect intersection = candidate->availableGeometry().intersected(requested);
            const int area = intersection.width() * intersection.height();
            if (area > bestIntersection) {
                bestIntersection = area;
                screen = candidate;
            }
        }
    }
    if (screen) {
        window->setScreen(screen);
        window->setGeometry(fitGeometry(requested, screen->availableGeometry(),
                                      window->minimumSize(), value("window/positionSaved").toBool()));
    }
    const QString state = value("window/state").toString();
    if (state == QStringLiteral("maximized")) window->showMaximized();
    else if (state == QStringLiteral("fullscreen")) window->showFullScreen();
    else window->showNormal();
    m_restoringWindow = false;
    connect(window, &QWindow::xChanged, this, &UiPreferences::rememberGeometry);
    connect(window, &QWindow::yChanged, this, &UiPreferences::rememberGeometry);
    connect(window, &QWindow::widthChanged, this, &UiPreferences::rememberGeometry);
    connect(window, &QWindow::heightChanged, this, &UiPreferences::rememberGeometry);
    connect(window, &QWindow::windowStateChanged, this, &UiPreferences::rememberWindowState);
}

void UiPreferences::rememberGeometry()
{
    if (!m_window || m_restoringWindow || !m_window->isVisible() ||
        m_window->windowState() != Qt::WindowNoState)
        return;
    const QRect rect = m_window->geometry();
    setValue("window/normalX", rect.x());
    setValue("window/normalY", rect.y());
    // A fitted window on a small screen can be below the normal application
    // minimum. Retain the configured dimensions in that case.
    if (rect.width() >= 1000) setValue("window/normalWidth", rect.width());
    if (rect.height() >= 650) setValue("window/normalHeight", rect.height());
    setValue("window/positionSaved", true);
}

void UiPreferences::rememberWindowState()
{
    if (!m_window || m_restoringWindow || m_window->windowState() == Qt::WindowMinimized)
        return; // Never relaunch minimized or overwrite the normal geometry.
    const Qt::WindowState state = m_window->windowState();
    setValue("window/state", state == Qt::WindowFullScreen ? "fullscreen"
                              : state == Qt::WindowMaximized ? "maximized" : "normal");
}

void UiPreferences::saveWindow(QWindow *window)
{
    if (window && window == m_window) {
        rememberGeometry();
        rememberWindowState();
    }
    sync();
}
