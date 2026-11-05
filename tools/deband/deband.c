/*
 * deband.c - multithreaded batch image debanding and high-pass dithering.
 * C99. Requires libpng and libjpeg for built-in image I/O.
 * FFmpeg is optional at runtime; when installed, it is the default backend.
 *
 * Linux:
 *   cc -O3 -std=c99 -D_XOPEN_SOURCE=700 -o deband deband.c \
 *      -lpng -ljpeg -lz -lm -pthread
 * MSYS2 UCRT64:
 *   gcc -O3 -std=c99 -D_CRT_SECURE_NO_WARNINGS -o deband.exe deband.c \
 *      -lpng -ljpeg -lz -lm
 *
 * Usage: deband [options] input_file_or_directory
 *  -o DIR    Output directory (default: .)
 *  -x        Overwrite in place (cannot combine with -o)
 *  -s        Force native C deband (FFmpeg is default when available)
 *  -j N      Parallel images (default: 4 FFmpeg, up to 8 native)
 *  -t VAL    Deband threshold, 0.00003..0.5 (default 0.020)
 *  -r N      Deband radius in pixels, 1..128 (default 24)
 *  -d VAL    Dither amplitude in 8-bit levels, 0..4 (default 0.8)
 *  -h        Help
 *
 * Input: PNG/JPEG, 24/32-bit BMP, 24/32-bit uncompressed/RLE TGA.
 * All original alpha values are preserved. With alpha=0, all four bytes
 * (including the hidden RGB data) remain exactly as decoded from input.
 *
 * Output without -x is always lossless PNG and never replaces an existing
 * file. In-place replacement uses a same-directory temp file and rename.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <sys/stat.h>

#include <png.h>
#include <jpeglib.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define PATH_SEP '\\'
#define MKDIR(p) _mkdir(p)
#define STAT _stat64
#define STATBUF struct _stat64
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pthread.h>
#define PATH_SEP '/'
#define MKDIR(p) mkdir((p), 0755)
#define STAT stat
#define STATBUF struct stat
#endif

#define MAX_PATH_SIZE 32768
#define NOISE_SIDE 512
#define NOISE_COUNT (NOISE_SIDE * NOISE_SIDE)

typedef struct {
    char *src;
    char *dst;
    int kind;
} Job;

typedef struct {
    Job *jobs;
    size_t count, cap;
    volatile long next;
    volatile long ok, failed, skipped;
    float threshold, dither;
    int radius, simple, overwrite;
    float *noise;
    float trig_cos[256], trig_sin[256];
} Context;

static const char *global_output_root;
static int global_is_dir;
static int global_prefix;
static int global_overwrite;
static Context *global_ctx;

static char *str_dup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static int is_sep(char c) { return c == '/' || c == '\\'; }
static int lower_ascii(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int eq_ext(const char *p, const char *s) {
    const char *d = strrchr(p, '.');
    if (!d) return 0;
    while (*d && *s) if (lower_ascii((unsigned char)*d++) != lower_ascii((unsigned char)*s++)) return 0;
    return !*d && !*s;
}
static int kind_of(const char *p) {
    if (eq_ext(p, ".png")) return 1;
    if (eq_ext(p, ".jpg") || eq_ext(p, ".jpeg")) return 2;
    if (eq_ext(p, ".bmp")) return 3;
    if (eq_ext(p, ".tga")) return 4;
    return 0;
}
static const char *base_name(const char *p) {
    const char *b = p;
    for (; *p; p++) if (is_sep(*p)) b = p + 1;
    return b;
}
static char *join_path(const char *a, const char *b) {
    size_t n = strlen(a), m = strlen(b);
    int sep = n > 0 && !is_sep(a[n - 1]);
    char *v = (char *)malloc(n + m + sep + 1);
    if (!v) return NULL;
    memcpy(v, a, n);
    if (sep) v[n++] = PATH_SEP;
    memcpy(v + n, b, m + 1);
    return v;
}
static char *parent_dir(const char *path) {
    char *r = str_dup(path);
    char *last = NULL;
    if (!r) return NULL;
    for (char *p = r; *p; p++) if (is_sep(*p)) last = p;
    if (!last) { free(r); return str_dup("."); }
    if (last == r) last[1] = 0;
    else *last = 0;
    return r;
}
static int dir_exists(const char *p) {
    STATBUF s;
    if (STAT(p, &s) != 0) return 0;
#ifdef _WIN32
    return (s.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(s.st_mode);
#endif
}
static int mkdirs(const char *path) {
    char *p = str_dup(path);
    if (!p) return 0;
    size_t len = strlen(p);
    for (size_t i = 1; i <= len; i++) {
        if (i != len && !is_sep(p[i])) continue;
        char saved = p[i];
        p[i] = 0;
#ifdef _WIN32
        if (i == 2 && p[1] == ':') { p[i] = saved; continue; }
#endif
        if (p[0] && !dir_exists(p) && MKDIR(p) != 0 && errno != EEXIST) {
            fprintf(stderr, "ERROR: cannot create directory '%s': %s\n", p, strerror(errno));
            free(p); return 0;
        }
        p[i] = saved;
    }
    free(p);
    return dir_exists(path);
}

/* Opening a temporary file in the target directory tests actual write access. */
static int writable_directory(const char *dir) {
    char *candidate = join_path(dir, ".deband_writecheck_XXXXXXXX");
    if (!candidate) return 0;
    int ok = 0;
#ifdef _WIN32
    char tmp[MAX_PATH_SIZE];
    static LONG sequence = 0;
    snprintf(tmp, sizeof(tmp), "%s.%lu.%ld", candidate,
             (unsigned long)GetCurrentProcessId(), (long)InterlockedIncrement(&sequence));
    HANDLE h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); DeleteFileA(tmp); ok = 1; }
#else
    char tmp[MAX_PATH_SIZE];
    snprintf(tmp, sizeof(tmp), "%s.XXXXXX", candidate);
    int fd = mkstemp(tmp);
    if (fd >= 0) { close(fd); unlink(tmp); ok = 1; }
#endif
    if (!ok) fprintf(stderr, "ERROR: output directory is not writable: %s\n", dir);
    free(candidate);
    return ok;
}

