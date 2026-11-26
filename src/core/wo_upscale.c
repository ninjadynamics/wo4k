#include "core/wo_upscale.h"

#include <math.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "neural_upsampler/jpegc_up2x_best_weights.h"

/* --- The network ---------------------------------------------------------------------
 * jpegc_up2x.cpp of neural_upsampler, in C. Every sum is made in the order
 * the library makes it, in single precision, so the bytes are the same
 * (tests/core/test_upscale.c). */

#define RADIUS JPEGC_UP2X_BEST_WIN
#define INPUTS JPEGC_UP2X_BEST_NIN
#define OUTPUTS JPEGC_UP2X_BEST_NOUT
#define WIDEST JPEGC_UP2X_BEST_MAXW
#define LAYERS JPEGC_UP2X_BEST_NLAYERS

_Static_assert(INPUTS == ((2 * RADIUS + 1) * (2 * RADIUS + 1) - 1) * 3 + 3,
               "the inputs are the window's differences from its centre, then the centre");
_Static_assert(JPEGC_UP2X_BEST_IN_ABS == 1 && JPEGC_UP2X_BEST_BASE == 1 && OUTPUTS == 12 &&
                   LAYERS == 2,
               "a 2x2 block of corrections to a bilinear base, from two hidden layers");
_Static_assert(INPUTS * JPEGC_UP2X_BEST_HIDDEN_0 + JPEGC_UP2X_BEST_HIDDEN_0 +
                       JPEGC_UP2X_BEST_HIDDEN_0 * JPEGC_UP2X_BEST_HIDDEN_1 +
                       JPEGC_UP2X_BEST_HIDDEN_1 + JPEGC_UP2X_BEST_HIDDEN_1 * OUTPUTS + OUTPUTS ==
                   JPEGC_UP2X_BEST_NWEIGHTS,
               "the weights are exactly what the layers need");
_Static_assert(JPEGC_UP2X_BEST_HIDDEN_0 <= WIDEST && JPEGC_UP2X_BEST_HIDDEN_1 <= WIDEST,
               "the activation buffers hold every hidden layer");

static const float INV_255 = 1.0f / 255.0f;

static float unit(uint8_t value) {
    return (float)value * INV_255;
}

static int clamp_index(int value, int last) {
    return value < 0 ? 0 : value > last ? last : value;
}

/* The window around (px, py) as differences from its centre, a colour at a
 * time, then the centre itself. Beyond the picture its edge repeats. */
static void features(const uint8_t *in, int width, int height, int px, int py, float *f) {
    const uint8_t *centre = in + ((size_t)py * (size_t)width + (size_t)px) * 4;
    const float c0 = unit(centre[0]);
    const float c1 = unit(centre[1]);
    const float c2 = unit(centre[2]);
    int k = 0;
    for (int dy = -RADIUS; dy <= RADIUS; dy++) {
        const uint8_t *row = in + (size_t)clamp_index(py + dy, height - 1) * (size_t)width * 4;
        for (int dx = -RADIUS; dx <= RADIUS; dx++) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            const uint8_t *q = row + (size_t)clamp_index(px + dx, width - 1) * 4;
            f[k++] = unit(q[0]) - c0;
            f[k++] = unit(q[1]) - c1;
            f[k++] = unit(q[2]) - c2;
        }
    }
    f[k++] = c0 * JPEGC_UP2X_BEST_ABS_GAIN;
    f[k++] = c1 * JPEGC_UP2X_BEST_ABS_GAIN;
    f[k++] = c2 * JPEGC_UP2X_BEST_ABS_GAIN;
}

/* The bilinear base of the 2x2 block, alpha included: four pixels of four
 * values, each a quarter of a pixel from the centre toward its side. */
