#ifndef IPR_RAW_LIBRAWINPUT_H
#define IPR_RAW_LIBRAWINPUT_H

#include "RawInput.h"

class LibRaw;

// RawInput from LibRaw, after unpack() and after IPR has set its own black and
// white levels on the decoder (RawDecoder): the only place the development's
// input depends on LibRaw. An input that RawInput::valid() refuses stays on
// LibRaw's own development.
namespace LibRawInput {

RawInput::Input read(const LibRaw &raw);

} // namespace LibRawInput

#endif // IPR_RAW_LIBRAWINPUT_H