static uint32_t hash_name(const char *p) {
    uint32_t h = 2166136261u;
    while (*p) { h ^= (uint8_t)*p++; h *= 16777619u; }
    return h;
}
static uint32_t mix32(uint32_t n) {
    n ^= n >> 16; n *= 0x7feb352du;
    n ^= n >> 15; n *= 0x846ca68bu;
    n ^= n >> 16; return n;
}
static void init_noise(float *out) {
    float *white = (float *)malloc(NOISE_COUNT * sizeof(float));
    double sq = 0.0;
    if (!white) { fprintf(stderr, "ERROR: out of memory\n"); exit(1); }
    for (uint32_t i = 0; i < NOISE_COUNT; i++) {
        float a = (mix32(i ^ 0xb4d531d5u) >> 8) * (1.0f / 16777216.0f);
        float b = (mix32(i ^ 0x06d22f8bu) >> 8) * (1.0f / 16777216.0f);
        white[i] = a - b;
    }
    for (int y = 0; y < NOISE_SIDE; y++) {
        int ym = ((y - 1) & (NOISE_SIDE - 1)) * NOISE_SIDE;
        int yp = ((y + 1) & (NOISE_SIDE - 1)) * NOISE_SIDE;
        int yy = y * NOISE_SIDE;
        for (int x = 0; x < NOISE_SIDE; x++) {
            float n = white[yy + x] - 0.25f * (
                white[yy + ((x - 1) & (NOISE_SIDE - 1))] +
                white[yy + ((x + 1) & (NOISE_SIDE - 1))] +
                white[ym + x] + white[yp + x]);
            out[yy + x] = n;
            sq += (double)n * n;
        }
    }
    float norm = (float)(1.0 / sqrt(sq / NOISE_COUNT));
    for (int i = 0; i < NOISE_COUNT; i++) out[i] *= norm;
    free(white);
}
static inline float noise_at(const Context *ctx, int x, int y, uint32_t h) {
    int nx = (x + (int)(h & 511u)) & 511;
    int ny = (y + (int)((h >> 9) & 511u)) & 511;
    return ctx->noise[ny * NOISE_SIDE + nx] * ctx->dither;
}
static inline uint8_t quantize(float f) {
    int i = (int)floorf(f + 0.5f);
    return (uint8_t)(i < 0 ? 0 : i > 255 ? 255 : i);
}

/* PNG uses the high-level libpng API to decode directly to RGBA. */
static int png_dimensions(const char *path, int *w, int *h) {
    png_image im;
    memset(&im, 0, sizeof(im)); im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&im, path)) return 0;
    *w = (int)im.width; *h = (int)im.height;
    png_image_free(&im); return 1;
}
static uint8_t *png_read(const char *path, int *w, int *h) {
    png_image im;
    memset(&im, 0, sizeof(im)); im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&im, path)) return NULL;
    *w = (int)im.width; *h = (int)im.height;
    if (*w < 1 || *h < 1 || (uint64_t)*w * *h > SIZE_MAX / 4u) {
        png_image_free(&im); return NULL;
    }
    im.format = PNG_FORMAT_RGBA;
    uint8_t *buffer = (uint8_t *)malloc(PNG_IMAGE_SIZE(im));
    if (!buffer) { png_image_free(&im); return NULL; }
    if (!png_image_finish_read(&im, NULL, buffer, 0, NULL)) {
        free(buffer); png_image_free(&im); return NULL;
    }
    png_image_free(&im); return buffer;
}
static int png_write(const char *path, const uint8_t *rgba, int w, int h) {
    png_image im;
    memset(&im, 0, sizeof(im)); im.version = PNG_IMAGE_VERSION;
    im.width = (png_uint_32)w; im.height = (png_uint_32)h;
    im.format = PNG_FORMAT_RGBA;
    im.flags |= PNG_IMAGE_FLAG_FAST;
    return png_image_write_to_file(&im, path, 0, rgba, 0, NULL) != 0;
}

struct jpg_error { struct jpeg_error_mgr pub; jmp_buf jump; };
static void jpg_error_exit(j_common_ptr cinfo) {
    struct jpg_error *err = (struct jpg_error *)cinfo->err;
    longjmp(err->jump, 1);
}
static int jpg_info(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    struct jpeg_decompress_struct dec;
    struct jpg_error err;
    memset(&dec, 0, sizeof(dec));
    dec.err = jpeg_std_error(&err.pub); err.pub.error_exit = jpg_error_exit;
    if (setjmp(err.jump)) { jpeg_destroy_decompress(&dec); fclose(f); return 0; }
    jpeg_create_decompress(&dec);
    jpeg_stdio_src(&dec, f);
    jpeg_read_header(&dec, TRUE);
    *w = (int)dec.image_width; *h = (int)dec.image_height;
    jpeg_destroy_decompress(&dec); fclose(f); return 1;
}
static uint8_t *jpg_read(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    struct jpeg_decompress_struct dec;
    struct jpg_error err;
    uint8_t *rgba = NULL;
    memset(&dec, 0, sizeof(dec));
    dec.err = jpeg_std_error(&err.pub); err.pub.error_exit = jpg_error_exit;
    if (setjmp(err.jump)) {
        free(rgba); jpeg_destroy_decompress(&dec); fclose(f); return NULL;
    }
    jpeg_create_decompress(&dec);
    jpeg_stdio_src(&dec, f);
    jpeg_read_header(&dec, TRUE);
    dec.out_color_space = JCS_RGB;
    jpeg_start_decompress(&dec);
    *w = (int)dec.output_width; *h = (int)dec.output_height;
    if (*w < 1 || *h < 1 || (uint64_t)*w * *h > SIZE_MAX / 4u) {
        jpeg_destroy_decompress(&dec); fclose(f); return NULL;
    }
    rgba = (uint8_t *)malloc((size_t)*w * *h * 4u);
    if (!rgba) { jpeg_destroy_decompress(&dec); fclose(f); return NULL; }
    JSAMPARRAY scan = (*dec.mem->alloc_sarray)((j_common_ptr)&dec, JPOOL_IMAGE,
                                               (JDIMENSION)(*w * 3), 1);
    while (dec.output_scanline < dec.output_height) {
        size_t y = dec.output_scanline;
        jpeg_read_scanlines(&dec, scan, 1);
        uint8_t *p = rgba + y * (size_t)*w * 4u;
        for (int x = 0; x < *w; x++) {
            p[x * 4 + 0] = scan[0][x * 3 + 0];
            p[x * 4 + 1] = scan[0][x * 3 + 1];
            p[x * 4 + 2] = scan[0][x * 3 + 2];
            p[x * 4 + 3] = 255;
        }
    }
    jpeg_finish_decompress(&dec);
    jpeg_destroy_decompress(&dec); fclose(f); return rgba;
}
static int jpg_write(const char *path, const uint8_t *rgba, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    struct jpeg_compress_struct enc;
    struct jpg_error err;
    uint8_t *row = NULL;
    memset(&enc, 0, sizeof(enc));
    enc.err = jpeg_std_error(&err.pub); err.pub.error_exit = jpg_error_exit;
    if (setjmp(err.jump)) {
        free(row); jpeg_destroy_compress(&enc); fclose(f); return 0;
    }
    jpeg_create_compress(&enc); jpeg_stdio_dest(&enc, f);
    enc.image_width = (JDIMENSION)w; enc.image_height = (JDIMENSION)h;
    enc.input_components = 3; enc.in_color_space = JCS_RGB;
    jpeg_set_defaults(&enc); jpeg_set_quality(&enc, 95, TRUE);
    jpeg_start_compress(&enc, TRUE);
    row = (uint8_t *)malloc((size_t)w * 3u);
    if (!row) { jpeg_destroy_compress(&enc); fclose(f); return 0; }
    while (enc.next_scanline < enc.image_height) {
        const uint8_t *p = rgba + (size_t)enc.next_scanline * w * 4u;
        for (int x = 0; x < w; x++) {
            row[x * 3 + 0] = p[x * 4 + 0];
            row[x * 3 + 1] = p[x * 4 + 1];
            row[x * 3 + 2] = p[x * 4 + 2];
        }
        JSAMPROW ptr = row;
        jpeg_write_scanlines(&enc, &ptr, 1);
    }
    free(row);
    jpeg_finish_compress(&enc); jpeg_destroy_compress(&enc); fclose(f);
    return 1;
}