static void base_block(const uint8_t *in, int width, int height, int px, int py, float *b) {
    static const float weight[2] = {0.75f, 0.25f};
    int k = 0;
    for (int j = 0; j < 2; j++) {
        const int side_y = j ? 1 : -1;
        for (int i = 0; i < 2; i++) {
            const int side_x = i ? 1 : -1;
            float acc[4] = {0, 0, 0, 0};
            for (int ty = 0; ty < 2; ty++) {
                const uint8_t *row = in + (size_t)clamp_index(py + (ty ? side_y : 0), height - 1) *
                                              (size_t)width * 4;
                float r[4] = {0, 0, 0, 0};
                for (int tx = 0; tx < 2; tx++) {
                    const uint8_t *q = row + (size_t)clamp_index(px + (tx ? side_x : 0), width - 1) * 4;
                    r[0] += weight[tx] * unit(q[0]);
                    r[1] += weight[tx] * unit(q[1]);
                    r[2] += weight[tx] * unit(q[2]);
                    r[3] += weight[tx] * unit(q[3]);
                }
                acc[0] += weight[ty] * r[0];
                acc[1] += weight[ty] * r[1];
                acc[2] += weight[ty] * r[2];
                acc[3] += weight[ty] * r[3];
            }
            b[k++] = acc[0];
            b[k++] = acc[1];
            b[k++] = acc[2];
            b[k++] = acc[3];
        }
    }
}

/* The layers: weights then biases for each, a leaky rectifier after the
 * hidden ones. */
static void forward(const float *in, float *out) {
    float first[WIDEST];
    float second[WIDEST];
    const float *p = JPEGC_UP2X_BEST_weights;
    const float *current = in;
    int count = INPUTS;
    float *next = first;
    for (int layer = 0; layer < LAYERS; layer++) {
        const int hidden = JPEGC_UP2X_BEST_hidden[layer];
        const float *weights = p;
        const float *bias = weights + (size_t)hidden * (size_t)count;
        for (int j = 0; j < hidden; j++) {
            float sum = bias[j];
            const float *w = weights + (size_t)j * (size_t)count;
            for (int i = 0; i < count; i++) {
                sum += w[i] * current[i];
            }
            next[j] = sum > 0 ? sum : JPEGC_UP2X_BEST_LEAK * sum;
        }
        p = bias + hidden;
        current = next;
        count = hidden;
        next = next == first ? second : first;
    }
    const float *weights = p;
    const float *bias = weights + (size_t)OUTPUTS * (size_t)count;
    for (int j = 0; j < OUTPUTS; j++) {
        float sum = bias[j];
        const float *w = weights + (size_t)j * (size_t)count;
        for (int i = 0; i < count; i++) {
            sum += w[i] * current[i];
        }
        out[j] = sum;
    }
}

static uint8_t to_byte(float value) {
    const long rounded = lroundf(value * 255.0f);
    return (uint8_t)(rounded < 0 ? 0 : rounded > 255 ? 255 : rounded);
}

typedef struct Pass {
    const uint8_t *in;
    uint8_t *out;
    int width;
    int height;
} Pass;

/* Each pixel of the rows [first, end) becomes its 2x2 block. */
static void double_rows(const Pass *pass, int first, int end) {
    float f[INPUTS];
    float correction[OUTPUTS];
    float base[16];
    const size_t out_stride = (size_t)pass->width * 8;
    for (int py = first; py < end; py++) {
        for (int px = 0; px < pass->width; px++) {
            features(pass->in, pass->width, pass->height, px, py, f);
            forward(f, correction);
            base_block(pass->in, pass->width, pass->height, px, py, base);
            int k = 0;
            for (int j = 0; j < 2; j++) {
                for (int i = 0; i < 2; i++) {
                    uint8_t *q = pass->out + (size_t)(2 * py + j) * out_stride +
                                 (size_t)(2 * px + i) * 4;
                    const float *b = base + (size_t)(2 * j + i) * 4;
                    for (int channel = 0; channel < 3; channel++) {
                        q[channel] = to_byte(b[channel] + correction[k]);
                        k++;
                    }
                    q[3] = to_byte(b[3]);
                }
            }
        }
    }
}

#ifdef _WIN32
#define MAX_THREADS 64

