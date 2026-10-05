#include "RawBlackLevelEstimator.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTextStream>

#include <libraw/libraw.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

namespace {

QString csv(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

bool supportedRaw(const QString &path)
{
    const QString extension = QFileInfo(path).suffix().toLower();
    return extension == QStringLiteral("cr2") ||
           extension == QStringLiteral("cr3") ||
           extension == QStringLiteral("dng");
}

QString processFile(const QString &label, const QString &root,
                    const QString &path)
{
    const QFileInfo info(path);
    LibRaw raw;
    int status = raw.open_file(path.toLocal8Bit().constData());
    QString decodeStatus;
    if (status != LIBRAW_SUCCESS) {
        decodeStatus = QString::fromLatin1(libraw_strerror(status));
        QStringList row{csv(label), csv(QDir(root).relativeFilePath(path)),
                        QString::number(info.size())};
        for (int column = 0; column < 22; ++column)
            row.append(QString());
        row.append(QStringLiteral("decode_error"));
        for (int column = 0; column < 8; ++column)
            row.append(QString());
        row.append(csv(decodeStatus));
        return row.join(QLatin1Char(','));
    }

    auto &data = raw.imgdata;
    const QString make = QString::fromLocal8Bit(data.idata.make).trimmed();
    const QString model = QString::fromLocal8Bit(data.idata.model).trimmed();
    const QString lens = QString::fromLocal8Bit(data.lens.Lens).trimmed();
    const QString extension = info.suffix();
    const auto number = [](double value) {
        return QString::number(value, 'g', 8);
    };

    status = raw.unpack();
    RawBlackLevelEstimator::Result estimate;
    if (status == LIBRAW_SUCCESS) {
        RawBlackLevelEstimator::Input input;
        input.extension = extension;
        input.cameraMake = make;
        input.mosaic = data.rawdata.raw_image;
        input.rawWidth = data.sizes.raw_width;
        input.rawHeight = data.sizes.raw_height;
        input.leftMargin = data.sizes.left_margin;
        input.topMargin = data.sizes.top_margin;
        input.activeWidth = data.sizes.width;
        input.activeHeight = data.sizes.height;
        input.maximum = data.color.maximum;
        input.hasCfa = data.idata.filters != 0 && data.idata.colors >= 3;
        input.globalBlack = data.color.black;
        input.rawGlobalBlack = data.rawdata.color.black;
        input.patternBlack = data.color.cblack[4];
        input.rawPatternBlack = data.rawdata.color.cblack[4];
        for (int channel = 0; channel < 4; ++channel) {
            input.channelBlack[channel] = data.color.cblack[channel];
            input.rawChannelBlack[channel] = data.rawdata.color.cblack[channel];
        }
        for (int row = 0; row < 2; ++row) {
            for (int column = 0; column < 2; ++column)
                input.cfaChannels[row * 2 + column] = raw.COLOR(row, column);
        }
        estimate = RawBlackLevelEstimator::estimate(input);
        decodeStatus = QStringLiteral("ok");
    } else {
        estimate.status = RawBlackLevelEstimator::Status::InvalidInput;
        decodeStatus = QString::fromLatin1(libraw_strerror(status));
    }

    QStringList row{
        csv(label),
        csv(QDir(root).relativeFilePath(path)),
        QString::number(info.size()),
        csv(make),
        csv(model),
        csv(lens),
        number(data.other.iso_speed),
        number(data.other.shutter),
        number(data.other.aperture),
        number(data.other.focal_len),
        QString::number(data.sizes.raw_width),
        QString::number(data.sizes.raw_height),
        QString::number(data.sizes.left_margin),
        QString::number(data.sizes.top_margin),
        QString::number(data.sizes.width),
        QString::number(data.sizes.height),
        QString::number(data.color.black),
        QString::number(data.rawdata.color.black),
        QString::number(data.color.maximum),
        QString::number(data.color.cblack[4]),
        QString::number(data.rawdata.color.cblack[4])};
    for (int channel = 0; channel < 4; ++channel)
        row.append(QString::number(data.color.cblack[channel]));
    row.append(RawBlackLevelEstimator::statusName(estimate.status));
    for (unsigned int level : estimate.levels)
        row.append(QString::number(level));
    for (std::uint64_t count : estimate.sampleCounts)
        row.append(QString::number(count));
    row.append(csv(decodeStatus));
    return row.join(QLatin1Char(','));
}

}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream output(stdout);
    if (argc < 3 || (argc - 1) % 2 != 0) {
        output << "Usage: rawBlackLevelCorpusProbe <label> <directory> "
                  "[<label> <directory> ...]\n";
        return 2;
    }

    output << "root_label,relative_path,file_bytes,camera_make,camera_model,"
              "lens,iso,shutter_seconds,aperture,focal_mm,raw_width,raw_height,"
              "left_margin,top_margin,active_width,active_height,global_black,"
              "raw_global_black,white_level,pattern_black,raw_pattern_black,"
              "channel_black_0,channel_black_1,channel_black_2,channel_black_3,"
              "estimate_status,estimated_0,estimated_1,estimated_2,estimated_3,"
              "samples_0,samples_1,samples_2,samples_3,decode_status\n";
    output.flush();

    for (int argument = 1; argument < argc; argument += 2) {
        const QString label = QString::fromLocal8Bit(argv[argument]);
        const QString rootPath = QString::fromLocal8Bit(argv[argument + 1]);
        const QDir root(rootPath);
        if (!root.exists()) {
            output << "ERROR: directory does not exist: " << rootPath
                   << Qt::endl;
            return 1;
        }
        QStringList files;
        QDirIterator iterator(rootPath, QDir::Files | QDir::Readable |
                                             QDir::NoSymLinks,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            if (supportedRaw(path))
                files.append(path);
        }
        std::sort(files.begin(), files.end(), [](const QString &first,
                                                 const QString &second) {
            return QString::compare(first, second, Qt::CaseInsensitive) < 0;
        });
        for (qsizetype index = 0; index < files.size(); ++index) {
            const QString &path = files.at(index);
            output << processFile(label, rootPath, path) << Qt::endl;
            if ((index + 1) % 25 == 0 || index + 1 == files.size()) {
                std::fprintf(stderr, "%s: %lld/%lld RAW traités\n",
                             qPrintable(label),
                             static_cast<long long>(index + 1),
                             static_cast<long long>(files.size()));
            }
        }
    }
    return 0;
}