/* Built-in uncompressed 24/32-bit BMP and TGA support. */
static uint16_t le16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void store16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static void store32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (i * 8));
}
static int bmp_header(const char *path, int *w, int *h, int *bpp,
                      int *bottom_up, uint32_t *offset, uint32_t *comp) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t header[54];
    int ok = fread(header, 1, sizeof(header), f) == sizeof(header);
    fclose(f);
    if (!ok || header[0] != 'B' || header[1] != 'M' || le32(header + 14) < 40)
        return 0;
    int32_t width = (int32_t)le32(header + 18);
    int32_t height = (int32_t)le32(header + 22);
    *bpp = le16(header + 28);
    *offset = le32(header + 10);
    *comp = le32(header + 30);
    if (width <= 0 || height == 0 || height == INT32_MIN ||
        le16(header + 26) != 1 || (*bpp != 24 && *bpp != 32) ||
        (*comp != 0 && !(*comp == 3 && *bpp == 32))) return 0;
    *bottom_up = height > 0;
    *w = width; *h = height < 0 ? -height : height;
    return 1;
}
static int bmp_info(const char *path, int *w, int *h) {
    int bpp, bottom; uint32_t off, comp;
    return bmp_header(path, w, h, &bpp, &bottom, &off, &comp);
}
static uint8_t *bmp_read(const char *path, int *w, int *h) {
    int bpp, bottom;
    uint32_t offset, comp;
    if (!bmp_header(path, w, h, &bpp, &bottom, &offset, &comp)) return NULL;
    if ((uint64_t)*w * *h > SIZE_MAX / 4u) return NULL;
    size_t stride = (((size_t)*w * bpp + 31u) / 32u) * 4u;
    if (stride < (size_t)*w * (size_t)(bpp / 8)) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *rgba = (uint8_t *)malloc((size_t)*w * *h * 4u);
    uint8_t *row = (uint8_t *)malloc(stride);
    if (!rgba || !row || fseek(f, (long)offset, SEEK_SET) != 0) {
        free(rgba); free(row); fclose(f); return NULL;
    }
    for (int file_y = 0; file_y < *h; file_y++) {
        if (fread(row, 1, stride, f) != stride) {
            free(rgba); free(row); fclose(f); return NULL;
        }
        int dst_y = bottom ? (*h - 1 - file_y) : file_y;
        uint8_t *dst = rgba + (size_t)dst_y * *w * 4u;
        for (int x = 0; x < *w; x++) {
            const uint8_t *src = row + (size_t)x * (bpp / 8);
            dst[x * 4 + 0] = src[2];
            dst[x * 4 + 1] = src[1];
            dst[x * 4 + 2] = src[0];
            dst[x * 4 + 3] = bpp == 32 && comp == 3 ? src[3] : 255;
        }
    }
    free(row); fclose(f); return rgba;
}
static int bmp_write(const char *path, const uint8_t *rgba, int w, int h) {
    uint64_t bytes = (uint64_t)w * h * 4u;
    if (bytes > UINT32_MAX - 122u) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint8_t header[122] = {0};
    header[0] = 'B'; header[1] = 'M';
    store32(header + 2, 122u + (uint32_t)bytes);
    store32(header + 10, 122);
    store32(header + 14, 108);
    store32(header + 18, (uint32_t)w);
    store32(header + 22, (uint32_t)(int32_t)-h);
    store16(header + 26, 1);
    store16(header + 28, 32);
    store32(header + 30, 3);
    store32(header + 34, (uint32_t)bytes);
    store32(header + 54, 0x00ff0000u);
    store32(header + 58, 0x0000ff00u);
    store32(header + 62, 0x000000ffu);
    store32(header + 66, 0xff000000u);
    header[70] = 'W'; header[71] = 'i'; header[72] = 'n'; header[73] = ' ';
    int ok = fwrite(header, 1, sizeof(header), f) == sizeof(header);
    uint8_t *row = (uint8_t *)malloc((size_t)w * 4u);
    if (!row) ok = 0;
    for (int y = 0; ok && y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *p = rgba + ((size_t)y * w + x) * 4u;
            uint8_t *q = row + (size_t)x * 4u;
            q[0] = p[2]; q[1] = p[1]; q[2] = p[0]; q[3] = p[3];
        }
        ok = fwrite(row, 1, (size_t)w * 4u, f) == (size_t)w * 4u;
    }
    free(row);
    if (fclose(f) != 0) ok = 0;
    return ok;
}
static int tga_header(const char *path, uint8_t hdr[18], int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fread(hdr, 1, 18, f) == 18;
    fclose(f);
    if (!ok || hdr[1] != 0 || (hdr[2] != 2 && hdr[2] != 10) ||
        (hdr[16] != 24 && hdr[16] != 32)) return 0;
    *w = le16(hdr + 12); *h = le16(hdr + 14);
    return *w > 0 && *h > 0;
}
static int tga_info(const char *path, int *w, int *h) {
    uint8_t hdr[18]; return tga_header(path, hdr, w, h);
}
static uint8_t *tga_read(const char *path, int *w, int *h) {
    uint8_t hdr[18];
    if (!tga_header(path, hdr, w, h) || (uint64_t)*w * *h > SIZE_MAX / 4u)
        return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 18 + hdr[0], SEEK_SET) != 0) { fclose(f); return NULL; }
    size_t pixels = (size_t)*w * *h;
    uint8_t *rgba = (uint8_t *)malloc(pixels * 4u);
    if (!rgba) { fclose(f); return NULL; }
    int bytes = hdr[16] / 8;
    int top = (hdr[17] & 0x20) != 0;
    int right = (hdr[17] & 0x10) != 0;
    size_t i = 0;
    while (i < pixels) {
        int packet = 0;
        if (hdr[2] == 10) {
            packet = fgetc(f);
            if (packet == EOF) goto bad_tga;
        }
        int repeat = hdr[2] == 10 && (packet & 0x80);
        size_t count = hdr[2] == 2 ? 1u : (size_t)(packet & 0x7f) + 1u;
        if (count > pixels - i) goto bad_tga;
        uint8_t shared[4] = {0, 0, 0, 255};
        if (repeat && fread(shared, 1, (size_t)bytes, f) != (size_t)bytes) goto bad_tga;
        for (size_t j = 0; j < count; j++, i++) {
            uint8_t bgra[4] = {0, 0, 0, 255};
            if (repeat) memcpy(bgra, shared, (size_t)bytes);
            else if (fread(bgra, 1, (size_t)bytes, f) != (size_t)bytes) goto bad_tga;
            size_t y = i / (size_t)*w, x = i % (size_t)*w;
            if (!top) y = (size_t)*h - 1 - y;
            if (right) x = (size_t)*w - 1 - x;
            uint8_t *q = rgba + (y * *w + x) * 4u;
            q[0] = bgra[2]; q[1] = bgra[1]; q[2] = bgra[0]; q[3] = bgra[3];
        }
    }
    fclose(f); return rgba;
