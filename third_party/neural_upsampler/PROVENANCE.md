# neural_upsampler

Vendored, unmodified `jpegc_up2x_best_weights.h` and `LICENSE` from Rich
Geldreich's neural_upsampler
(https://github.com/richgel999/neural_upsampler), commit
`c33a4c6aba130e001eb1b442f69d46b392711653` (28 October 2026).

- License: the Unlicense (public domain); see `LICENSE`
- What it is: the weights of the BEST model of a neural 2x image upsampler,
  a network of 11,948 weights that reads the 7x7 pixels around each pixel
  and gives the 2x2 pixels that replace it.

WO4K does not use the library's C++ source. `src/core/wo_upscale.c` runs the
same network in C, expression for expression, and a test holds it to the
library's output byte for byte. It is used by the maintainer command
`wo4k-build upscale`, which makes a set of upscaled textures; nothing of it
is in the released programs.
