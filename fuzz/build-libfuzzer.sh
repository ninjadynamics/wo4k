#!/bin/sh
# Build every fuzz harness with clang's libFuzzer, AddressSanitizer and
# UndefinedBehaviorSanitizer (Linux), then run it.
#
#   fuzz/build-libfuzzer.sh           build, then replay each seed corpus once
#   fuzz/build-libfuzzer.sh 60        build, then fuzz each target for 60 s
#
# Environment: CC (default clang), OUT (default build/fuzz-linux).
# New inputs found while fuzzing are kept in $OUT/corpus/<name>.
set -eu
cd "$(dirname "$0")/.."

CC=${CC:-clang}
OUT=${OUT:-build/fuzz-linux}
SECONDS_PER_TARGET=${1:-0}
FLAGS="-std=c11 -g -O1 -Wall -Wextra -Werror -Wshadow -Wvla -Wstrict-prototypes \
-Wmissing-prototypes -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -Isrc"
COMMON="src/core/wo_base.c src/core/wo_io.c src/core/wo_json.c"

build() {
    name=$1
    shift
    echo "building fuzz_$name"
    # shellcheck disable=SC2086
    $CC $FLAGS -o "$OUT/fuzz_$name" "fuzz/fuzz_$name.c" $COMMON "$@" -lm
}

mkdir -p "$OUT"
build iso src/core/wo_iso.c
build cue src/core/wo_disctracks.c
build mds src/core/wo_disctracks.c
build tim src/core/wo_texfmt.c
build tga src/core/wo_texfmt.c
build cmp src/core/wo_texfmt.c
build wad src/core/wo_texfmt.c
build recipe src/core/wo_patch.c
build json
build wav src/core/wo_audioclean.c
build cdaudio src/core/wo_cdaudio.c
build texsrc src/core/wo_texsrc.c src/core/wo_texfmt.c

for name in iso cue mds tim tga cmp wad recipe json wav cdaudio texsrc; do
    corpus="$OUT/corpus/$name"
    mkdir -p "$corpus"
    if [ "$SECONDS_PER_TARGET" -gt 0 ]; then
        "$OUT/fuzz_$name" -max_total_time="$SECONDS_PER_TARGET" -max_len=262144 \
            -rss_limit_mb=4096 "$corpus" "fuzz/corpus/$name"
    else
        "$OUT/fuzz_$name" -runs=0 "fuzz/corpus/$name"
    fi
done