bad_tga:
    free(rgba); fclose(f); return NULL;
}
static int tga_write(const char *path, const uint8_t *rgba, int w, int h) {
    if (w < 1 || h < 1 || w > 65535 || h > 65535) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint8_t hdr[18] = {0};
    hdr[2] = 2; store16(hdr + 12, (uint16_t)w);
    store16(hdr + 14, (uint16_t)h); hdr[16] = 32; hdr[17] = 0x28;
    int ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    uint8_t *row = (uint8_t *)malloc((size_t)w * 4u);
    if (!row) ok = 0;
    for (int y = 0; ok && y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t *p = rgba + ((size_t)y * w + x) * 4;
            uint8_t *q = row + (size_t)x * 4;
            q[0] = p[2]; q[1] = p[1]; q[2] = p[0]; q[3] = p[3];
        }
        ok = fwrite(row, 1, (size_t)w * 4u, f) == (size_t)w * 4u;
    }
    free(row);
    if (fclose(f) != 0) ok = 0;
    return ok;
}

static int img_info(const Job *j, int *w, int *h) {
    if (j->kind == 1) return png_dimensions(j->src, w, h);
    if (j->kind == 2) return jpg_info(j->src, w, h);
    if (j->kind == 3) return bmp_info(j->src, w, h);
    return tga_info(j->src, w, h);
}
static uint8_t *img_read(const Job *j, int *w, int *h) {
    if (j->kind == 1) return png_read(j->src, w, h);
    if (j->kind == 2) return jpg_read(j->src, w, h);
    if (j->kind == 3) return bmp_read(j->src, w, h);
    return tga_read(j->src, w, h);
}
static int img_write(const char *path, const uint8_t *rgba, int w, int h, int kind) {
    if (kind == 2) return jpg_write(path, rgba, w, h);
    if (kind == 3) return bmp_write(path, rgba, w, h);
    if (kind == 4) return tga_write(path, rgba, w, h);
    return png_write(path, rgba, w, h);
}

/* Native approximation of FFmpeg's four-sample deband filter. */
static uint8_t *simple_deband(const Job *j, int *w, int *h, const Context *ctx) {
    uint8_t *src = img_read(j, w, h);
    if (!src) return NULL;
    size_t pixels = (size_t)*w * *h;
    uint8_t *out = (uint8_t *)malloc(pixels * 4u);
    if (!out) { free(src); return NULL; }
    uint32_t seed = hash_name(j->src);
    float thr = ctx->threshold * 255.0f;
    for (int y = 0; y < *h; y++) {
        for (int x = 0; x < *w; x++) {
            size_t pos = ((size_t)y * *w + x);
            const uint8_t *p = src + pos * 4u;
            uint8_t *q = out + pos * 4u;
            /* Preserve every channel, including hidden RGB, under zero alpha. */
            if (p[3] == 0) {
                memcpy(q, p, 4);
                continue;
            }
            uint32_t hash = mix32(seed ^ (uint32_t)x * 0x9e3779b9u ^
                                  (uint32_t)y * 0x85ebca6bu);
            int r = 1 + (int)(hash % (unsigned)ctx->radius);
            int direction = (int)((hash >> 13) & 255u);
            int dx = (int)lrintf(ctx->trig_cos[direction] * r);
            int dy = (int)lrintf(ctx->trig_sin[direction] * r);
            if (!dx && !dy) dx = 1;
            int xs[4] = {x + dx, x - dy, x - dx, x + dy};
            int ys[4] = {y + dy, y + dx, y - dy, y - dx};
            for (int i = 0; i < 4; i++) {
                if (xs[i] < 0) xs[i] = 0;
                if (xs[i] >= *w) xs[i] = *w - 1;
                if (ys[i] < 0) ys[i] = 0;
                if (ys[i] >= *h) ys[i] = *h - 1;
            }
            const uint8_t *a = src + ((size_t)ys[0] * *w + xs[0]) * 4;
            const uint8_t *b = src + ((size_t)ys[1] * *w + xs[1]) * 4;
            const uint8_t *c = src + ((size_t)ys[2] * *w + xs[2]) * 4;
            const uint8_t *d = src + ((size_t)ys[3] * *w + xs[3]) * 4;
            float noise = noise_at(ctx, x, y, seed);
            /* Exclude fully transparent samples from RGB averaging. */
            int count = (a[3] != 0) + (b[3] != 0) +
                        (c[3] != 0) + (d[3] != 0);
            for (int k = 0; k < 3; k++) {
                float avg = count ? ((a[3] ? a[k] : 0) +
                                     (b[3] ? b[k] : 0) +
                                     (c[3] ? c[k] : 0) +
                                     (d[3] ? d[k] : 0)) / count : (float)p[k];
                float v = fabsf(avg - p[k]) <= thr ? avg : (float)p[k];
                q[k] = quantize(v + noise);
            }
            q[3] = p[3];
        }
    }
    free(src);
    return out;
}

