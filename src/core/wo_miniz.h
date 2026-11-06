/* The single way WO4K includes miniz, so every translation unit agrees on its
 * configuration. File access always goes through WoSource/WoFile (Unicode,
 * long paths), never through miniz's narrow-character stdio helpers. */
#ifndef WO_MINIZ_H
#define WO_MINIZ_H

#ifndef MINIZ_NO_STDIO
#define MINIZ_NO_STDIO
#endif
#ifndef MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#endif

#include "miniz/miniz.h"

#endif
