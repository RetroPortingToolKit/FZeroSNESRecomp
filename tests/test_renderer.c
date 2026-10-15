#include "fzero_renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)
static Ppu p;
static uint8_t ram[0x20000], before[0x20000];
static uint32_t stock[256 * 224], guarded[FZERO_MAX_WIDTH * 224 + 2];
static void word(unsigned address, unsigned value) { ram[address] = value; ram[address + 1] = value >> 8; }
static void publish(unsigned frame) {
  FzeroRendererBeginFrame(ram, frame);
  for (unsigned y = 1; y <= 224; ++y) FzeroRendererCaptureLine(&p, y);
  FzeroRendererEndFrame(&p, stock);
}
static void setup(void) {
  memset(&p, 0, sizeof(p)); memset(ram, 0, sizeof(ram));
  p.inidisp = 15; p.bgmode = 7; p.screenEnabled[0] = 1;
  p.m7matrix[0] = p.m7matrix[3] = 256;
  for (unsigned i = 0; i < 0x8000; ++i) p.vram[i] = 0x0100;
  p.cgram[1] = 31;
  for (unsigned i = 0; i < 256 * 224; ++i) stock[i] = 0x123456;
  for (unsigned i = 0; i < 128; ++i) p.oam[2*i] = 0x8080;
  memset(p.highOam, 0x55, sizeof(p.highOam));
  ram[0x54] = 2; ram[0x81] = 1;
  FzeroRendererReset();
}

static void view_publish(FzeroRenderView *view, unsigned frame) {
  FzeroRenderViewBeginFrame(view, ram, frame);
  for (unsigned y = 1; y <= 224; ++y) FzeroRenderViewCaptureLine(view, &p, y);
  FzeroRenderViewEndFrame(view, &p, stock);
}

static void test_view_isolation(void) {
  FzeroRenderView *a = FzeroRenderViewCreate(), *b = FzeroRenderViewCreate();
  CHECK(a && b);
  static uint32_t first[FZERO_MAX_WIDTH * 224], again[FZERO_MAX_WIDTH * 224];
  static uint32_t hd[FZERO_MAX_WIDTH * 224 * 4];
  FzeroVideoSettings settings; FzeroVideoStock(&settings);
  settings.enhanced = true; settings.aspect = FZERO_ASPECT_32_9;
  FzeroViewport v = FzeroCalculateViewport(&settings, 1920, 540);
  setup();
  view_publish(a, 10);
  CHECK(FzeroRenderViewDraw(a, first, v, 0.5));
  p.cgram[1] = 0x03e0;
  word(0xb70, 4096); word(0xb90, 2048); ram[0xac] = 90;
  view_publish(b, 600);
  CHECK(FzeroRenderViewDraw(b, again, v, 0.5));
  CHECK(memcmp(first, again, (size_t)v.width * 224 * sizeof(*first)));
  /* Alternating HD/native draws cannot overwrite the other camera's frame,
   * scratch or interpolation history. Worker work is joined on return. */
  CHECK(FzeroRenderViewDrawPresentation(b, NULL, hd, sizeof(hd)/sizeof(*hd), v, 0.5, 2));
  CHECK(FzeroRenderViewDraw(a, again, v, 0.5));
  CHECK(!memcmp(first, again, (size_t)v.width * 224 * sizeof(*first)));
  CHECK(!FzeroRendererHasFrame());
  FzeroRenderViewReset(b);
  CHECK(!FzeroRenderViewHasFrame(b) && FzeroRenderViewHasFrame(a));
  CHECK(!FzeroRenderViewDraw(b, again, v, 1));
  FzeroRenderViewDestroy(b);
  CHECK(FzeroRenderViewDraw(a, again, v, 0.5));
  CHECK(!memcmp(first, again, (size_t)v.width * 224 * sizeof(*first)));
  FzeroRenderViewDestroy(a);
  CHECK(!FzeroRenderViewDraw(NULL, again, v, 1));
}