/* FFmpeg runs as a subprocess and streams 16-bit RGBA directly into memory. */
#ifdef _WIN32
/* Restrict inheritance to the pipe and NUL handles. Inheriting every open
 * handle lets a child keep another worker's PNG temporary file open, making
 * MoveFileEx/DeleteFile fail with sharing violations during a batch. */
static BOOL launch_ffmpeg_hidden(char *cmd, HANDLE output, HANDLE nul,
                                PROCESS_INFORMATION *pi) {
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &bytes);
    if (!bytes) return FALSE;
    LPPROC_THREAD_ATTRIBUTE_LIST attributes =
        (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(bytes);
    if (!attributes) return FALSE;
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) {
        free(attributes); return FALSE;
    }
    HANDLE handles[2] = {output, nul};
    BOOL launched = FALSE;
    if (UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                  handles, sizeof(handles), NULL, NULL)) {
        STARTUPINFOEXA si;
        memset(&si, 0, sizeof(si));
        si.StartupInfo.cb = sizeof(si);
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = nul;
        si.StartupInfo.hStdOutput = output;
        si.StartupInfo.hStdError = nul;
        si.lpAttributeList = attributes;
        launched = CreateProcessA(NULL, cmd, NULL, NULL, TRUE,
                                 CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                                 NULL, NULL, &si.StartupInfo, pi);
    }
    DWORD error = GetLastError();
    DeleteProcThreadAttributeList(attributes);
    free(attributes);
    SetLastError(error);
    return launched;
}
#endif
static int read_ffmpeg(const char *path, const char *filter, void *buffer, size_t n) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 1024 * 1024)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ,
                             &sa, OPEN_EXISTING, 0, NULL);
    if (nul == INVALID_HANDLE_VALUE) { CloseHandle(rd); CloseHandle(wr); return 0; }
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    size_t capacity = strlen(path) * 2 + strlen(filter) + 1024;
    char *cmd = (char *)malloc(capacity);
    if (!cmd) { CloseHandle(rd); CloseHandle(wr); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul); return 0; }
    /* Windows file names cannot contain double quotes, so quoting is safe here.
       Doubling backslashes before a terminal quote is handled explicitly. */
    char *quoted = (char *)malloc(strlen(path) * 2 + 8);
    if (!quoted) { free(cmd); CloseHandle(rd); CloseHandle(wr); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul); return 0; }
    char *q = quoted;
    *q++ = '"';
    const char *it = path;
    while (*it) {
        if (*it == '\\') {
            const char *s = it;
            while (*it == '\\') it++;
            size_t count = (size_t)(it - s);
            if (*it == '"' || *it == 0) count *= 2;
            while (count--) *q++ = '\\';
        } else if (*it == '"') { *q++ = '\\'; *q++ = '"'; it++; }
        else *q++ = *it++;
    }
    *q++ = '"'; *q = 0;
    snprintf(cmd, capacity,
        "ffmpeg -nostdin -hide_banner -loglevel error -threads 1 -filter_threads 1 "
        "-i %s -vf \"%s\" -frames:v 1 -pix_fmt rgba64le -f rawvideo -",
        quoted, filter);
    BOOL launched = launch_ffmpeg_hidden(cmd, wr, nul, &pi);
    free(quoted); free(cmd); CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!launched) { CloseHandle(rd); return 0; }
    uint8_t *dst = (uint8_t *)buffer;
    size_t got = 0;
    for (;;) {
        uint8_t extra[4096];
        void *target = got < n ? (void *)(dst + got) : (void *)extra;
        DWORD ask = (DWORD)(got < n ? ((n - got) > (1u << 20) ? (1u << 20) : (n - got)) : sizeof(extra));
        DWORD count = 0;
        if (!ReadFile(rd, target, ask, &count, NULL) || count == 0) break;
        if (got >= n) { got = n + 1; break; }
        got += count;
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return got == n && code == 0;
#else
    int fds[2];
    if (pipe(fds) != 0) return 0;
    pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); return 0; }
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        close(fds[1]);
        int nul = open("/dev/null", O_WRONLY);
        if (nul >= 0) { dup2(nul, STDERR_FILENO); close(nul); }
        execlp("ffmpeg", "ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
               "-threads", "1", "-filter_threads", "1", "-i", path,
               "-vf", filter, "-frames:v", "1", "-pix_fmt", "rgba64le",
               "-f", "rawvideo", "-", (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    size_t got = 0;
    int overflow = 0;
    while (got < n) {
        ssize_t rd = read(fds[0], (uint8_t *)buffer + got, n - got);
        if (rd > 0) got += (size_t)rd;
        else if (rd < 0 && errno == EINTR) continue;
        else break;
    }
    if (got == n) {
        uint8_t extra[64];
        ssize_t more = read(fds[0], extra, sizeof(extra));
        overflow = more > 0;
    }
    close(fds[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return got == n && !overflow && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}
static uint8_t *ffmpeg_deband(const Job *j, int w, int h, const Context *ctx) {
    uint64_t count = (uint64_t)w * h;
    if (count == 0 || count > SIZE_MAX / 8u) return NULL;
    uint16_t *raw = (uint16_t *)malloc((size_t)count * 8u);
    if (!raw) return NULL;
    char filter[512];
    snprintf(filter, sizeof(filter),
        "format=gbrap16le,deband=1thr=%.6f:2thr=%.6f:3thr=%.6f:"
        "4thr=0.00003:range=%d:blur=1,format=rgba64le",
        ctx->threshold, ctx->threshold, ctx->threshold, ctx->radius);
    if (!read_ffmpeg(j->src, filter, raw, (size_t)count * 8u)) {
        free(raw); return NULL;
    }
    uint8_t *out = (uint8_t *)malloc((size_t)count * 4u);
    if (!out) { free(raw); return NULL; }
    uint32_t seed = hash_name(j->src);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t i = (size_t)y * w + x;
            float n = noise_at(ctx, x, y, seed);
            for (int k = 0; k < 3; k++) {
                float val = (float)raw[i * 4u + k] * (255.0f / 65535.0f);
                out[i * 4u + k] = quantize(val + n);
            }
            out[i * 4u + 3] = (uint8_t)((raw[i * 4u + 3] + 128) / 257);
        }
    }
    free(raw); return out;
}

/* FFmpeg may modify alpha or the RGB stored under fully transparent pixels.
 * Reapply original alpha to every pixel, and restore all four original bytes
 * where alpha is zero. Only required for formats that support transparency.
 * A failure to read the original is fatal: do not silently damage pixels. */
static int preserve_transparency(const Job *j, uint8_t *out, int w, int h) {
    if (j->kind == 2) return 1; /* JPEG has no alpha. */
    int src_w = 0, src_h = 0;
    uint8_t *src = img_read(j, &src_w, &src_h);
    if (!src || src_w != w || src_h != h) {
        free(src);
        return 0;
    }
    size_t pixels = (size_t)w * (size_t)h;
    for (size_t i = 0; i < pixels; i++) {
        size_t o = i * 4u;
        uint8_t alpha = src[o + 3u];
        if (alpha == 0) memcpy(out + o, src + o, 4);
        else out[o + 3u] = alpha;
    }
    free(src);
    return 1;
}

/* Never replace an existing destination outside -x. */
static int publish(const char *temp, const char *dst, int overwrite) {
#ifdef _WIN32
    DWORD flags = MOVEFILE_WRITE_THROUGH;
    if (overwrite) flags |= MOVEFILE_REPLACE_EXISTING;
    if (!MoveFileExA(temp, dst, flags)) return 0;
    return 1;
#else
    if (overwrite) return rename(temp, dst) == 0;
    if (link(temp, dst) != 0) return 0;
    unlink(temp); return 1;
#endif
}
static void remove_file(const char *path) {
#ifdef _WIN32
    DeleteFileA(path);
#else
    unlink(path);
#endif
}
static void process_job(Context *ctx, size_t index) {
    Job *j = &ctx->jobs[index];
    int w = 0, h = 0;
    if (!img_info(j, &w, &h) || w < 1 || h < 1 || (uint64_t)w * h > SIZE_MAX / 8u) {
        fprintf(stderr, "ERROR: cannot read dimensions: %s\n", j->src);
        goto failed;
    }
    char *parent = parent_dir(j->dst);
    if (!parent || !mkdirs(parent)) {
        fprintf(stderr, "ERROR: cannot create output parent for: %s\n", j->dst);
        free(parent); goto failed;
    }
    free(parent);
    uint8_t *rgba = NULL;
    if (!ctx->simple) {
        rgba = ffmpeg_deband(j, w, h, ctx);
        if (rgba && !preserve_transparency(j, rgba, w, h)) {
            fprintf(stderr, "WARN: cannot restore original alpha for '%s'; trying native mode\n", j->src);
            free(rgba);
            rgba = NULL;
        }
    }
    if (!rgba) {
        if (!ctx->simple) fprintf(stderr, "WARN: FFmpeg failed for '%s'; trying simple mode\n", j->src);
        rgba = simple_deband(j, &w, &h, ctx);
    }
    if (!rgba) { fprintf(stderr, "ERROR: decode/deband failed: %s\n", j->src); goto failed; }

    size_t nt = strlen(j->dst) + 100;
    char *temp = (char *)malloc(nt);
    if (!temp) { free(rgba); goto failed; }
#ifdef _WIN32
    unsigned long pid = (unsigned long)GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    snprintf(temp, nt, "%s.debandtmp.%lu.%zu", j->dst, pid, index);
    int writer_kind = ctx->overwrite ? j->kind : 1;
    int wrote = img_write(temp, rgba, w, h, writer_kind);
    free(rgba);
    if (!wrote) {
        fprintf(stderr, "ERROR: cannot write: %s\n", temp);
        remove_file(temp); free(temp); goto failed;
    }
    if (!publish(temp, j->dst, ctx->overwrite)) {
        fprintf(stderr, "ERROR: cannot publish (exists or not writable): %s\n", j->dst);
        remove_file(temp); free(temp); goto failed;
    }
    free(temp);
#ifdef _WIN32
    InterlockedIncrement((volatile LONG *)&ctx->ok);
#else
    __sync_fetch_and_add(&ctx->ok, 1);
#endif
    return;
failed:
#ifdef _WIN32
    InterlockedIncrement((volatile LONG *)&ctx->failed);
#else
    __sync_fetch_and_add(&ctx->failed, 1);
#endif
}

static int add_job(const char *src, const char *rel, int kind) {
    Context *ctx = global_ctx;
    if (ctx->count == ctx->cap) {
        size_t capacity = ctx->cap ? ctx->cap * 2 : 512;
        Job *more = (Job *)realloc(ctx->jobs, capacity * sizeof(Job));
        if (!more) return 0;
        ctx->jobs = more; ctx->cap = capacity;
    }
    char *dest = NULL;
    if (global_overwrite) dest = str_dup(src);
    else {
        char *base = str_dup(rel);
        if (!base) return 0;
        char *last = base;
        for (char *p = base; *p; p++) if (is_sep(*p)) last = p + 1;
        size_t n = strlen(base) + 24;
        char *output_rel = (char *)malloc(n);
        if (!output_rel) { free(base); return 0; }
        size_t prefix_pos = (size_t)(last - base);
        memcpy(output_rel, base, prefix_pos);
        output_rel[prefix_pos] = 0;
        strcat(output_rel, global_prefix ? "deband_" : "");
        strcat(output_rel, last);
        if (kind != 1 || !eq_ext(last, ".png")) strcat(output_rel, ".png");
        dest = join_path(global_output_root, output_rel);
        free(output_rel); free(base);
    }
    if (!dest) return 0;
    ctx->jobs[ctx->count].src = str_dup(src);
    ctx->jobs[ctx->count].dst = dest;
    ctx->jobs[ctx->count].kind = kind;
    if (!ctx->jobs[ctx->count].src) { free(dest); return 0; }
    ctx->count++;
    return 1;
}

static int same_path(const char *a, const char *b) {
#ifdef _WIN32
    while (*a && *b) {
        int aa = is_sep(*a) ? '/' : lower_ascii((unsigned char)*a);
        int bb = is_sep(*b) ? '/' : lower_ascii((unsigned char)*b);
        if (aa != bb) return 0;
        a++; b++;
    }
    return !*a && !*b;
#else
    return strcmp(a, b) == 0;
#endif
}
static char *absolute_path(const char *p) {
#ifdef _WIN32
    char *buf = (char *)malloc(MAX_PATH_SIZE);
    if (!buf) return NULL;
    if (!_fullpath(buf, p, MAX_PATH_SIZE)) { free(buf); return NULL; }
    return buf;
#else
    return realpath(p, NULL);
#endif
}
static int inside_path(const char *p, const char *root) {
    size_t n = strlen(root);
#ifdef _WIN32
    for (size_t i = 0; i < n; i++)
        if (!p[i] || (is_sep(root[i]) ? !is_sep(p[i]) : lower_ascii(p[i]) != lower_ascii(root[i]))) return 0;
#else
    if (strncmp(p, root, n)) return 0;
#endif
    return (n > 0 && is_sep(root[n - 1])) || is_sep(p[n]);
}
static int scan_tree(const char *path, const char *rel, const char *skip_root) {
#ifdef _WIN32
    char *pat = join_path(path, "*");
    if (!pat) return 0;
    WIN32_FIND_DATAA data;
    HANDLE hd = FindFirstFileA(pat, &data);
    free(pat);
    if (hd == INVALID_HANDLE_VALUE) { fprintf(stderr, "ERROR: cannot scan directory: %s\n", path); return 0; }
    do {
        const char *name = data.cFileName;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (global_prefix && !strncmp(name, "deband_", 7)) continue;
        char *child = join_path(path, name);
        char *relative = *rel ? join_path(rel, name) : str_dup(name);
        if (!child || !relative) { free(child); free(relative); FindClose(hd); return 0; }
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!skip_root || !same_path(child, skip_root)) {
                if (!scan_tree(child, relative, skip_root)) {
                    free(child); free(relative); FindClose(hd); return 0;
                }
            }
        } else {
            int kind = kind_of(name);
            if (kind && !add_job(child, relative, kind)) {
                free(child); free(relative); FindClose(hd); return 0;
            }
        }
        free(child); free(relative);
    } while (FindNextFileA(hd, &data));
    FindClose(hd); return 1;