typedef struct Rows {
    const Pass *pass;
    int first;
    int end;
} Rows;

static DWORD WINAPI rows_thread(void *context) {
    const Rows *rows = context;
    double_rows(rows->pass, rows->first, rows->end);
    return 0;
}

/* The rows shared among the processor's threads, this one among them: at
 * least eight rows each, and a thread that cannot start leaves its rows to
 * this one. */
static void double_all(const Pass *pass) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    int threads = info.dwNumberOfProcessors > MAX_THREADS ? MAX_THREADS
                                                           : (int)info.dwNumberOfProcessors;
    if (threads > pass->height / 8) {
        threads = pass->height / 8;
    }
    if (threads < 2) {
        double_rows(pass, 0, pass->height);
        return;
    }
    Rows rows[MAX_THREADS];
    HANDLE started[MAX_THREADS];
    for (int t = 1; t < threads; t++) {
        rows[t].pass = pass;
        rows[t].first = (int)((int64_t)pass->height * t / threads);
        rows[t].end = (int)((int64_t)pass->height * (t + 1) / threads);
        started[t] = CreateThread(NULL, 0, rows_thread, &rows[t], 0, NULL);
    }
    double_rows(pass, 0, pass->height / threads);
    for (int t = 1; t < threads; t++) {
        if (!started[t]) {
            double_rows(pass, rows[t].first, rows[t].end);
        }
    }
    for (int t = 1; t < threads; t++) {
        if (started[t]) {
            WaitForSingleObject(started[t], INFINITE);
            CloseHandle(started[t]);
        }
    }
}
#else
static void double_all(const Pass *pass) {
    double_rows(pass, 0, pass->height);
}
#endif

void wo_upscale_double(const uint8_t *rgba, uint32_t width, uint32_t height, uint8_t *out) {
    Pass pass = {rgba, out, (int)width, (int)height};
    double_all(&pass);
}

/* --- What the network expects ----------------------------------------------------------- */

#define KERNEL_TAPS 5

static double srgb_to_linear(double value) {
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

static double linear_to_srgb(double value) {
    value = value < 0 ? 0 : value > 1 ? 1 : value;
    return value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1 / 2.4) - 0.055;
}

/* The Mitchell-Netravali filter (B = C = 1/3). */
static double mitchell(double x) {
    x = fabs(x);
    if (x < 1) {
        return (7 * x * x * x - 12 * x * x + 16.0 / 3) / 6;
    }
    if (x < 2) {
        return (-7.0 / 3 * x * x * x + 12 * x * x - 20 * x + 32.0 / 3) / 6;
    }
    return 0;
}

/* The trainer halves a picture with the Mitchell filter stretched to twice
 * its width: eight pixels a way, at half-pixel steps. Where every texel is
 * a pair of those pixels the eight weights fall on five texels: the outer
 * two singly, the others in pairs. */
static void texel_kernel(double kernel[KERNEL_TAPS]) {
    double taps[8];
    double sum = 0;
    for (int i = 0; i < 8; i++) {
        taps[i] = mitchell(((double)(i - 4) + 0.5) / 2);
        sum += taps[i];
    }
    for (int i = 0; i < 8; i++) {
        taps[i] /= sum;
    }
    kernel[0] = taps[0];
    kernel[1] = taps[1] + taps[2];
    kernel[2] = taps[3] + taps[4];
    kernel[3] = taps[5] + taps[6];
    kernel[4] = taps[7];
}