static uint32_t palette_rgb(unsigned c) {
  unsigned r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
  return (((r << 3) | (r >> 2)) << 16) |
         (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
}

static void test_panorama(void) {
  /* Build an independently indexed, coloured panorama in the retail strip
   * layout. Leave padding past the stock-visible overlap empty: simply
   * widening the tilemap sampler must fail at strip/rotation boundaries. */
  for (int layer = 0; layer < 2; ++layer) {
    setup();
    memset(p.vram, 0, sizeof(p.vram));
    p.bgmode = 1; p.screenEnabled[0] = 1 << layer;
    p.bgXsc[layer] = layer == 0 ? 0x79 : 0x71;
    int base = layer == 0 ? 0x7800 : 0x7000;
    int first = layer == 0 ? 36 : 92;
    int period = layer == 0 ? 896 : 768;
    for (int i = 1; i < 128; ++i) p.cgram[i] = i;
    for (int pixel = 1; pixel <= 15; ++pixel)
      for (int y = 0; y < 8; ++y) {
        p.vram[pixel * 16 + y] = ((pixel & 1) ? 255 : 0) | ((pixel & 2) ? 0xff00 : 0);
        p.vram[pixel * 16 + y + 8] = ((pixel & 4) ? 255 : 0) | ((pixel & 8) ? 0xff00 : 0);
      }
    for (int band = 0; band * 256 < period; ++band) {
      int remaining = period - band * 256;
      int valid = (remaining < 256 ? remaining : 256) + 256;
      for (int x = 0; x < valid; x += 8) {
        int tile = ((band * 256 + x) % period) / 8;
        for (int row = 0; row < 7; ++row)
          p.vram[base + (x >= 256 ? 1024 : 0) +
                 (first / 8 + band * 7 + row) * 32 + (x / 8) % 32] =
              (tile % 15 + 1) | ((tile / 15) << 10);
      }
    }
    for (int aspect = FZERO_ASPECT_STOCK; aspect <= FZERO_ASPECT_FIT; ++aspect) {
      FzeroVideoSettings s; FzeroVideoStock(&s); /* tests build an explicit viewport, not the shipped defaults */
      s.enhanced = true; s.aspect = aspect;
      FzeroViewport v = FzeroCalculateViewport(&s, 5120, 1440);
      for (int origin = 0; origin < period; origin += 31) {
        p.hScroll[layer] = origin % 256;
        p.vScroll[layer] = first + (origin / 256) * 56;
        publish(1);
        CHECK(FzeroRendererDraw(guarded + 1, v, 1));
        for (int sx = 0; sx < v.width; ++sx) {
          int logical = (origin + sx - v.extra + period) % period;
          int tile = logical / 8;
          unsigned index = (tile / 15) * 16 + tile % 15 + 1;
          CHECK(guarded[1 + 10 * v.width + sx] == palette_rgb(p.cgram[index]));
        }
      }
    }
  }
}

static void test_hud_transition(void) {
  static uint32_t active[FZERO_MAX_WIDTH * 224];
  for (int aspect = FZERO_ASPECT_STOCK; aspect <= FZERO_ASPECT_FIT; ++aspect) {
    FzeroVideoSettings s; FzeroVideoStock(&s); /* tests build an explicit viewport, not the shipped defaults */
    s.enhanced = true; s.aspect = aspect;
    FzeroViewport v = FzeroCalculateViewport(&s, 5120, 1440);
    setup(); memset(p.vram, 0, sizeof(p.vram));
    p.bgmode = 1; p.screenEnabled[0] = 1 | 4 | 16;
    p.bgXsc[2] = 0x74; p.bgTileAdr = 0x100;
    p.vram[0x7400 + 32 + 3] = p.vram[0x7400 + 32 + 24] = 1;
    for (int y = 0; y < 8; ++y) {
      p.vram[0x1008 + y] = 255; /* red BG3 HUD tiles */
      p.vram[16 + y] = 255; /* green HUD sprites */
    }
    p.cgram[193] = 0x03e0;
    p.oam[20 * 2] = (100 << 8) | 24; p.oam[20 * 2 + 1] = 0x3801;
    p.oam[32 * 2] = (100 << 8) | 200; p.oam[32 * 2 + 1] = 0x3801;
    p.highOam[5] &= ~3; p.highOam[8] &= ~3;
    ram[0x55] = 3; publish(1);
    CHECK(FzeroRendererDraw(active, v, 1));
    CHECK(active[10 * v.width + 24] == 0xff0000);
    CHECK(active[10 * v.width + 192 + 2 * v.extra] == 0xff0000);
    CHECK(active[100 * v.width + 24] == 0x00ff00);
    CHECK(active[100 * v.width + 200 + 2 * v.extra] == 0x00ff00);

    /* Race setup already owns this HUD in substates 1 and 2, before phase 3.
     * Start from a reset too: loading a setup snapshot must not need history. */
    for (int substate = 1; substate <= 2; ++substate) {
      FzeroRendererReset();
      ram[0x55] = 2; ram[0x56] = substate; publish(2);
      CHECK(FzeroRendererDraw(guarded + 1, v, 0.5));
      CHECK(!memcmp(active, guarded + 1, v.width * 224 * sizeof(*active)));
      if (v.enhanced)
        CHECK(guarded[1 + 20 * v.width + 176 + 2 * v.extra] == 0x123456);
    }
    /* Before HUD installation, the same reservations still belong to the
     * centered intro. Do not latch the previous frame's adaptive layout. */
    ram[0x56] = 0; publish(3);
    CHECK(FzeroRendererDraw(guarded + 1, v, 1));
    CHECK(guarded[1 + 10 * v.width + v.extra + 24] == 0xff0000);
    CHECK(guarded[1 + 100 * v.width + v.extra + 24] == 0x00ff00);
    if (v.extra) CHECK(guarded[1 + 100 * v.width + 24] == 0);
    /* Attract exit still displays the race HUD while scene 3 fades it. */
    ram[0x54] = 3;
    for (int phase = 4; phase <= 5; ++phase) {
      ram[0x55] = phase; publish(4 + phase);
      CHECK(FzeroRendererDraw(guarded + 1, v, 1));
      CHECK(!memcmp(active, guarded + 1, v.width * 224 * sizeof(*active)));
    }
    /* GP and Training YOU LOST both retain the live race HUD and timer. */
    ram[0x54] = 2; ram[0x55] = 6;
    for (int training = 0; training <= 1; ++training) {
      ram[0x58] = training; publish(10 + training);
      CHECK(FzeroRendererDraw(guarded + 1, v, 1));
      CHECK(!memcmp(active, guarded + 1, v.width * 224 * sizeof(*active)));
    }
  }
}
static void test_adaptive_scenes(void) {
  for (int aspect = FZERO_ASPECT_STOCK; aspect <= FZERO_ASPECT_FIT; ++aspect) {
    FzeroVideoSettings s; FzeroVideoStock(&s); /* tests build an explicit viewport, not the shipped defaults */
    s.enhanced = true; s.aspect = aspect;
    FzeroViewport v = FzeroCalculateViewport(&s, 5120, 1440);
    setup(); ram[0x54] = 0; /* Title has live track, but no race HUD. */
    for (int scene = 0; scene < 3; ++scene) {
      if (scene == 1) { ram[0x81] = 0; p.cgram[0] = 0x03e0; }
      if (scene == 2) p.inidisp = 0; /* Backdrop fades with the scene. */
      publish(scene + 1);
      CHECK(FzeroRendererDraw(guarded + 1, v, 0.5));
      for (int y = 0; y < 224; ++y) {
        CHECK(!memcmp(guarded + 1 + y * v.width + v.extra,
                      stock + y * 256, 256 * sizeof(*stock)));
        for (int x = 0; x < v.extra; ++x) {
          uint32_t expected = scene == 0 ? 0xff0000 : scene == 1 ? 0x00ff00 : 0;
          CHECK(guarded[1 + y * v.width + x] == expected);
          CHECK(guarded[1 + y * v.width + v.width - 1 - x] == expected);
        }
      }
    }
    p.inidisp = 128; publish(4);
    CHECK(FzeroRendererDraw(guarded + 1, v, 1));
    for (int i = 0; i < v.width * 224; ++i) CHECK(guarded[1 + i] == 0);
  }
}
static void test_intro_counter(void) {
  static uint32_t intro[FZERO_MAX_WIDTH * 224];
  for (int aspect = FZERO_ASPECT_STOCK; aspect <= FZERO_ASPECT_FIT; ++aspect) {
    FzeroVideoSettings s; FzeroVideoStock(&s); /* tests build an explicit viewport, not the shipped defaults */
    s.enhanced = true; s.aspect = aspect;
    FzeroViewport v = FzeroCalculateViewport(&s, 5120, 1440);
    setup(); p.screenEnabled[0] = 16; memset(p.vram, 0, sizeof(p.vram));
    for (int y = 0; y < 8; ++y) p.vram[16 + y] = 255;
    p.cgram[193] = 0x03e0;
    p.oam[126 * 2] = (190 << 8) | 208; p.oam[126 * 2 + 1] = 0x3801;
    p.oam[127 * 2] = (198 << 8) | 232; p.oam[127 * 2 + 1] = 0x3801;
    p.highOam[31] &= ~0xf0;
    publish(1); CHECK(FzeroRendererDraw(intro, v, 0.5));
    CHECK(intro[190 * v.width + 208 + 2 * v.extra] == 0x00ff00);
    CHECK(intro[198 * v.width + 232 + 2 * v.extra] == 0x00ff00);
    /* Training reuses these slots for its course map, which stays together. */
    ram[0x58] = 1; publish(2);
    CHECK(FzeroRendererDraw(guarded + 1, v, 1));
    CHECK(guarded[1 + 190 * v.width + 208 + v.extra] == 0x00ff00);
    CHECK(guarded[1 + 198 * v.width + 232 + v.extra] == 0x00ff00);
    ram[0x58] = 0;
    ram[0x54] = 3; publish(2); /* Black results reuse the temporary counter. */
    CHECK(FzeroRendererDraw(guarded + 1, v, 1));
    CHECK(!memcmp(intro, guarded + 1, v.width * 224 * sizeof(*intro)));
    /* Retail setup moves the same artwork into the permanent HUD slots. */
    memcpy(p.oam + 22 * 2, p.oam + 126 * 2, 4 * sizeof(*p.oam));
    p.highOam[5] &= ~0xf0;
    p.oam[126 * 2] = p.oam[127 * 2] = 0x8080; p.highOam[31] |= 0x50;
    ram[0x54] = 2; ram[0x55] = 2; ram[0x56] = 1; publish(2);
    CHECK(FzeroRendererDraw(guarded + 1, v, 1));
    CHECK(!memcmp(intro, guarded + 1, v.width * 224 * sizeof(*intro)));
    /* All three S indicators must be right anchored on their first frame. */
    for (int i = 44; i <= 46; ++i) {
      p.oam[i * 2] = (208 << 8) | (208 + 8 * (i - 44));
      p.oam[i * 2 + 1] = 0x3801;
      p.highOam[i / 4] &= ~(3 << (2 * (i % 4)));
    }
    publish(3); CHECK(FzeroRendererDraw(intro, v, 1));
    for (int i = 0; i < 3; ++i)
      CHECK(intro[208 * v.width + 208 + 8 * i + 2 * v.extra] == 0x00ff00);
    ram[0x55] = 3; ram[0x56] = 0; publish(4);
    CHECK(FzeroRendererDraw(guarded + 1, v, 1));
    CHECK(!memcmp(intro, guarded + 1, v.width * 224 * sizeof(*intro)));
  }
}
static void test_loss_window(void) {
  setup(); ram[0x54] = 3;
  p.screenEnabled[0] = 0;
  p.windowsel = 2u << 20; /* Colour window 1: collapsed to x=0. */
  p.window1left = p.window1right = 0;
  p.cgwsel = 0x10; p.cgadsub = 0x20; p.fixedColor = 31;
  FzeroVideoSettings s; FzeroVideoStock(&s); /* tests build an explicit viewport, not the shipped defaults */
  s.enhanced = true; s.aspect = FZERO_ASPECT_21_9;
  FzeroViewport v = FzeroCalculateViewport(&s, 3440, 1440);
  publish(1); CHECK(FzeroRendererDraw(guarded + 1, v, 1));
  for (int y = 0; y < 224; ++y) {
    CHECK(guarded[1 + y * v.width] == 0xff0000);
    for (int x = 1; x < v.width; ++x) CHECK(guarded[1 + y * v.width + x] == 0);
  }
}
static void test_league_recap(void) {
  static uint32_t output[FZERO_MAX_WIDTH * 224 * 16];
  /* The real final-GP recap still runs in race scene 2. It reuses HUD
   * reservations 0..63 while the player and top power/score BG remain live. */
  setup(); ram[0x55] = 3; ram[0xc3] = 0x11; ram[0x975] = 0x88;
  memset(p.vram, 0, sizeof(p.vram));
  p.screenEnabled[0] = 16; p.cgram[193] = 0x03e0;
  for (unsigned y = 0; y < 8; ++y) p.vram[16 + y] = p.vram[0x1800 + y] = 255;
  for (unsigned slot = 0; slot < 64; ++slot) {
    p.oam[slot*2] = ((64 + slot / 8 * 16) << 8) | (80 + slot % 8 * 8);
    p.oam[slot*2+1] = slot >= 48 && slot < 52 ? 0x3980 : 0x3801;
    p.highOam[slot/4] &= ~(3 << (slot%4*2));
  }
  publish(1);
  for (int aspect = FZERO_ASPECT_STOCK; aspect <= FZERO_ASPECT_FIT; ++aspect) {
    FzeroVideoSettings s; FzeroVideoStock(&s);
    s.enhanced = true; s.aspect = (FzeroAspect)aspect;
    FzeroViewport v = FzeroCalculateViewport(&s, 5120, 1440);
    for (unsigned scale = 1; scale <= 4; scale *= 2) {
      CHECK(scale == 1 ? FzeroRendererDraw(output, v, 1) :
          FzeroRendererDrawHd(output, sizeof(output)/sizeof(*output), v, 1, scale));
      for (unsigned row = 0; row < 8; ++row)
        for (unsigned x = 0; x < (unsigned)v.width; ++x)
          CHECK(output[(64 + row*16)*scale*v.width*scale + x*scale] ==
                (x >= 80 + (unsigned)v.extra && x < 144 + (unsigned)v.extra ? 0x00ff00 : 0));
    }
  }
}
static void test_results_fade(void) {
  static uint32_t results[FZERO_MAX_WIDTH * 224];
  for (int completed = 0; completed <= 1; ++completed)
  for (int aspect = FZERO_ASPECT_STOCK; aspect <= FZERO_ASPECT_FIT; ++aspect) {
    FzeroVideoSettings s; FzeroVideoStock(&s);
    s.enhanced = true; s.aspect = aspect;
    FzeroViewport v = FzeroCalculateViewport(&s, 5120, 1440);
    setup(); memset(p.vram, 0, sizeof(p.vram));
    ram[0x54] = 3; ram[0x55] = 0; ram[0x56] = 5;
    ram[0x5f] = completed ? 0x84 : 0x87;
    p.bgmode = 9; p.screenEnabled[0] = completed ? 0x97 : 0x94;
    if (completed) {
      /* Successful results retain the centered colour-window placing art. */
      p.windowsel = 2u << 20; p.window1left = 120; p.window1right = 135;
      p.cgwsel = 0x10; p.cgadsub = 0x20; p.fixedColor = 0x7c00;
    }
    p.bgXsc[2] = 0x74; p.bgTileAdr = 0x100;
    p.vram[0x7400 + 3 * 32 + 2] = 1; /* score */
    p.vram[0x7400 + 8 * 32 + 12] = 1; /* centered table, not score */
    for (int y = 0; y < 8; ++y) {
      p.vram[0x1008 + y] = 255;
      p.vram[16 + y] = 255;
    }
    p.cgram[193] = 0x03e0;
    /* END GAME reuses race HUD slots 20..30. These must NOT split. */
    for (int slot = 17; slot <= 30; ++slot) {
      p.oam[slot * 2] = (128 << 8) | (80 + 8 * (slot - 17));
      p.oam[slot * 2 + 1] = 0x3801;
      p.highOam[slot / 4] &= ~(3 << ((slot % 4) * 2));
    }
    p.oam[126 * 2] = (190 << 8) | 208; p.oam[126 * 2 + 1] = 0x3801;
    p.highOam[31] &= ~0x30;
    /* The lap table also reuses former car slots. Stale actor coordinates
     * and piece counts must not move or cull those number sprites. */
    ram[0x50] = 1; word(0xac0, 0x320); word(0xb02, 0x88);
    word(0xc52, (unsigned)-350); ram[0x11d2] = 0;
    p.oam[68 * 2] = (100 << 8) | 150; p.oam[68 * 2 + 1] = 0x3801;
    p.highOam[17] &= ~3;
    publish(1); CHECK(FzeroRendererDraw(results, v, 1));
    CHECK(results[24 * v.width + 16] == 0xff0000);
    CHECK(results[64 * v.width + v.extra + 96] == 0xff0000);
    CHECK(results[190 * v.width + 2 * v.extra + 208] == 0x00ff00);
    CHECK(results[100 * v.width + v.extra + 150] == 0x00ff00);
    if (completed) {
      CHECK(results[80 * v.width + v.extra + 120] == 0x0000ff);
      CHECK(results[80 * v.width + v.extra + 119] == 0);
    }
    for (int x = 80; x < 192; ++x)
      CHECK(results[128 * v.width + v.extra + x] == 0x00ff00);
    for (int choice = 0; choice <= 1; ++choice) {
      ram[0x55] = 5; ram[0x56] = choice;
      for (int brightness = 15; brightness >= 0; --brightness) {
        p.inidisp = brightness; publish(2 + 15 - brightness);
        CHECK(FzeroRendererDraw(guarded + 1, v, 0.5));
        for (int i = 0; i < v.width * 224; ++i) {
          unsigned c = results[i];
          unsigned expected = (((c >> 16) * brightness / 15) << 16) |
              ((((c >> 8) & 255) * brightness / 15) << 8) |
              ((c & 255) * brightness / 15);
          CHECK(guarded[1 + i] == expected);
        }
      }
    }
  }
}
/* Retail streams the Mode 7 tilemap for the stock 256-pixel viewport only:
 * $03:9243 keeps one 1024x1024-unit world square uploaded, anchored at
 * $00A8/$00AA, and the tilemap aliases the 8192x4096 world every 1024 units.
 * Give the live tilemap one tile everywhere and the course tables ($00B0 block
 * grid -> $7F:5000 sub-row pointers -> 2x2 tile groups) a different one, then
 * check that only the samples outside the square follow the tables.
 *
 * The matrix is retail's shape - a quarter turn, so a=d=0 - because an
 * identity matrix cancels the rotation centre out of the origin and would hide
 * every mistake in how a texel is measured against it. Here the camera sits at
 * (512,512) with the square at the world origin, so a scanline samples world x
 * 1024-line and world y equal to the screen coordinate: the left margin falls
 * outside the square and everything from the stock columns rightwards does not. */
static void course_matrix(int centre_y, int scroll_y) {
  p.m7matrix[0] = 0; p.m7matrix[1] = -256;
  p.m7matrix[2] = 256; p.m7matrix[3] = 0;
  p.m7matrix[4] = 512; p.m7matrix[5] = (int16_t)centre_y;
  p.m7matrix[6] = 0; p.m7matrix[7] = (int16_t)scroll_y;
}

static void course_expect(const uint32_t *out, FzeroViewport v) {
  for (int y = 0; y < 224; ++y) {
    const uint32_t *row = out + y * v.width;
    for (int x = 0; x < v.extra; ++x) CHECK(row[x] == 0x00ff00);
    for (int x = v.extra; x < v.width; ++x) CHECK(row[x] == 0xff0000);
  }
}

static void test_course_streaming(void) {
  setup();
  ram[0x55] = 3;
  course_matrix(512, 0);
  p.cgram[1] = 0x001f; p.cgram[2] = 0x03e0;
  /* Tile 1 everywhere in the tilemap; character data for tiles 1 and 7. */
  for (unsigned i = 0; i < 0x4000; ++i) p.vram[i] = 1;
  for (unsigned i = 0; i < 64; ++i) {
    p.vram[1 * 64 + i] |= 1 << 8;
    p.vram[7 * 64 + i] |= 2 << 8;
  }
  word(0xb70, 512); word(0xb90, 512);   /* camera */
  word(0xa8, 0); word(0xaa, 0);         /* streaming anchor */
  uint8_t *bank = ram + 0x10000;
  ram[0xb0] = 0x00; ram[0xb1] = 0x40;          /* block grid at $7F:4000 */
  for (unsigned i = 0; i < 32 * 16; ++i) bank[0x4000 + i] = 3;
  for (unsigned i = 0; i < 16; ++i) {          /* block 3's sixteen sub-rows */
    bank[0x5000 + 3 * 32 + i * 2] = 0x00;
    bank[0x5000 + 3 * 32 + i * 2 + 1] = 0x60;  /* -> $7F:6000 */
    bank[0x6000 + i * 2] = 0x00;
    bank[0x6000 + i * 2 + 1] = 0x61;           /* -> $7F:6100 */
  }
  for (unsigned i = 0; i < 4; ++i) bank[0x6100 + i] = 7;
  publish(1);
  FzeroVideoSettings s; FzeroVideoStock(&s); s.enhanced = true;
  s.aspect = FZERO_ASPECT_21_9;
  FzeroViewport v = FzeroCalculateViewport(&s, 3840, 1646);
  CHECK(v.width == 448 && v.extra == 96);
  uint32_t *out = guarded + 1;
  memcpy(before, ram, sizeof(ram));
  CHECK(FzeroRendererDraw(out, v, 1));
  CHECK(!memcmp(ram, before, sizeof(ram)));
  course_expect(out, v);

  /* The anchor's low bits do not move the square: $03:9346 and $03:9381 select
   * the streamed strip with ($14 & $03F0) and ($12 & $03F0), so it always
   * starts on a 16-unit block boundary. An unaligned anchor must not push the
   * first block row out of the square. */
  word(0xa8, 8); word(0xaa, 8);
  publish(2);
  CHECK(FzeroRendererDraw(out, v, 1));
  course_expect(out, v);
  word(0xa8, 0); word(0xaa, 0);
  publish(3);
  CHECK(FzeroRendererDraw(out, v, 1));

  /* Retail writes either representative of the camera's map position: some
   * frames carry the camera's own value and some that plus 1024. A frame's own
   * origin and centre always agree, but an interpolated origin takes the
   * shortest path across the seam, so a presentation between two such frames
   * must still resolve the same world cells. Raising the centre and the
   * vertical scroll together leaves the geometry alone and moves only the
   * representative. */
  course_matrix(512 + 1024, 1024);
  publish(4);
  for (double blend = 0; blend < 1.0; blend += 0.25) {
    CHECK(FzeroRendererDraw(out, v, blend));
    course_expect(out, v);
  }
  CHECK(FzeroRendererDraw(out, v, 1));
  course_expect(out, v);

  /* FzeroMode7Interpolate keeps the current scanline when the two lines carry
   * different control bits, and the compositor does not blend at all when the
   * previous line was not Mode 7. The centre and the camera have to follow the
   * same decision, or a current-frame texel is measured against a blended
   * centre and every sample moves a whole map period. */
  course_matrix(512, 0);
  publish(5);
  course_matrix(512 + 1024, 1024);
  p.m7sel ^= 0x04; /* read by neither the transform nor the fetch */
  publish(6);
  for (double blend = 0; blend < 1.0; blend += 0.25) {
    CHECK(FzeroRendererDraw(out, v, blend));
    course_expect(out, v);
  }
  p.m7sel ^= 0x04;
  course_matrix(512, 0);
  p.bgmode = 1;
  publish(7);
  course_matrix(512 + 1024, 1024);
  p.bgmode = 7;
  publish(8);
  for (double blend = 0; blend < 1.0; blend += 0.25) {
    CHECK(FzeroRendererDraw(out, v, blend));
    course_expect(out, v);
  }
  course_matrix(512, 0);
  publish(9);
  CHECK(FzeroRendererDraw(out, v, 1));

  /* Stock width never consults the tables. */
  s.enhanced = false;
  v = FzeroCalculateViewport(&s, 1024, 768);
  CHECK(v.width == 256 && FzeroRendererDraw(out, v, 1));
  for (int x = 0; x < 256; ++x) CHECK(out[x] == 0xff0000);
  /* Neither does a scene that is not a live race. */
  ram[0x54] = 1; publish(10);
  s.enhanced = true; s.aspect = FZERO_ASPECT_21_9;
  v = FzeroCalculateViewport(&s, 3840, 1646);
  CHECK(FzeroRendererDraw(out, v, 1));
  CHECK(out[0] == 0xff0000 && out[v.extra] == 0x123456);
}

static void test_player_spark(void) {
  setup();
  p.screenEnabled[0] = 16;
  memset(p.vram, 0, sizeof(p.vram));
  for (int y = 0; y < 8; ++y) p.vram[16 + y] = 255;
  p.cgram[193] = 0x03e0;
  ram[0x50] = 1; ram[0x55] = 3;
  /* $02BC is the effect next to the player, between the boost and rank HUD
   * reservations. It must stay at its guest position at every aspect. */
  p.oam[47 * 2] = (170 << 8) | 120; p.oam[47 * 2 + 1] = 0x3801;
  p.highOam[47 / 4] &= ~(3u << ((47 % 4) * 2));
  p.oam[46 * 2] = (200 << 8) | 120; p.oam[46 * 2 + 1] = 0x3801;
  p.highOam[46 / 4] &= ~(3u << ((46 % 4) * 2));
  publish(1);
  for (int aspect = FZERO_ASPECT_16_9; aspect <= FZERO_ASPECT_32_9; ++aspect) {
    FzeroVideoSettings s; FzeroVideoStock(&s);
    s.enhanced = true; s.aspect = (FzeroAspect)aspect;
    FzeroViewport v = FzeroCalculateViewport(&s, 5120, 1440);
    uint32_t *out = guarded + 1;
    CHECK(FzeroRendererDraw(out, v, 1));
    CHECK(out[170 * v.width + v.extra + 120] == 0x00ff00);
    CHECK(out[170 * v.width + 2 * v.extra + 120] == 0);
    CHECK(out[200 * v.width + 2 * v.extra + 120] == 0x00ff00);
  }
}

static void test_explosion_slots(void) {
  static uint32_t hd[FZERO_MAX_WIDTH * 224 * 16];
  /* Rank -> expanding explosion -> smoke -> rank, without resetting the
   * renderer. Include flipped pieces and the first non-rank reservation. */
  static const unsigned tiles[] = {0x180, 0x189, 0x190, 0x199,
                                   0x120, 0x126, 0x128, 0x140, 0x142, 0x181};
  for (int aspect = FZERO_ASPECT_STOCK; aspect <= FZERO_ASPECT_FIT; ++aspect) {
    setup();
    p.screenEnabled[0] = 16;
    memset(p.vram, 0, sizeof(p.vram));
    p.cgram[177] = 0x03e0; /* effect palette */
    p.cgram[129] = 0x001f; /* rank palette */
    ram[0x50] = 1; ram[0x55] = 3;
    FzeroVideoSettings s; FzeroVideoStock(&s);
    s.enhanced = true; s.aspect = (FzeroAspect)aspect;
    FzeroViewport v = FzeroCalculateViewport(&s, 5120, 1440);
    for (unsigned phase = 0; phase < sizeof(tiles) / sizeof(*tiles); ++phase) {
      unsigned tile = tiles[phase];
      bool rank = phase < 4 || phase == 9;
      for (int y = 0; y < 8; ++y) p.vram[0x1000 + (tile & 255) * 16 + y] = 255;
      for (int slot = 48; slot <= 52; ++slot) {
        p.oam[slot * 2] = (100 << 8) | (80 + (slot - 48) * 8);
        p.oam[slot * 2 + 1] = tile | (rank ? 0x3000 : 0x3600) | ((slot & 3) << 14);
        p.highOam[slot / 4] &= ~(3u << ((slot % 4) * 2));
      }
      publish(phase + 1);
      for (unsigned scale = 1; scale <= 4; scale *= 2) {
        for (int blend = 1; blend <= 2; ++blend) {
          CHECK(scale == 1 ? FzeroRendererDraw(hd, v, blend * 0.5) :
              FzeroRendererDrawHd(hd, sizeof(hd) / sizeof(*hd), v, blend * 0.5, scale));
          /* Check the entire row: no piece can be detached, duplicated or
           * lost, and genuine rank digits must still follow the left edge. */
          for (int x = 0; x < v.width; ++x) {
            int first = 80 + (rank ? 0 : v.extra);
            int last = 112 + v.extra;
            bool ink = (x >= first && x < first + 32) || (x >= last && x < last + 8);
            unsigned expected = ink ? (rank ? 0xff0000 : 0x00ff00) : 0;
            for (unsigned sub = 0; sub < scale; ++sub)
              CHECK(hd[100 * scale * v.width * scale + x * scale + sub] == expected);
          }
        }
      }
    }
  }
}

static void test_hd_mode7(void) {
  static uint32_t hd[FZERO_MAX_WIDTH * 224 * FZERO_HD_SCALE_MAX * FZERO_HD_SCALE_MAX + 2];
  setup();
  FzeroVideoSettings settings; FzeroVideoStock(&settings);
  FzeroViewport v = FzeroCalculateViewport(&settings, 800, 600);
  memset(p.vram, 0, sizeof(p.vram));
  for (unsigned i = 0; i < 0x4000; ++i) p.vram[i] = 1;
  for (unsigned i = 0; i < 64; ++i) p.vram[64 + i] |= (i + 1) << 8;
  for (unsigned i = 0; i < 256; ++i) p.cgram[i] = (uint16_t)i;
  p.m7matrix[0] = p.m7matrix[3] = 512;
  publish(1);
  CHECK(FzeroRendererHasFrame());
  CHECK(FzeroRendererDraw(guarded + 1, v, 1));
  for (unsigned scale = 2; scale <= FZERO_HD_SCALE_MAX; ++scale) {
    size_t count = (size_t)v.width * 224 * scale * scale;
    hd[0] = hd[count + 1] = 0xdeadbeef;
    CHECK(!FzeroRendererDrawHd(hd + 1, count - 1, v, 1, scale));
    CHECK(FzeroRendererDrawHd(hd + 1, count, v, 1, scale));
    CHECK(hd[0] == 0xdeadbeef && hd[count + 1] == 0xdeadbeef);
    CHECK(hd[1] == palette_rgb(p.cgram[17]));
    CHECK(hd[1 + (scale + 1) / 2] == palette_rgb(p.cgram[18]));
    CHECK(hd[1 + v.width * scale * ((scale + 1) / 2)] == palette_rgb(p.cgram[25]));
    /* Higher resolution cannot alter the native render or published source. */
    CHECK(FzeroRendererDraw(hd + 1, v, 1));
    CHECK(!memcmp(hd + 1, guarded + 1, (size_t)v.width * 224 * sizeof(*hd)));
  }
  const unsigned invalid_scales[] = {0, 1, 11, UINT32_MAX};
  for (unsigned i = 0; i < countof(invalid_scales); ++i) {
    hd[1] = 0xdeadbeef;
    CHECK(!FzeroRendererDrawHd(hd + 1, countof(hd) - 2, v, 1, invalid_scales[i]));
    CHECK(hd[1] == 0xdeadbeef);
  }
  /* An HDMA jump to another origin must not be smoothed across the split. */
  FzeroRendererBeginFrame(ram, 2);
  for (unsigned y = 1; y <= 224; ++y) {
    p.m7matrix[6] = y < 100 ? 0 : 1;
    FzeroRendererCaptureLine(&p, y);
  }
  FzeroRendererEndFrame(&p, stock);
  CHECK(FzeroRendererDrawHd(hd + 1, countof(hd) - 2, v, 1, 2));
  CHECK(hd[1 + 197 * 512] == palette_rgb(p.cgram[57]));
  /* Geometry follows neighbouring scanlines instead of reusing one line's
   * matrix for a whole block: 2 -> 4 horizontal texels across this band. */
  FzeroRendererBeginFrame(ram, 3);
  p.m7matrix[6] = 0;
  for (unsigned y = 1; y <= 224; ++y) {
    p.m7matrix[0] = y == 1 ? 512 : 1024;
    FzeroRendererCaptureLine(&p, y);
  }
  FzeroRendererEndFrame(&p, stock);
  CHECK(FzeroRendererDrawHd(hd + 1, countof(hd) - 2, v, 1, 2));
  CHECK(hd[1 + 512 + 2] == palette_rgb(p.cgram[28]));
  /* Temporal interpolation remains independent of spatial resolution. */
  p.m7matrix[0] = 512; p.m7matrix[6] = 0;
  FzeroRendererReset(); publish(10);
  p.m7matrix[6] = 1; publish(11);
  CHECK(FzeroRendererDrawHd(hd + 1, countof(hd) - 2, v, 0.5, 2));
  CHECK(hd[1] == palette_rgb(p.cgram[18]));
  CHECK(FzeroRendererDrawHd(hd + 1, countof(hd) - 2, v, 1, 2));
  CHECK(hd[1] == palette_rgb(p.cgram[19]));
  /* Flat screens retain every original pixel, including centered menus. */
  ram[0x81] = 0; publish(4);
  CHECK(FzeroRendererDrawHd(hd + 1, countof(hd) - 2, v, 1, 4));
  for (size_t i = 0; i < 256 * 224 * 16; ++i) CHECK(hd[1 + i] == 0x123456);
  FzeroRendererReset();
  CHECK(!FzeroRendererHasFrame());
  CHECK(!FzeroRendererDrawHd(hd + 1, countof(hd) - 2, v, 1, 2));
}

static void test_hd_results(void) {
  static uint32_t race[FZERO_MAX_WIDTH * 224 * 16], results[FZERO_MAX_WIDTH * 224 * 16];
  const FzeroViewport views[] = {{256, 0, 4.0 / 3.0, false}, {342, 43, 16.0 / 9.0, true}};
  for (unsigned view = 0; view < countof(views); ++view) {
    FzeroViewport v = views[view];
    for (unsigned scale = 2; scale <= 4; scale *= 2) {
      setup();
      memset(p.vram, 0, sizeof(p.vram));
      for (unsigned i = 0; i < 0x4000; ++i) p.vram[i] = 1;
      for (unsigned i = 0; i < 64; ++i) p.vram[64 + i] |= (i + 1) << 8;
      for (unsigned i = 0; i < 256; ++i) p.cgram[i] = (uint16_t)i;
      p.m7matrix[0] = p.m7matrix[3] = 512;
      publish(1);
      size_t count = (size_t)v.width * 224 * scale * scale;
      CHECK(FzeroRendererDrawHd(race, count, v, 1, scale));
      /* A finish freezes this same camera/road. Entering the results scene
       * must retain every subpixel, including the widened track margins. */
      ram[0x54] = 3; ram[0x5f] = 0x80;
      for (unsigned phase = 0; phase <= 5; ++phase) {
        ram[0x55] = phase;
        publish(phase + 2);
        CHECK(FzeroRendererDrawHd(results, count, v, 0.5, scale));
        CHECK(!memcmp(race, results, count * sizeof(*race)));
      }
      /* The first native pixel really contains distinct HD texels. Merely
       * upscaling the frozen native framebuffer cannot satisfy this test. */
      size_t first = (size_t)v.extra * scale;
      CHECK(results[first] != results[first + scale / 2]);
      CHECK(results[first] != results[first + v.width * scale * (scale / 2)]);
    }
  }
}

static void test_hd_composition_cache(void) {
  static uint32_t native[FZERO_MAX_WIDTH * 224 + 2];
  static uint32_t hd[FZERO_MAX_WIDTH * 224 * 16 + 2];
  static uint32_t hd_only[FZERO_MAX_WIDTH * 224 * 16];
  FzeroViewport v = {342, 43, 16.0 / 9.0, true};
  size_t count = (size_t)v.width * 224;
  for (unsigned scenario = 0; scenario < 64; ++scenario) {
    setup();
    ram[0x55] = 3;
    for (unsigned i = 0; i < 0x8000; ++i) p.vram[i] = (i & 63) << 8;
    for (unsigned i = 0; i < 256; ++i) p.cgram[i] = (i * 619 + 37) & 0x7fff;
    p.inidisp = scenario % 16;
    p.m7sel = (scenario & 3) | ((scenario & 16) ? 0x80 : 0) |
        ((scenario & 32) ? 0x40 : 0);
    p.screenEnabled[0] = (scenario & 1 ? 1 : 0) | (scenario & 2 ? 16 : 0);
    p.screenEnabled[1] = (scenario & 4 ? 1 : 0) | (scenario & 8 ? 16 : 0);
    p.screenWindowed[0] = scenario * 13;
    p.screenWindowed[1] = scenario * 23;
    p.windowsel = scenario * 0x194ad;
    p.wbgobjlog = scenario * 751;
    p.window1left = 60; p.window1right = 173;
    p.window2left = 99; p.window2right = 255;
    p.cgadsub = scenario * 37;
    p.cgwsel = (scenario * 14) & 0xfe;
    p.fixedColor = (scenario * 1739) & 0x7fff;
    p.oam[0] = (80 << 8) | 100;
    p.oam[1] = 64 | ((scenario & 3) << 12) | 0xe00;
    p.highOam[0] &= ~3;
    publish(scenario + 1);
    CHECK(FzeroRendererDraw(guarded + 1, v, 1));
    for (unsigned scale = 2; scale <= 4; scale *= 2) {
      size_t hd_count = count * scale * scale;
      native[0] = native[count + 1] = hd[0] = hd[hd_count + 1] = 0xdeadbeef;
      native[1] = hd[1] = 0xdeadbeef;
      CHECK(!FzeroRendererDrawPresentation(native + 1, hd + 1, hd_count - 1, v, 1, scale));
      CHECK(native[1] == 0xdeadbeef && hd[1] == 0xdeadbeef);
      CHECK(FzeroRendererDrawPresentation(native + 1, hd + 1, hd_count, v, 1, scale));
      CHECK(native[0] == 0xdeadbeef && native[count + 1] == 0xdeadbeef);
      CHECK(hd[0] == 0xdeadbeef && hd[hd_count + 1] == 0xdeadbeef);
      CHECK(!memcmp(native + 1, guarded + 1, count * sizeof(*native)));
      /* With integral transforms, each HD pixel's first subpixel lands on
       * the exact native texel. The native compositor is the independent
       * oracle for windows, OBJ priority, transparency and colour math. */
      for (int y = 0; y < 224; ++y) for (int x = 0; x < v.width; ++x)
        CHECK(hd[1 + ((size_t)y * scale * v.width + x) * scale] == native[1 + y * v.width + x]);
      CHECK(FzeroRendererDrawHd(hd_only, hd_count, v, 1, scale));
      CHECK(!memcmp(hd + 1, hd_only, hd_count * sizeof(*hd)));
    }
  }
  setup(); p.inidisp = 128; publish(70);
  CHECK(FzeroRendererDrawPresentation(native + 1, hd + 1, count * 4, v, 1, 2));
  for (size_t i = 0; i < count; ++i) CHECK(native[1 + i] == 0);
  for (size_t i = 0; i < count * 4; ++i) CHECK(hd[1 + i] == 0);
}

int main(void) {
  FzeroVideoSettings s; FzeroVideoStock(&s); /* tests build an explicit viewport, not the shipped defaults */ s.enhanced = true; s.aspect = FZERO_ASPECT_32_9;
  FzeroViewport v = FzeroCalculateViewport(&s, 5120, 1440);
  uint32_t *out = guarded + 1;
  setup();
  CHECK(!FzeroRendererDraw(out, v, 1));
  guarded[0] = guarded[v.width * 224 + 1] = 0xdeadbeef;
  publish(1); memcpy(before, ram, sizeof(ram));
  CHECK(FzeroRendererDraw(out, v, 1));
  CHECK(out[0] == 0xff0000 && out[v.width-1] == 0xff0000);
  CHECK(guarded[0] == 0xdeadbeef && guarded[v.width*224+1] == 0xdeadbeef);
  CHECK(!memcmp(ram, before, sizeof(ram)));
  memset(p.vram, 0, sizeof(p.vram));
  CHECK(FzeroRendererDraw(out, v, 1) && out[0] == 0xff0000); /* immutable publication */
  ram[0x54] = 1; publish(2);
  CHECK(FzeroRendererDraw(out, v, 0.5));
  CHECK(out[0] == 0 && out[v.extra] == 0x123456 && out[v.extra + 255] == 0x123456);
  setup(); p.screenEnabled[0] = 16; memset(p.vram, 0, sizeof(p.vram));
  for (int y = 0; y < 8; ++y) p.vram[16 + y] = 255;
  p.cgram[193] = 0x03e0;
  ram[0x50] = 1; word(0xb02, 0x88); word(0xc52, 318); word(0xc62, 80);
  word(0xac0, 0x320); ram[0x11d2] = 1;
  p.oam[68*2] = (80 << 8) | 54; p.oam[68*2+1] = 0x3801;
  publish(3);
  CHECK(FzeroRendererDraw(out, v, 1));
  CHECK(out[80*v.width + v.extra + 310] == 0x00ff00); /* 9-bit X restored on right */
  word(0xc52, (unsigned)-82); p.oam[68*2] = (80 << 8) | 166;
  publish(4);
  CHECK(FzeroRendererDraw(out, v, 1));
  CHECK(out[80*v.width + v.extra - 90] == 0x00ff00); /* left margin */
  /* Stale player/effect tiles are parked offscreen with arbitrary Y/attrs. */
  word(0xac0, 0x300); word(0xb00, 0x88);
  p.oam[68*2] = 128; p.oam[68*2+1] = 0x3801;
  p.oam[52*2] = (100 << 8) | 128; p.oam[52*2+1] = 0x3801;
  publish(5); CHECK(FzeroRendererDraw(out, v, 1));
  CHECK(out[v.extra - 128] == 0 && out[100*v.width + v.extra - 128] == 0);
  /* The intro reuses HUD reservations for centered course-title letters. */
  p.oam[20*2] = (100 << 8) | 100; p.oam[20*2+1] = 0x3801;
  p.highOam[5] &= ~3;
  ram[0x55] = 2;
  publish(6); CHECK(FzeroRendererDraw(out, v, 1));
  CHECK(out[100*v.width + v.extra + 100] == 0x00ff00);
  CHECK(out[100*v.width + 100] == 0);
  FzeroRendererReset(); CHECK(!FzeroRendererDraw(out, v, 0));
  test_panorama();
  test_hud_transition();
  test_adaptive_scenes();
  test_intro_counter();
  test_loss_window();
  test_results_fade();
  test_league_recap();
  test_course_streaming();
  test_player_spark();
  test_explosion_slots();
  test_hd_mode7();
  test_hd_results();
  test_hd_composition_cache();
  test_view_isolation();
  FzeroRendererShutdown();
  CHECK(FzeroRendererWorkerCount() == 1);
  puts("F-Zero renderer: bounds, immutable frames, scene fallback, car identity, signed X, panorama wrap and HUD transitions passed");
  return 0;
}