#else
    DIR *dir = opendir(path);
    if (!dir) { fprintf(stderr, "ERROR: cannot scan directory: %s\n", path); return 0; }
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        const char *name = e->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        if (global_prefix && !strncmp(name, "deband_", 7)) continue;
        char *child = join_path(path, name);
        char *relative = *rel ? join_path(rel, name) : str_dup(name);
        if (!child || !relative) { free(child); free(relative); closedir(dir); return 0; }
        struct stat st;
        if (lstat(child, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (!skip_root || !same_path(child, skip_root)) {
                    if (!scan_tree(child, relative, skip_root)) {
                        free(child); free(relative); closedir(dir); return 0;
                    }
                }
            } else if (S_ISREG(st.st_mode)) {
                int kind = kind_of(name);
                if (kind && !add_job(child, relative, kind)) {
                    free(child); free(relative); closedir(dir); return 0;
                }
            }
        }
        free(child); free(relative);
    }
    closedir(dir); return 1;
#endif
}
static int detect_ffmpeg(void) {
#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si)); memset(&pi, 0, sizeof(pi)); si.cb = sizeof(si);
    char cmd[] = "ffmpeg -version";
    int found = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                               NULL, NULL, &si, &pi) != 0;
    if (found) { WaitForSingleObject(pi.hProcess, INFINITE); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    return found;
#else
    const char *path = getenv("PATH");
    if (!path) return 0;
    char *paths = str_dup(path);
    if (!paths) return 0;
    char *p = paths;
    while (*p) {
        char *end = strchr(p, ':');
        if (end) *end = 0;
        char *exe = join_path(*p ? p : ".", "ffmpeg");
        int found = exe && access(exe, X_OK) == 0;
        free(exe);
        if (found) { free(paths); return 1; }
        if (!end) break;
        p = end + 1;
    }
    free(paths); return 0;
#endif
}

