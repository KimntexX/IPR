#pragma once

#include "ColorPipeline.h"
#include "ImageMetadata.h"
#include "RawDecoder.h"

namespace ImageImport {

enum class ColorOrigin { CameraDcp, DecoderMatrix, EmbeddedIcc, AssumedSrgb };

struct Provenance
{
    ColorOrigin colorOrigin = ColorOrigin::AssumedSrgb;
    QString decoderId;
    QString decoderVersion;
    QString sourceProfile;
};

struct Capabilities
{
    bool cameraDevelopment = false;
    bool cameraWhiteBalanceContext = false;
    bool profileWithoutRedecode = false;
};

// Common boundary for decoder imports, cached masters and profile retouches.
// A successful result is full-resolution, straight-alpha, finite RGBA float32
// in linear Rec.2020 D65. Signed/extended RGB is allowed; no clipping/resizing.
struct Result
{
    ColorPipeline::WorkingImage image;
    ImageMetadata metadata;
    std::shared_ptr<RawDecoder::DcpSource> dcpSource;
    Provenance provenance;
    Capabilities capabilities;
    QStringList warnings;
    QString error;
};

Result load(const QString &fileName, const RawDecoder::Options &options = {});
// Rebuild derived descriptors and validate the exact same master contract.
// Failure clears image/retained buffers/capabilities, never substitutes a profile.
bool complete(Result &result);
}
