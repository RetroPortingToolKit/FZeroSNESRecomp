#include "fzero_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FzeroViewport g_viewport;
/* Presentation blend for the sequence mode. The desktop host presents between
 * simulations, so 1 alone never exercises the interpolated path. */
static double g_alpha = 1;
static unsigned g_scale = 1;
static unsigned g_supersample = 1;
static uint32_t *pixels;
static size_t pixel_capacity;

/* Writes the frame and reports how many of its stock columns differ from the
 * PPU's own output. That count is a diagnostic, not an I/O failure: only a
 * single-frame run turns it into an exit status. */
static int write_ppm(const uint32_t *output, const char *path,
                     unsigned *differences, bool report) {
  FzeroViewport v = g_viewport;
  FILE *f = fopen(path, "wb");
  if (!f) return 4;
  int width = v.width * (int)g_scale, height = 224 * (int)g_scale;
  fprintf(f, "P6\n%d %d\n255\n", width, height);
  *differences = 0;
  for (int y = 0; y < height; ++y) {
    unsigned row_differences = 0;
    for (int x = 0; x < width; ++x) {
      uint32_t p = output[y * width + x];
      unsigned char rgb[3] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p};
      if (fwrite(rgb, 3, 1, f) != 1) { fclose(f); return 4; }
      if (g_scale == 1 && v.width == 256 && p != FzeroRendererStockFrame()[y * 256 + x]) ++row_differences;
    }
    if (row_differences && report) fprintf(stderr, "row %d: %u differing pixels\n", y, row_differences);
    *differences += row_differences;
  }
  return fclose(f) ? 4 : 0;
}

/* One capture through the compositor. The renderer carries state between
 * frames - interpolation sources and the blended Mode 7 reference - so a
 * sequence has to be replayed in order to represent what a player sees. */
static int render_one(const char *capture, const char *output,
                      unsigned *differences, bool report) {
  if (!FzeroRendererLoadCapture(capture)) return 2;
  bool ok = g_scale == 1 && g_supersample == 1 ? FzeroRendererDraw(pixels, g_viewport, g_alpha) :
      FzeroRendererDrawHd(pixels, pixel_capacity, g_viewport, g_alpha, g_scale);
  if (!ok) return 3;
  return write_ppm(pixels, output, differences, report);
}

static void usage(void) {
  fputs("usage: FZeroRenderCapture capture.bin aspect output.ppm\n"
        "       FZeroRenderCapture --sequence[=alpha] aspect output-directory capture.bin...\n"
        "       Set FZERO_HD_SCALE to an integer from 2 to 10 to render HD Mode 7.\n"
        "       Set FZERO_HD_SUPERSAMPLE from 1 to 4 for samples per axis.\n",
        stderr);
}

int main(int argc, char **argv) {
  atexit(FzeroRendererShutdown);
  const char *scale = getenv("FZERO_HD_SCALE");
  if (scale && *scale) {
    if (!FzeroParseHdScale(scale, &g_scale)) { usage(); return 2; }
  }
  const char *samples = getenv("FZERO_HD_SUPERSAMPLE");
  if (samples && *samples) {
    char *end;
    unsigned long value = strtoul(samples, &end, 10);
    if (*end || value < 1 || value > 4) { usage(); return 2; }
    g_supersample = (unsigned)value;
  }
  if (!FzeroRendererSetSupersample(g_supersample)) return 2;
  pixel_capacity = (size_t)FZERO_MAX_WIDTH * 224 * g_scale * g_scale;
  pixels = calloc(pixel_capacity, sizeof(*pixels));
  if (!pixels) { fputs("Unable to allocate capture frame\n", stderr); return 3; }
  bool sequence = argc > 1 && strncmp(argv[1], "--sequence", 10) == 0 &&
                  (argv[1][10] == 0 || argv[1][10] == '=');
  if (sequence && argv[1][10] == '=') {
    char *end;
    g_alpha = strtod(argv[1] + 11, &end);
    /* An unparsable blend used to read as zero and silently render the
     * previous frame for the whole run. */
    if (end == argv[1] + 11 || *end || !(g_alpha >= 0 && g_alpha <= 1)) {
      fputs("FZeroRenderCapture: --sequence= needs a blend in [0,1]\n", stderr);
      return 2;
    }
  }
  if (sequence ? argc < 5 : argc != 4) { usage(); return 2; }
  FzeroVideoSettings settings;
  FzeroVideoStock(&settings);
  settings.enhanced = true;
  if (!FzeroParseAspect(argv[2], &settings.aspect)) { usage(); return 2; }
  g_viewport = FzeroCalculateViewport(&settings, 1920, 1080);
  unsigned differences = 0;
  if (!sequence) {
    int status = render_one(argv[1], argv[3], &differences, true);
    if (status) return status;
    if (g_scale > 1)
      fprintf(stderr, "HD Mode 7 capture: %dx%u\n", g_viewport.width * (int)g_scale, 224 * g_scale);
    else if (g_viewport.width == 256)
      fprintf(stderr, "native capture: width=256 stock_diff_pixels=%u\n", differences);
    else
      fprintf(stderr, "native capture: width=%d (wide output, no stock comparison)\n",
              g_viewport.width);
    return differences ? 1 : 0;
  }
  unsigned total = 0;
  for (int i = 4; i < argc; ++i) {
    char path[1024];
    const char *base = argv[i];
    for (const char *p = argv[i]; *p; ++p)
      if (*p == '/' || *p == '\\') base = p + 1;
    size_t length = strlen(base);
    if (length > 4 && strcmp(base + length - 4, ".bin") == 0) length -= 4;
    if (snprintf(path, sizeof(path), "%s/%.*s.ppm", argv[3], (int)length, base) >=
        (int)sizeof(path)) return 2;
    int status = render_one(argv[i], path, &differences, false);
    if (status) return status;
    total += differences;
  }
  fprintf(stderr, "native capture sequence: %d frames at width=%d", argc - 4, g_viewport.width);
  if (g_viewport.width == 256) fprintf(stderr, " stock_diff_pixels=%u", total);
  fputc('\n', stderr);
  return 0;
}