#ifdef _WIN32
static DWORD WINAPI worker(void *opaque) {
    Context *ctx = (Context *)opaque;
    for (;;) {
        long at = InterlockedIncrement((volatile LONG *)&ctx->next) - 1;
        if ((size_t)at >= ctx->count) break;
        process_job(ctx, (size_t)at);
    }
    return 0;
}
#else
static void *worker(void *opaque) {
    Context *ctx = (Context *)opaque;
    for (;;) {
        long at = __sync_fetch_and_add(&ctx->next, 1);
        if ((size_t)at >= ctx->count) break;
        process_job(ctx, (size_t)at);
    }
    return NULL;
}
#endif

static int cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO info; GetSystemInfo(&info);
    return (int)info.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 2;
#endif
}
static void usage(const char *exe) {
    printf("Deband - fast batch debanding + high-frequency dithering\n\n"
           "Usage: %s [options] FILE_OR_DIRECTORY\n\n"
           "  -o DIR    Output directory (default: .)\n"
           "  -x        Overwrite inputs in place, atomically (no -o)\n"
           "  -s        Force native C mode (default uses FFmpeg if installed)\n"
           "  -j N      Parallel image jobs (default: 4 FFmpeg, up to 8 C)\n"
           "  -t F      Threshold 0.00003..0.5 (default: 0.020)\n"
           "  -r N      Radius 1..128 pixels (default: 24)\n"
           "  -d F      Dither strength 0..4 levels (default: 0.8)\n"
           "  -h        Show help\n\n"
           "Without -x, produces PNGs and refuses to overwrite existing files.\n"
           "With -o . or implicit -o ., prefixes each output with deband_.\n"
           "Supported input: PNG, JPG, JPEG, BMP (24/32-bit), TGA (24/32-bit).\n"
           "Fully transparent pixels retain original RGBA, including hidden RGB.\n"
           "In-place JPEG output is quality 95; metadata may be lost.\n",
           exe);
}