bool wo_upscale_prefilter(uint8_t *rgba, uint32_t width, uint32_t height, WoError *error) {
    const size_t texels = (size_t)width * height;
    double *light = wo_alloc_array(texels, 3 * sizeof(double), error);
    double *work = light ? wo_alloc_array(texels, 3 * sizeof(double), error) : NULL;
    if (!work) {
        wo_free(light);
        return false;
    }
    double linear[256];
    for (int i = 0; i < 256; i++) {
        linear[i] = srgb_to_linear(i / 255.0);
    }
    double kernel[KERNEL_TAPS];
    texel_kernel(kernel);
    for (size_t i = 0; i < texels; i++) {
        for (int c = 0; c < 3; c++) {
            light[i * 3 + (size_t)c] = linear[rgba[i * 4 + (size_t)c]];
        }
    }
    /* Down the columns, then along the rows; beyond the picture its edge repeats. */
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            for (int c = 0; c < 3; c++) {
                double sum = 0;
                for (int k = 0; k < KERNEL_TAPS; k++) {
                    int row = clamp_index((int)y + k - KERNEL_TAPS / 2, (int)height - 1);
                    sum += kernel[k] * light[((size_t)row * width + x) * 3 + (size_t)c];
                }
                work[((size_t)y * width + x) * 3 + (size_t)c] = sum;
            }
        }
    }
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            for (int c = 0; c < 3; c++) {
                double sum = 0;
                for (int k = 0; k < KERNEL_TAPS; k++) {
                    int column = clamp_index((int)x + k - KERNEL_TAPS / 2, (int)width - 1);
                    sum += kernel[k] * work[((size_t)y * width + (size_t)column) * 3 + (size_t)c];
                }
                double value = linear_to_srgb(sum) * 255.0 + 0.5;
                rgba[((size_t)y * width + x) * 4 + (size_t)c] =
                    (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
            }
        }
    }
    wo_free(work);
    wo_free(light);
    return true;
}

/* --- See-through texels ----------------------------------------------------------------- */

#define FILL_ROUNDS 8

/* Give the see-through texels the colours of the nearest texels that show:
 * a round at a time, each takes the average of its neighbours that have a
 * colour by then. Eight rounds reach further than any pass looks. */
static bool fill_unseen(uint8_t *rgba, uint32_t width, uint32_t height, WoError *error) {
    const size_t texels = (size_t)width * height;
    size_t unseen = 0;
    for (size_t i = 0; i < texels; i++) {
        unseen += rgba[i * 4 + 3] == 0;
    }
    if (!unseen || unseen == texels) {
        return true;
    }
    double *colour = wo_alloc_array(texels, 3 * sizeof(double), error);
    double *found = colour ? wo_alloc_array(texels, 3 * sizeof(double), error) : NULL;
    uint8_t *known = found ? wo_alloc(texels, error) : NULL;
    uint8_t *fresh = known ? wo_alloc(texels, error) : NULL;
    if (!fresh) {
        wo_free(known);
        wo_free(found);
        wo_free(colour);
        return false;
    }
    for (size_t i = 0; i < texels; i++) {
        known[i] = rgba[i * 4 + 3] != 0;
        for (int c = 0; c < 3; c++) {
            colour[i * 3 + (size_t)c] = known[i] ? rgba[i * 4 + (size_t)c] : 0;
        }
    }
    for (int round = 0; round < FILL_ROUNDS && unseen; round++) {
        memset(fresh, 0, texels);
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                size_t at = (size_t)y * width + x;
                if (known[at]) {
                    continue;
                }
                double total[3] = {0, 0, 0};
                double count = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        int64_t nx = (int64_t)x + dx;
                        int64_t ny = (int64_t)y + dy;
                        if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= width || ny >= height) {
                            continue;
                        }
                        size_t neighbour = (size_t)ny * width + (size_t)nx;
                        if (known[neighbour]) {
                            total[0] += colour[neighbour * 3];
                            total[1] += colour[neighbour * 3 + 1];
                            total[2] += colour[neighbour * 3 + 2];
                            count += 1;
                        }
                    }
                }
                if (count > 0) {
                    fresh[at] = 1;
                    for (int c = 0; c < 3; c++) {
                        found[at * 3 + (size_t)c] = total[c] / count;
                    }
                }
            }
        }
        size_t filled = 0;
        for (size_t i = 0; i < texels; i++) {
            if (fresh[i]) {
                known[i] = 1;
                memcpy(colour + i * 3, found + i * 3, 3 * sizeof(double));
                filled++;
            }
        }
        if (!filled) {
            break;
        }
        unseen -= filled;
    }
    for (size_t i = 0; i < texels; i++) {
        for (int c = 0; c < 3; c++) {
            double value = colour[i * 3 + (size_t)c] + 0.5;
            rgba[i * 4 + (size_t)c] = (uint8_t)(value > 255 ? 255 : value);
        }
    }
    wo_free(fresh);
    wo_free(known);
    wo_free(found);
    wo_free(colour);
    return true;
}