int main(int argc, char **argv) {
    if (argc == 1) { usage(argv[0]); return 0; }
    Context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.threshold = 0.020f; ctx.radius = 24; ctx.dither = 0.8f;
    const char *input = NULL, *output = ".";
    int explicit_output = 0, jobs = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { usage(argv[0]); return 0; }
        if (!strcmp(argv[i], "-s")) ctx.simple = 1;
        else if (!strcmp(argv[i], "-x")) ctx.overwrite = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) { output = argv[++i]; explicit_output = 1; }
        else if (!strcmp(argv[i], "-j") && i + 1 < argc) jobs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) ctx.threshold = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) ctx.radius = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) ctx.dither = (float)atof(argv[++i]);
        else if (argv[i][0] == '-') { fprintf(stderr, "ERROR: unrecognized argument: %s\n", argv[i]); return 2; }
        else if (!input) input = argv[i];
        else { fprintf(stderr, "ERROR: too many input paths\n"); return 2; }
    }
    if (!input) { usage(argv[0]); return 2; }
    if (ctx.overwrite && explicit_output) { fprintf(stderr, "ERROR: -x and -o cannot be combined\n"); return 2; }
    if (ctx.radius < 1 || ctx.radius > 128 || ctx.threshold < 0.00003f ||
        ctx.threshold > 0.5f || ctx.dither < 0 || ctx.dither > 4 || jobs > 128 || jobs < 0) {
        fprintf(stderr, "ERROR: parameter outside accepted range\n"); return 2;
    }
    STATBUF st;
    if (STAT(input, &st) != 0) { fprintf(stderr, "ERROR: input does not exist: %s\n", input); return 2; }
#ifdef _WIN32
    global_is_dir = (st.st_mode & _S_IFDIR) != 0;
#else
    global_is_dir = S_ISDIR(st.st_mode);
#endif
    if (!global_is_dir && kind_of(input) == 0) {
        fprintf(stderr, "ERROR: unsupported input extension: %s\n", input); return 2;
    }
    char *input_root = absolute_path(input);
    if (!input_root) { fprintf(stderr, "ERROR: cannot resolve input path\n"); return 2; }
    global_overwrite = ctx.overwrite;
    global_ctx = &ctx;
    if (!ctx.overwrite) {
        if (!mkdirs(output)) {
            fprintf(stderr, "ERROR: output is not an accessible directory: %s\n", output);
            free(input_root); return 2;
        }
        if (!writable_directory(output)) { free(input_root); return 2; }
    }
    char *output_root = ctx.overwrite ? NULL : absolute_path(output);
    if (!ctx.overwrite && !output_root) { fprintf(stderr, "ERROR: cannot resolve output directory\n"); free(input_root); return 2; }
    global_output_root = output_root;
    char *cwd = absolute_path(".");
    char *source_dir = global_is_dir ? NULL : parent_dir(input_root);
    global_prefix = !ctx.overwrite && (
        (cwd && same_path(output_root, cwd)) ||
        (global_is_dir && same_path(output_root, input_root)) ||
        (source_dir && same_path(output_root, source_dir)));
    free(source_dir);
    free(cwd);
    if (ctx.overwrite) {
        char *p = global_is_dir ? str_dup(input_root) : parent_dir(input_root);
        int good = p && writable_directory(p);
        free(p);
        if (!good) { free(output_root); free(input_root); return 2; }
    }
    const char *skip_root = NULL;
    if (!ctx.overwrite && global_is_dir && inside_path(output_root, input_root)) skip_root = output_root;
    if (global_is_dir) {
        if (!scan_tree(input_root, "", skip_root)) { fprintf(stderr, "ERROR: scanning failed\n"); return 2; }
    } else {
        if (!add_job(input_root, base_name(input_root), kind_of(input_root))) {
            fprintf(stderr, "ERROR: cannot enqueue input\n"); return 2;
        }
    }
    if (!ctx.count) { fprintf(stderr, "No supported images found.\n"); free(input_root); free(output_root); return 1; }
    if (!ctx.simple && !detect_ffmpeg()) {
        fprintf(stderr, "FFmpeg not detected; using native C mode.\n");
        ctx.simple = 1;
    }
    int threads = jobs ? jobs : (ctx.simple ? cpu_count() : 4);
    if (!jobs && ctx.simple && threads > 8) threads = 8;
    if ((size_t)threads > ctx.count) threads = (int)ctx.count;
    if (threads < 1) threads = 1;
    ctx.noise = (float *)malloc(NOISE_COUNT * sizeof(float));
    if (!ctx.noise) { fprintf(stderr, "ERROR: out of memory\n"); return 2; }
    init_noise(ctx.noise);
    for (int i = 0; i < 256; i++) {
        float angle = (float)i * (6.283185307f / 256.0f);
        ctx.trig_cos[i] = cosf(angle);
        ctx.trig_sin[i] = sinf(angle);
    }
    printf("Backend: %s | images: %zu | workers: %d | threshold: %.4f | radius: %d | dither: %.2f\n",
           ctx.simple ? "native C" : "FFmpeg", ctx.count, threads,
           ctx.threshold, ctx.radius, ctx.dither);
    fflush(stdout);
#ifdef _WIN32
    HANDLE *handles = (HANDLE *)calloc((size_t)threads, sizeof(HANDLE));
    if (!handles) return 2;
    for (int i = 0; i < threads; i++) handles[i] = CreateThread(NULL, 0, worker, &ctx, 0, NULL);
    for (int i = 0; i < threads; i++) if (handles[i]) { WaitForSingleObject(handles[i], INFINITE); CloseHandle(handles[i]); }
    free(handles);
#else
    pthread_t *handles = (pthread_t *)calloc((size_t)threads, sizeof(pthread_t));
    if (!handles) return 2;
    int started = 0;
    for (int i = 0; i < threads; i++) {
        if (pthread_create(&handles[i], NULL, worker, &ctx) != 0) break;
        started++;
    }
    for (int i = 0; i < started; i++) pthread_join(handles[i], NULL);
    free(handles);
#endif
    printf("Done. %ld succeeded, %ld failed.\n", ctx.ok, ctx.failed);
    for (size_t i = 0; i < ctx.count; i++) { free(ctx.jobs[i].src); free(ctx.jobs[i].dst); }
    free(ctx.jobs); free(ctx.noise); free(input_root); free(output_root);
    return ctx.failed ? 1 : 0;
}