/* --- A texture -------------------------------------------------------------------------- */

#define MAX_SIDE 4096u /* of a texture given; the game's are 256 at most */

/* Filter *pixels as the network expects and double it; *pixels is replaced. */
static bool filter_and_double(uint8_t **pixels, uint32_t width, uint32_t height, WoError *error) {
    uint8_t *doubled = wo_alloc_array((size_t)width * height, 16, error);
    if (!doubled || !wo_upscale_prefilter(*pixels, width, height, error)) {
        wo_free(doubled);
        return false;
    }
    wo_upscale_double(*pixels, width, height, doubled);
    wo_free(*pixels);
    *pixels = doubled;
    return true;
}

/* The upscaled cut-out steps from solid to clear within a pixel, at the
 * texels' own edge, but it leaves a faint skirt some pixels wide on the
 * clear side and ripples on the solid one (the filter works in linear
 * light, where a little light shows as a lot). Below an eighth is clear and
 * above seven eighths solid; between them the step keeps its slope. */
#define CLEAR_BELOW 32u
#define SOLID_ABOVE 223u

static uint8_t firm(uint8_t alpha) {
    if (alpha <= CLEAR_BELOW) {
        return 0;
    }
    if (alpha >= SOLID_ABOVE) {
        return 255;
    }
    return (uint8_t)((alpha - CLEAR_BELOW) * 255u / (SOLID_ABOVE - CLEAR_BELOW));
}

bool wo_upscale_texture(const uint8_t *rgba, uint32_t width, uint32_t height, unsigned scale,
                        WoImage *out, WoError *error) {
    memset(out, 0, sizeof(*out));
    if (!rgba || !width || !height || width > MAX_SIDE || height > MAX_SIDE ||
        (scale != 2 && scale != 4)) {
        return wo_fail(error, WO_ERR_FORMAT, "A texture of %ux%u cannot be upscaled %ux.",
                       (unsigned)width, (unsigned)height, scale);
    }
    const size_t texels = (size_t)width * height;
    bool cut = false;
    for (size_t i = 0; i < texels && !cut; i++) {
        cut = rgba[i * 4 + 3] != 255;
    }
    uint8_t *colour = wo_alloc_array(texels, 4, error);
    uint8_t *mask = colour && cut ? wo_alloc_array(texels, 4, error) : NULL;
    bool ok = colour && (!cut || mask);
    if (ok) {
        memcpy(colour, rgba, texels * 4);
        ok = fill_unseen(colour, width, height, error);
    }
    for (size_t i = 0; ok && i < texels; i++) {
        colour[i * 4 + 3] = 255;
        if (mask) {
            /* The cut-out as a grey picture of its own. */
            memset(mask + i * 4, rgba[i * 4 + 3], 3);
            mask[i * 4 + 3] = 255;
        }
    }
    uint32_t w = width;
    uint32_t h = height;
    for (unsigned done = 1; ok && done < scale; done *= 2) {
        ok = filter_and_double(&colour, w, h, error) &&
             (!mask || filter_and_double(&mask, w, h, error));
        w *= 2;
        h *= 2;
    }
    for (size_t i = 0; ok && mask && i < (size_t)w * h; i++) {
        colour[i * 4 + 3] = firm(mask[i * 4]);
    }
    wo_free(mask);
    if (!ok) {
        wo_free(colour);
        return false;
    }
    out->width = w;
    out->height = h;
    out->pixels = colour;
    return true;
}
