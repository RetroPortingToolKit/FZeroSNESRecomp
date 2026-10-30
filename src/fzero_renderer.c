#include "fzero_renderer.h"
#include "fzero_mode7.h"
#include "render_workers.h"
#include "snes/mode7_hd.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct FzeroRasterLine {
  uint8_t registers[PPU_SAVESTATE_REGS_SIZE];
  uint16_t palette[256], oam[256];
  uint8_t high_oam[32];
} FzeroRasterLine;

typedef struct FzeroSourceFrame {
  FzeroRasterLine lines[224];
  uint16_t vram[0x8000];
  uint32_t stock[256 * 224];
  uint8_t ram[0x20000];
  unsigned frame;
  bool valid;
} FzeroSourceFrame;

struct FzeroRenderView {
  FzeroSourceFrame frames[2];
  unsigned current;
  bool race[2]; /* Host metadata stays outside the legacy capture file. */
  Ppu scanout; /* Never points at guest state or another view's scratch. */
  SnesRenderWorkers *render_workers;
  Ppu *worker_scratch;
  bool workers_attempted;
  uint8_t *course_map, *course_bank, *course_dependencies;
  unsigned course_grid;
};
static FzeroRenderView single_player;
static void race_color_math(Ppu *p);

static void prepare_workers(FzeroRenderView *view) {
  if (view->workers_attempted) return;
  view->workers_attempted = true;
  view->render_workers = SnesRenderWorkersCreate(0);
  if (!view->render_workers) return;
  view->worker_scratch = calloc(SnesRenderWorkersCount(view->render_workers), sizeof(*view->worker_scratch));
  if (!view->worker_scratch) {
    SnesRenderWorkersDestroy(view->render_workers);
    view->render_workers = NULL;
  }
}

unsigned FzeroRenderViewWorkerCount(const FzeroRenderView *view) {
  return view ? SnesRenderWorkersCount(view->render_workers) : 0;
}
static void shutdown_view(FzeroRenderView *view) {
  SnesRenderWorkersDestroy(view->render_workers);
  view->render_workers = NULL;
  free(view->worker_scratch); view->worker_scratch = NULL;
  view->workers_attempted = false;
  free(view->course_map); free(view->course_bank); free(view->course_dependencies);
  view->course_map = view->course_bank = view->course_dependencies = NULL;
}
FzeroRenderView *FzeroRenderViewCreate(void) { return calloc(1, sizeof(FzeroRenderView)); }
void FzeroRenderViewDestroy(FzeroRenderView *view) {
  if (!view) return;
  shutdown_view(view);
  free(view);
}


bool FzeroRenderViewLoadCapture(FzeroRenderView *view, const char *path) {
  if (!view || !path) return false;
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  /* Alternate buffers the way FzeroRendererBeginFrame does, so replaying a
   * sequence offline presents the previous frame to the compositor exactly as
   * a session does and can exercise the interpolated presentation path. */
  view->current ^= 1;
  FzeroSourceFrame *frame = &view->frames[view->current];
  bool ok = fread(frame, sizeof(*frame), 1, f) == 1 && fgetc(f) == EOF;
  fclose(f);
  frame->valid = ok;
  view->race[view->current] = false;
  return ok;
}
const uint32_t *FzeroRenderViewStockFrame(const FzeroRenderView *view) {
  return view ? view->frames[view->current].stock : NULL;
}
bool FzeroRenderViewHasFrame(const FzeroRenderView *view) {
  return view && view->frames[view->current].valid;
}

void FzeroRenderViewReset(FzeroRenderView *view) {
  if (view) view->frames[0].valid = view->frames[1].valid = false;
}
void FzeroRenderViewBeginFrame(FzeroRenderView *view, const uint8_t ram[0x20000], unsigned frame) {
  if (!view || !ram) return;
  view->current ^= 1;
  FzeroSourceFrame *f = &view->frames[view->current];
  f->valid = false;
  view->race[view->current] = false;
  f->frame = frame;
  memcpy(f->ram, ram, sizeof(f->ram));
}
void FzeroRenderViewCaptureLine(FzeroRenderView *view, const Ppu *p, unsigned line) {
  if (!view || !p || line < 1 || line > 224) return;
  FzeroRasterLine *l = &view->frames[view->current].lines[line - 1];
  memcpy(l->registers, p, sizeof(l->registers));
  memcpy(l->palette, p->cgram, sizeof(l->palette));
  memcpy(l->oam, p->oam, sizeof(l->oam));
  memcpy(l->high_oam, p->highOam, sizeof(l->high_oam));
}

static void dump_frame(const FzeroSourceFrame *f, Ppu *scratch) {
  const char *number = getenv("FZERO_CAPTURE_FRAME");
  const char *numbers = getenv("FZERO_CAPTURE_FRAMES");
  const char *prefix = getenv("FZERO_CAPTURE_PREFIX");
  const char *every_text = getenv("FZERO_CAPTURE_EVERY");
  unsigned every = every_text ? (unsigned)strtoul(every_text, NULL, 10) : 0;
  bool selected = number && strtoul(number, NULL, 10) == f->frame;
  if (every && f->frame % every == 0) selected = true;
  if (numbers) for (const char *p = numbers; *p;) {
    char *end;
    if (strtoul(p, &end, 10) == f->frame) selected = true;
    if (end == p || *end != ',') break;
    p = end + 1;
  }
  if (!selected || !prefix) return;
  char path[1024];
  char numbered_prefix[960];
  if (numbers || every) {
    if (snprintf(numbered_prefix, sizeof(numbered_prefix), "%s-%06u", prefix, f->frame) >= (int)sizeof(numbered_prefix)) return;
    prefix = numbered_prefix;
  }
  if (snprintf(path, sizeof(path), "%s.json", prefix) >= (int)sizeof(path)) return;
  FILE *out = fopen(path, "w");
  if (!out) return;
  fprintf(out, "{\"frame\":%u,\"state\":[%u,%u,%u],\"mode7\":%u,\"lines\":[",
          f->frame, f->ram[0x54], f->ram[0x55], f->ram[0x56], f->ram[0x81]);
  for (int y = 0; y < 224; ++y) {
    memcpy(scratch, f->lines[y].registers, PPU_SAVESTATE_REGS_SIZE);
    fprintf(out, "%s{\"y\":%d,\"mode\":%u,\"brightness\":%u,\"main\":%u,\"sub\":%u,\"math\":%u,\"cgwsel\":%u,\"fixed\":%u,\"matrix\":[",
            y ? "," : "", y, scratch->bgmode, scratch->inidisp, scratch->screenEnabled[0],
            scratch->screenEnabled[1], scratch->cgadsub, scratch->cgwsel, scratch->fixedColor);
    for (int j = 0; j < 8; ++j) fprintf(out, "%s%d", j ? "," : "", scratch->m7matrix[j]);
    fprintf(out, "],\"bgsc\":[%u,%u,%u,%u],\"tileadr\":%u,\"scroll\":[%u,%u,%u,%u,%u,%u,%u,%u]}",
            scratch->bgXsc[0], scratch->bgXsc[1], scratch->bgXsc[2], scratch->bgXsc[3], scratch->bgTileAdr,
            scratch->hScroll[0], scratch->vScroll[0], scratch->hScroll[1], scratch->vScroll[1],
            scratch->hScroll[2], scratch->vScroll[2], scratch->hScroll[3], scratch->vScroll[3]);
  }
  fprintf(out, "],\"oam\":[");
  const FzeroRasterLine *l = &f->lines[100];
  for (int i = 0; i < 128; ++i) {
    unsigned word = l->oam[i * 2], attr = l->oam[i * 2 + 1];
    unsigned high = l->high_oam[i / 4] >> ((i % 4) * 2);
    fprintf(out, "%s[%d,%u,%u,%u,%u]", i ? "," : "", i, (word & 255) | ((high & 1) << 8), word >> 8, attr, (high >> 1) & 1);
  }
  fputs("]}\n", out);
  fclose(out);
  snprintf(path, sizeof(path), "%s.bin", prefix);
  out = fopen(path, "wb");
  if (out) { fwrite(f, sizeof(*f), 1, out); fclose(out); }
}

void FzeroRenderViewEndFrame(FzeroRenderView *view, const Ppu *p, const uint32_t stock[256 * 224]) {
  if (!view || !p || !stock) return;
  FzeroSourceFrame *f = &view->frames[view->current];
  memcpy(f->vram, p->vram, sizeof(f->vram));
  memcpy(f->stock, stock, sizeof(f->stock));
  f->valid = true;
  /* Preserve the single-player capture format and filenames. Callers with
   * multiple views choose their capture destination explicitly. */
  if (view == &single_player) dump_frame(f, &view->scanout);
}

bool FzeroRenderViewCaptureRace(FzeroRenderView *view, const FzeroRenderView *assets,
                                const uint8_t world[0x20000], const FzeroActor *actor,
                                const FzeroCamera *camera, unsigned frame) {
  if (!assets) assets = &single_player;
  if (!view || view == assets || !world || !actor || !camera ||
      actor->x >= 8192 || actor->y >= 4096 ||
      !FzeroRenderViewHasFrame(assets)) return false;
  const FzeroSourceFrame *source = &assets->frames[assets->current];
  if (source->ram[0x54] != 2 || !source->ram[0x81]) return false;
  view->current ^= 1;
  FzeroSourceFrame *f = &view->frames[view->current];
  *f = *source;
  memcpy(f->ram, world, sizeof(f->ram));
  f->frame = frame;
  f->ram[0x54] = 2; f->ram[0x55] = 3; f->ram[0x56] = 0; f->ram[0x81] = 1;
  f->ram[0xac] = camera->inverse_angle;
  f->ram[0xb70] = actor->x; f->ram[0xb71] = actor->x >> 8;
  f->ram[0xb90] = actor->y; f->ram[0xb91] = actor->y >> 8;
  uint16_t palette_indices[256],palette_map[256];
  for(unsigned i=0;i<256;++i)palette_indices[i]=(uint16_t)i;
  FzeroCameraCoursePalette(palette_map,palette_indices,source->ram[0xcf5],source->ram[0x9a],frame,
        world[FZERO_WORLD_PALETTE]?world[FZERO_WORLD_CYCLES]:-1,
      world+FZERO_WORLD_CYCLE_OFFSETS);
  for (unsigned y = 0; y < 224; ++y) {
    for(unsigned i=0;i<256;++i)f->lines[y].palette[i]=source->lines[y].palette[palette_map[i]];
    /* Work on owned PPU scratch, then retain only its register prefix. */
    Ppu *p = &view->scanout;
    memcpy(p, f->lines[y].registers, PPU_SAVESTATE_REGS_SIZE);
    p->screenEnabled[0] &= 3; p->screenEnabled[1] &= 3;
    if ((p->bgmode & 7) == 7) {
      if (!FzeroCameraAtLine(camera, y + 1, p->m7matrix)) {
        f->valid = false; return false;
      }
    } else if (y < 47) {
      /* A versus pane uses the whole native height. The original HUD hid
       * this panorama band; its BG1/BG2 scenery is still present in VRAM. */
      p->screenEnabled[0] = 3;
      p->cgadsub = 0; /* Native power-window math must not tint the panorama. */
      p->hScroll[0] = camera->background_h; p->vScroll[0] = camera->background_v;
      p->hScroll[1] = camera->skyline_h; p->vScroll[1] = camera->skyline_v;
    }
    /* Stock HUD power-window clipping has no ownership in a versus pane. */
    p->screenWindowed[0] = p->screenWindowed[1] = 0;
    if((p->bgmode&7)==7)race_color_math(p);
    else {p->windowsel=0;p->cgwsel&=15;}
    /* BF5F/C057 flashes five course colors through the electric-surface
     * ramp. Each camera owns its human's phase; the shared guest is frozen. */
    for(unsigned color=0;color<5;++color) {
      f->lines[y].palette[18+color]=FzeroCameraFlashColor(f->lines[y].palette[18+color],actor->electric_flash,false);
    }
    /* BF5F/C07E/C095 retains a separate three-color surface flash until
     * F5 expires. It must not leak from one human's view into the other. */
    static const unsigned surface_colors[]={25,26,39};
    for(unsigned color=0;color<3;++color) {
      unsigned index=surface_colors[color];
      f->lines[y].palette[index]=FzeroCameraFlashColor(f->lines[y].palette[index],actor->track_flash,true);
    }
    memcpy(f->lines[y].registers, p, PPU_SAVESTATE_REGS_SIZE);
  }
  view->race[view->current] = true;
  f->valid = true;
  return true;
}

static unsigned tile_pixel(const uint16_t *vram, unsigned address, int x, int y, int bpp) {
  unsigned a = (address + y) & 0x7fff;
  unsigned shift = 7 - x;
  unsigned bits = vram[a] >> shift;
  unsigned pixel = (bits & 1) | ((bits >> 7) & 2);
  if (bpp == 4) {
    bits = vram[(a + 8) & 0x7fff] >> shift;
    pixel |= ((bits & 1) << 2) | ((bits >> 5) & 8);
  }
  return pixel;
}

static uint16_t background_pixel(const Ppu *p, const uint16_t *vram,
                                 int layer, int x, int y, bool extend_panorama) {
  int size = PPU_bigTiles(p, layer) ? 16 : 8;
  int px = (x + p->hScroll[layer]) & 1023, py = (y + p->vScroll[layer]) & 1023;
  /* $A60C packs the skyline into overlapping 512x56 strips, selected by
   * vertical scroll ($A69F: 36,92,148,204). BG1's panorama is 896 pixels;
   * BG2's is 768 and starts at scroll 92. Only the stock 256-pixel view is
   * guaranteed valid in each strip, including the partially filled last one.
   * In the margins, address the full panorama through each strip's first
   * 256 pixels instead of wrapping X into unrelated/padded strip content.
   * Keep this local to the known world layout; HUD and guest VRAM stay intact. */
  if (extend_panorama && layer < 2 && size == 8 &&
      p->bgXsc[layer] == (layer == 0 ? 0x79 : 0x71) &&
      p->hScroll[layer] < 256 && y >= 1 && y < 52) {
    int first = layer == 0 ? 36 : 92;
    int scroll = p->vScroll[layer];
    if (scroll >= first && scroll <= 204 && (scroll - first) % 56 == 0) {
      int band = (scroll - first) / 56;
      int period = layer == 0 ? 896 : 768;
      int panorama_x = (band * 256 + p->hScroll[layer] + x) % period;
      if (panorama_x < 0) panorama_x += period;
      px = panorama_x % 256;
      py = y + first + (panorama_x / 256) * 56;
    }
  }
  int tx = px / size, ty = py / size;
  unsigned sc = p->bgXsc[layer];
  unsigned address = (sc & 0xfc) * 256 + (tx & 31) + (ty & 31) * 32;
  if ((sc & 1) && (tx & 32)) address += 1024;
  if ((sc & 2) && (ty & 32)) address += (sc & 1) ? 2048 : 1024;
  unsigned tile = vram[address & 0x7fff];
  int cx = px % size, cy = py % size;
  if (tile & 0x4000) cx = size - 1 - cx;
  if (tile & 0x8000) cy = size - 1 - cy;
  unsigned number = ((tile & 1023) + cx / 8 + (cy / 8) * 16) & 1023;
  int bpp = layer == 2 ? 2 : 4;
  unsigned base = ((p->bgTileAdr >> (layer * 4)) & 15) * 4096;
  unsigned pixel = tile_pixel(vram, base + number * (bpp * 4), cx & 7, cy & 7, bpp);
  if (!pixel) return 0;
  static const unsigned low[] = {8, 7, 1}, high[] = {12, 11, 3};
  unsigned priority = tile & 0x2000 ? high[layer] : low[layer];
  if (layer == 2 && (tile & 0x2000) && (p->bgmode & 8)) priority = 15;
  unsigned palette = ((tile >> 10) & 7) * (1u << bpp);
  return (priority << 12) | (layer << 8) | palette | pixel;
}

static bool in_window(const Ppu *p, int layer, int x, int extra) {
  unsigned flags = (p->windowsel >> (layer * 4)) & 15;
  bool enabled1 = (flags & 2) != 0, enabled2 = (flags & 8) != 0;
  int l1 = p->window1left == 0 ? -extra : p->window1left;
  int r1 = p->window1right == 255 ? 255 + extra : p->window1right;
  int l2 = p->window2left == 0 ? -extra : p->window2left;
  int r2 = p->window2right == 255 ? 255 + extra : p->window2right;
  bool a = (x >= l1 && x <= r1) != ((flags & 1) != 0);
  bool b = (x >= l2 && x <= r2) != ((flags & 4) != 0);
  if (!enabled1) return enabled2 && b;
  if (!enabled2) return a;
  switch ((p->wbgobjlog >> (layer * 2)) & 3) {
  case 0: return a || b;
  case 1: return a && b;
  case 2: return a != b;
  default: return a == b;
  }
}

static int read_i16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

/* Retail streams the Mode 7 tilemap and sizes what it streams for the stock
 * 256-pixel viewport. $03:9243 keeps exactly one 1024-by-1024-unit world
 * square uploaded, anchored at $00A8/$00AA ($00:97C3 camera minus 512 plus the
 * $0A:ED00 look-ahead, slew-clamped by $03:92AA). The tilemap is 128 by 128
 * tiles - 1024 by 1024 pixels - so that square fills it exactly and the map
 * aliases the 8192-by-4096-unit world every 1024 units: a sample outside the
 * square reads the tiles another part of the course left in the same cell.
 * A widened viewport reaches outside it, which is the reported pop-in.
 *
 * $03:939E and $03:9417 build their uploads from course tables in WRAM bank
 * $7F, which the frame snapshot already carries, so the compositor can resolve
 * the same tile for any world position instead. Guest state is never written,
 * and a sample inside the square still reads the live tilemap.
 *
 * $0020/$0022 hold the same anchor but are reused as scratch afterwards and do
 * not survive every frame; $00A8/$00AA ($03:9254, $03:925E) do. */
typedef struct FzeroCourse {
  const uint8_t *bank;  /* WRAM bank $7F. */
  unsigned grid;        /* $00B0/$00B1: the block grid, placed by $00:9F4C. */
  int anchor_x, anchor_y;
  double camera_x, camera_y;
  bool valid;
  bool complete;
} FzeroCourse;

/* Shortest-path blend of a coordinate that repeats every `period` units, the
 * rule FzeroMode7Interpolate already applies to a scanline's origin. */
static double periodic_blend(double from, double to, double alpha, double period) {
  return from + alpha * remainder(to - from, period);
}

/* The Mode 7 centre is the camera reduced to the map, but retail writes either
 * representative: on some frames it is the camera's map position and on others
 * that plus 1024. Both describe the same place and a frame's own origin and
 * centre always agree, so a texel minus its own centre is exact - but an
 * interpolated origin takes the shortest path across the seam and can land in
 * the other representative. Subtracting the wrong one moves every Mode 7
 * sample a whole map period and repaints the screen from another part of the
 * course for that one presentation. Blend the centre and the camera the same
 * periodic way so they stay in the origin's representative. */

static FzeroCourse course_open(const FzeroSourceFrame *f, bool world) {
  FzeroCourse course = {0};
  if (!world) return course;
  course.bank = f->ram + 0x10000;
  course.grid = (unsigned)f->ram[0xb0] | ((unsigned)f->ram[0xb1] << 8);
  if(!course.grid)return course; /* Race setup has not installed course tables. */
  /* $03:9346 and $03:9381 select the streamed strip with ($14 & $03F0) and
   * ($12 & $03F0), so the square starts on a 16-unit block boundary whatever
   * the anchor's low bits are. Align down, or the last block row and column
   * are classified outside and resolved twice over. */
  course.anchor_x = read_i16(f->ram + 0xa8) & 0x1ff0;
  course.anchor_y = read_i16(f->ram + 0xaa) & 0x0ff0;
  course.camera_x = read_i16(f->ram + 0xb70);
  course.camera_y = read_i16(f->ram + 0xb90);
  course.valid = true;
  return course;
}

static int course_centre(const int16_t matrix[8], int index) {
  return ((int)(matrix[index] & 0x1fff) ^ 0x1000) - 0x1000;
}

/* The full course does not repeat every 1024 texels. Only the hardware
 * camera centre does. Unwrap that centre, then interpolate the actual affine
 * displacement. Wrapping the complete origin can select a different course
 * block for one presentation during a turn, especially near the horizon. */
static FzeroMode7Line course_interpolate(FzeroMode7Line a,FzeroMode7Line b,
    double alpha,double ax,double ay,double bx,double by) {
  double t=FzeroMode7Blend(&a,&b,alpha);
  if(t>=1)return b;
  FzeroMode7Line r=b;
  r.origin_x=(periodic_blend(ax,bx,t,1024)+(a.origin_x/256-ax)*(1-t)+(b.origin_x/256-bx)*t)*256;
  r.origin_y=(periodic_blend(ay,by,t,1024)+(a.origin_y/256-ay)*(1-t)+(b.origin_y/256-by)*t)*256;
  r.step_x=a.step_x+(b.step_x-a.step_x)*t;r.step_y=a.step_y+(b.step_y-a.step_y)*t;
  return r;
}

static unsigned course_word(const FzeroCourse *course, unsigned address) {
  return (unsigned)course->bank[address & 0xffff] |
         ((unsigned)course->bank[(address + 1) & 0xffff] << 8);
}

/* Three indirections, all in bank $7F, following $03:93BF..$03:93E7 (and
 * $03:9438..$03:9463, which resolves the same tile for the column strip):
 * ($B0),Y selects a block from a 32-by-16 grid of 256-unit cells; the block id
 * times 32 picks one of sixteen 16-unit sub-rows in the $5000 pointer table;
 * that sub-row lists sixteen pointers to 2-by-2 tile groups. Each group's four
 * bytes go to $4A00/$4A80 at X and X+1, so they read (x0,y0) (x0,y1) (x1,y0)
 * (x1,y1). Every world position resolves - the grid spans the whole 8192-by-
 * 4096-unit world - so there is no void case to handle. */
static unsigned course_tile(const FzeroCourse *course, int world_x, int world_y) {
  unsigned block = course->bank[(course->grid + ((world_y >> 8) & 15) * 32 +
                                 ((world_x >> 8) & 31)) & 0xffff];
  unsigned row = course_word(course, 0x5000 + block * 32 + ((world_y >> 4) & 15) * 2);
  unsigned group = course_word(course, row + ((world_x >> 4) & 15) * 2);
  return course->bank[(group + ((world_x >> 3) & 1) * 2 +
                       ((world_y >> 3) & 1)) & 0xffff];
}

/* Decode the native character map only when an address it actually used
 * changes. Streaming scratch elsewhere in bank7F does not invalidate it.
 * The resulting 1024x512 bytes are source data, independent of HD scale. */
static const uint8_t *course_map(FzeroRenderView *view, const FzeroCourse *course) {
  bool changed = !view->course_map || view->course_grid != course->grid;
  if (!changed) for (unsigned i = 0; i < 65536; ++i)
    if (view->course_dependencies[i] && view->course_bank[i] != course->bank[i]) { changed = true; break; }
  if (!changed) return view->course_map;
  if (!view->course_map) {
    view->course_map = malloc(1024 * 512); view->course_bank = malloc(65536);
    view->course_dependencies = malloc(65536);
    if (!view->course_map || !view->course_bank || !view->course_dependencies) {
      free(view->course_map); free(view->course_bank); free(view->course_dependencies);
      view->course_map = view->course_bank = view->course_dependencies = NULL; return NULL;
    }
  }
  uint8_t *used = view->course_dependencies;
  memset(used, 0, 65536);
  for (unsigned cy = 0; cy < 512; ++cy) for (unsigned cx = 0; cx < 1024; ++cx) {
    unsigned x = cx * 8, y = cy * 8;
    unsigned cell = (course->grid + (y >> 8) * 32 + (x >> 8)) & 65535;
    unsigned block = course->bank[cell]; used[cell] = 1;
    unsigned row_address = (0x5000 + block * 32 + ((y >> 4) & 15) * 2) & 65535;
    used[row_address] = used[(row_address + 1) & 65535] = 1;
    unsigned row = course_word(course, row_address);
    unsigned group_address = (row + ((x >> 4) & 15) * 2) & 65535;
    used[group_address] = used[(group_address + 1) & 65535] = 1;
    unsigned group = course_word(course, group_address);
    unsigned tile = (group + ((x >> 3) & 1) * 2 + ((y >> 3) & 1)) & 65535;
    used[tile] = 1; view->course_map[cy * 1024 + cx] = course->bank[tile];
  }
  memcpy(view->course_bank, course->bank, 65536); view->course_grid = course->grid;
  return view->course_map;
}

/* Tile number for one Mode 7 texel, or -1 to keep the live tilemap. The
 * transform is centred on the camera, so the texel's offset from the 13-bit
 * centre is exact even where the wrapped coordinate is ambiguous. */
/* Neighbouring samples on a scanline share an eight-unit cell - hundreds of
 * them in the near field, where a pixel advances a third of a unit - and the
 * tile depends on nothing finer, so one entry retires the three dependent
 * loads for every sample after the first in each cell. */
typedef struct FzeroCourseCache { int cell_x, cell_y, tile; } FzeroCourseCache;

static const FzeroCourseCache kCourseCacheEmpty = {-1, -1, -1};

/* What one scanline measures its texels against. The per-line blend keeps the
 * texel and the centre in the same frame and the camera anchors them to the
 * world; a texel is already whole, so the whole conversion collapses to one
 * integer offset per scanline instead of two floors per sample. */
typedef struct FzeroCourseLine { int offset_x, offset_y; } FzeroCourseLine;

static FzeroCourseLine course_line(int camera_x, int camera_y,
                                   int centre_x, int centre_y) {
  return (FzeroCourseLine){camera_x-centre_x,camera_y-centre_y};
}

static int course_blended_offset(int old_camera,int camera,int old_centre,int centre,
                                 unsigned period,double blend) {
  /* Interpolate the relative reference, not two large floating coordinates
   * followed by subtraction. For example 1154 -> 1155 versus 130 -> 131
   * must stay exactly 1024: at alpha .004 the separate blends subtract to
   * 1023.9999999999999 and floor selects the neighbouring course tile. */
  double delta=remainder(camera-old_camera,period)-remainder(centre-old_centre,1024);
  return (int)floor((old_camera-old_centre)+delta*blend);
}

static int course_sample_at(const FzeroCourse *course, const FzeroCourseLine *line,
                            FzeroCourseCache *cache, int tx, int ty) {
  if (!course->valid) return -1;
  int world_x = (tx + line->offset_x) & 0x1fff;
  int world_y = (ty + line->offset_y) & 0x0fff;
  if (!course->complete && ((world_x - course->anchor_x) & 0x1fff) < 1024 &&
      ((world_y - course->anchor_y) & 0x0fff) < 1024) return -1;
  int cell_x = world_x >> 3, cell_y = world_y >> 3;
  if (cell_x != cache->cell_x || cell_y != cache->cell_y) {
    cache->cell_x = cell_x;
    cache->cell_y = cell_y;
    cache->tile = (int)course_tile(course, world_x, world_y);
  }
  return cache->tile;
}
static int course_sample(const FzeroCourse *course, const FzeroCourseLine *line,
                         FzeroCourseCache *cache, FzeroMode7Texel texel) {
  if (!(fabs(texel.x) < 1e6 && fabs(texel.y) < 1e6)) return -1;
  return course_sample_at(course, line, cache, (int)texel.x, (int)texel.y);
}

/* The guest's affine rows are bounded. Prove that once at both endpoints,
 * then keep whole texels as integers through course/tile lookup. Unusual
 * diagnostic transforms retain the general sampler and its finite guards. */
static bool bounded_line(const FzeroMode7Line *line, FzeroViewport viewport) {
  double left = -viewport.extra, right = viewport.width - viewport.extra;
  return SnesMode7HdSpanFits(
      (line->origin_x + left * line->step_x) / 256,
      (line->origin_y + left * line->step_y) / 256,
      (line->origin_x + right * line->step_x) / 256,
      (line->origin_y + right * line->step_y) / 256, 999999);
}

/* $0081DE DMA-orders six 32-byte vehicle reservations using $0AC0..$0ACA.
 * Resolve the reservation, not screen proximity: nearby cars may overlap or
 * swap drawing order. $F468 records the used opponent tiles at $11D0+2*car. */
static int object_owner(const FzeroSourceFrame *f, int slot) {
  if (!f->ram[0x50]) return -1;
  int car = -1;
  if (slot >= 68 && slot < 116) {
    int source = read_i16(f->ram + 0xac0 + ((slot - 68) / 8) * 2);
    if (source >= 0x300 && source <= 0x3a0 && !(source & 31))
      car = (source - 0x300) / 32;
  } else if (slot >= 116) {
    /* $C339 alternates odd/even opponent shadows. NMI increments $51
     * after the source was built, so this snapshot contains the next parity. */
    car = 1 + ((f->ram[0x51] ^ 1) & 1) + ((slot - 116) / 4) * 2;
  }
  if (car < 0 || car >= 6 || (f->ram[0xb00 + car * 2] & 0x88) != 0x88) return -1;
  return car;
}

static int object_x(const FzeroSourceFrame *f, int raw_x, FzeroViewport viewport, int owner) {
  if (owner >= 0 && viewport.enhanced) {
    int cx = read_i16(f->ram + 0xc50 + owner * 2);
    return raw_x + 512 * (int)round((cx - raw_x) / 512.0);
  }
  return raw_x >= 256 ? raw_x - 512 : raw_x;
}

static void sprites(const Ppu *p, const FzeroSourceFrame *frame,
                    const FzeroSourceFrame *previous, double alpha,
                    int y, FzeroViewport viewport, bool race_hud, bool results,
                    uint16_t *pixels) {
  const FzeroRasterLine *line = &frame->lines[y];
  const uint16_t *vram = frame->vram;
  /* The completed-league recap runs inside the live race ($C3=$11).
   * $039A32 uses $0975 to animate its table in the former HUD reservations.
   * Cars and the BG power/score HUD remain live; only these OBJ slots change
   * ownership. The separate scene-3 results screen is not involved. */
  bool lap_review = frame->ram[0x54] == 2 && frame->ram[0xc3] == 0x11 &&
                    frame->ram[0x975] != 0;
  static const int sizes[8][2] = {{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
  memset(pixels, 0, (size_t)viewport.width * sizeof(*pixels));
  for (int slot = 127; slot >= 0; --slot) {
    unsigned position = line->oam[slot * 2], attr = line->oam[slot * 2 + 1];
    unsigned high = line->high_oam[slot / 4] >> ((slot % 4) * 2);
    int size = sizes[p->obsel >> 5][(high >> 1) & 1];
    int sprite_y = position >> 8;
    int raw_x = (position & 255) | ((high & 1) << 8);
    if (raw_x == 384 && sprite_y == 128) continue;
    /* $B164 draws the intro's spare-machine icon/count in temporary slots
     * 126/127 ($03F8/$03FC); setup later transfers them to HUD slots 22/23.
     * They already belong to the right edge while the course name is centered. */
    bool intro_counter = !race_hud && frame->ram[0x58] == 0 &&
        (results || frame->ram[0x55] <= 2) && slot >= 126;
    /* Results reuse the former car reservations for text and numbers. The
     * frozen race's actor/DMA tables no longer describe those sprites. */
    int owner = intro_counter || results ? -1 : object_owner(frame, slot);
    /* The screen-locked player and unowned effects use offscreen X as a
     * hiding mechanism, sometimes retaining Y and stale tile attributes.
     * Only verified opponent reservations can reveal those signed positions. */
    if (viewport.enhanced && raw_x >= 256 && owner <= 0) continue;
    if (viewport.enhanced && owner == 0 && attr == 0) continue;
    if (viewport.enhanced && slot >= 68 && slot < 116 && owner > 0 &&
        (slot - 68) % 8 >= frame->ram[0x11d0 + owner * 2]) continue;
    int x = object_x(frame, raw_x, viewport, owner);
    /* Match a live car and the same tile reservation before interpolating.
     * HUD values, births/deaths, reused slots and sprite animation changes
     * remain discrete. Guest positions are never written by this pass. */
    if (previous && owner >= 0 && alpha < 1 &&
        !memcmp(previous->ram + 0xb00 + owner * 2, frame->ram + 0xb00 + owner * 2, 2)) {
      const FzeroRasterLine *old = &previous->lines[y];
      int old_slot = slot;
      if (slot >= 68 && slot < 116) {
        old_slot = -1;
        for (int block = 68; block < 116; block += 8)
          if (object_owner(previous, block) == owner) { old_slot = block + (slot - 68) % 8; break; }
      }
      if (old_slot >= 0) {
      unsigned old_position = old->oam[old_slot * 2];
      unsigned old_high = old->high_oam[old_slot / 4] >> ((old_slot % 4) * 2);
      int old_y = old_position >> 8, old_owner = object_owner(previous, old_slot);
      int old_x = object_x(previous, (old_position & 255) | ((old_high & 1) << 8), viewport, old_owner);
      if (old->oam[old_slot * 2 + 1] == attr && old_owner == owner &&
          ((old_high ^ high) & 2) == 0 && abs(old_x - x) <= 32 && abs(old_y - sprite_y) <= 24) {
        x = (int)round(old_x + alpha * (x - old_x));
        sprite_y = (int)round(old_y + alpha * (sprite_y - old_y));
      }
      }
    }
    int row = (y - sprite_y) & 255;
    if (row >= size) continue;
    /* Map/markers 20..31, timer/boosts 32..46, rank 48..51. Slot 47 is
     * NOT HUD: $00:BED7..BF5E writes the player's skid/collision spark at
     * $02BC using the vehicle's $0C70/$0C80 position. Moving it to the right
     * edge detaches it from the car whenever the effect appears (#4).
     * $EDB3/$EE93 reuse 48..63 for explosion/smoke pieces. Only anchor
     * 48..51 when they contain the rank digits ($180..$189/$190..$199,
     * written by $A8B1), not merely because they occupy rank's slots. */
    unsigned tile_number = attr & 0x1ff;
    bool rank_digit = slot >= 48 && slot < 52 &&
        (tile_number & 0x1e0) == 0x180 && (tile_number & 15) <= 9;
    if (race_hud && !lap_review && ((slot >= 20 && slot < 47) || rank_digit)) {
      if (x < 0 || x >= 256) continue;
      x += (slot < 22 || (slot >= 24 && slot < 32) || slot >= 48) ?
          -viewport.extra : viewport.extra;
    }
    if (intro_counter) x += viewport.extra;
    x += viewport.extra;
    if (attr & 0x8000) row = size - 1 - row;
    unsigned base = (p->obsel & 7) << 13;
    if (attr & 0x100) base += (((p->obsel & 0x18) + 8) << 9);
    unsigned palette = 128 + ((attr >> 9) & 7) * 16;
    unsigned priority = ((attr >> 12) & 3) * 4 + 2;
    unsigned layer = attr & 0x800 ? 4 : 6; /* OBJ palettes 0..3 bypass math. */
    for (int col = 0; col < size; ++col) {
      int dest = x + col;
      if (dest < 0 || dest >= viewport.width) continue;
      int cx = attr & 0x4000 ? size - 1 - col : col;
      unsigned tile = ((((attr & 255) >> 4) + row / 8) << 4) |
                       (((attr & 15) + cx / 8) & 15);
      unsigned pixel = tile_pixel(vram, base + tile * 16, cx & 7, row & 7, 4);
      if (pixel) pixels[dest] = (priority << 12) | (layer << 8) | palette | pixel;
    }
  }
}

static bool window_condition(unsigned mode, bool inside) {
  return mode == 3 || (mode == 1 && !inside) || (mode == 2 && inside);
}

static void race_color_math(Ppu *p) {
  /* VS owns its actor shadows. Remove both native shadow-window math and
   * full-width HDMA haze: leaving either behind creates hard dark seams or
   * horizontal color bands on the full-course track. */
  p->cgadsub=0;p->windowsel=0;p->cgwsel&=15;
}

static uint32_t colour(const Ppu *p, const uint16_t *palette, uint16_t main,
                       uint16_t sub, bool inside) {
  unsigned rgb = palette[main & 255], layer = (main >> 8) & 15;
  bool clipped = window_condition(p->cgwsel >> 6, inside);
  bool math = !window_condition((p->cgwsel >> 4) & 3, inside) &&
              ((p->cgadsub & 63) & (1u << layer));
  unsigned other = p->fixedColor;
  bool half = math && (p->cgadsub & 64) && !clipped;
  if (math && (p->cgwsel & 2)) {
    if ((sub & 255) != 0) other = palette[sub & 255];
    else half = false;
  }
  uint32_t result = 0;
  for (int component = 0; component < 3; ++component) {
    int c = clipped ? 0 : (rgb >> (component * 5)) & 31;
    if (math) {
      int second = (other >> (component * 5)) & 31;
      c += p->cgadsub & 128 ? -second : second;
      if (c < 0) c = 0;
      if (half) c /= 2;
      if (c > 31) c = 31;
    }
    c = ((c << 3) | (c >> 2)) * (p->inidisp & 15) / 15;
    result |= (uint32_t)c << (16 - component * 8);
  }
  return result;
}

static FzeroMode7Line hd_transform(const FzeroSourceFrame *frame, int y) {
  const uint8_t *registers = frame->lines[y].registers;
  int16_t matrix[8];
  memcpy(matrix, registers + offsetof(Ppu, m7matrix), sizeof(matrix));
  SnesMode7HdTransform t = SnesMode7HdMakeTransform(
      matrix, registers[offsetof(Ppu, m7sel)], (unsigned)y + 1);
  return (FzeroMode7Line){t.origin_x * 256, t.origin_y * 256,
                         t.step_x * 256, t.step_y * 256, t.control};
}

static FzeroMode7Line hd_frame_transform(const FzeroSourceFrame *frame,
                                         const FzeroSourceFrame *previous,
                                         int y, double alpha, bool interpolate,bool complete_course) {
  FzeroMode7Line t = hd_transform(frame, y);
  if (interpolate && alpha < 1 &&
      (previous->lines[y].registers[offsetof(Ppu, bgmode)] & 7) == 7)
    {
      int16_t before[8],after[8];
      memcpy(before,previous->lines[y].registers+offsetof(Ppu,m7matrix),sizeof(before));
      memcpy(after,frame->lines[y].registers+offsetof(Ppu,m7matrix),sizeof(after));
      t=complete_course?course_interpolate(hd_transform(previous,y),t,alpha,
          course_centre(before,4),course_centre(before,5),course_centre(after,4),course_centre(after,5)):
          FzeroMode7Interpolate(hd_transform(previous,y),t,alpha);
    }
  return t;
}

static void expand_line(uint32_t *out, const uint32_t *row,
                        int y, int width, unsigned scale) {
  uint32_t *first = out + (size_t)y * scale * width * scale;
  for (int x = 0; x < width; ++x)
    for (unsigned sx = 0; sx < scale; ++sx) first[x * scale + sx] = row[x];
  for (unsigned sy = 1; sy < scale; ++sy)
    memcpy(first + (size_t)sy * width * scale, first, (size_t)width * scale * sizeof(*out));
}

typedef struct FzeroHdPixelContext {
  const uint32_t *colors;
  uint16_t objects[2];
  uint8_t flags; /* BG enabled on main/sub, then colour-window membership. */
} FzeroHdPixelContext;

typedef struct FzeroRenderBatch {
  const FzeroSourceFrame *frame, *previous;
  uint32_t *out, *native;
  Ppu *scratch;
  FzeroViewport viewport;
  FzeroCourse course;
  double alpha, sample_offset[FZERO_HD_SCALE_MAX];
  unsigned scale;
  bool scenery, results, result_scenery, world, race_hud, interpolate;
  SnesHdFrame *jobs;
  int picture, tilemap;
  bool jobs_ok;
} FzeroRenderBatch;

static bool publish_native(SnesHdFrame *jobs, unsigned y, const uint32_t *row, unsigned width) {
  SnesHdLine j = {0}; j.kind = kSnesHdNative;
  memcpy(j.native, row, width * sizeof(*row));
  return SnesHdFrameSetLine(jobs, y, &j);
}

static void render_lines(void *opaque, size_t begin, size_t end, unsigned slot) {
  FzeroRenderBatch *batch = opaque;
  const FzeroSourceFrame *f = batch->frame, *previous = batch->previous;
  uint32_t *out = batch->out, *native = batch->native;
  Ppu *scanout = &batch->scratch[slot];
  FzeroViewport viewport = batch->viewport;
  const FzeroCourse course = batch->course;
  double alpha = batch->alpha;
  const double *sample_offset = batch->sample_offset;
  unsigned scale = batch->scale;
  bool scenery = batch->scenery, results = batch->results;
  bool result_scenery = batch->result_scenery, world = batch->world;
  bool race_hud = batch->race_hud, interpolate = batch->interpolate;
  uint16_t object_pixels[FZERO_MAX_WIDTH];
  uint32_t row[FZERO_MAX_WIDTH];
  for (int y = (int)begin; y < (int)end; ++y) {
    const FzeroRasterLine *l = &f->lines[y];
    memcpy(scanout, l->registers, PPU_SAVESTATE_REGS_SIZE);
    if (scanout->inidisp & 128) {
      if (batch->jobs && !publish_native(batch->jobs, y, out + (size_t)y * viewport.width, viewport.width)) batch->jobs_ok = false;
      continue;
    }
    int mode = scanout->bgmode & 7;
    if(mode==7 && (world || result_scenery) && !(scanout->cgadsub&128)) {
      bool full_row=true;
      const int points[]={0,255,scanout->window1left,scanout->window1right+1,
                          scanout->window2left,scanout->window2right+1};
      for(unsigned i=0;i<sizeof(points)/sizeof(*points);++i)if(points[i]<256 &&
          window_condition((scanout->cgwsel>>4)&3,in_window(scanout,5,points[i],0)))full_row=false;
      if(full_row)scanout->cgadsub&=~0x23u; /* Keep localized shadow/HUD math. */
    }
    if (!scenery || (mode != 1 && mode != 7)) {
      /* Flat selection/loading screens keep their original centered artwork,
       * but their backdrop, fades and colour windows cover the full viewport. */
      for (int sx = 0; sx < viewport.width; ++sx)
        row[sx] = colour(scanout, l->palette, 0x500, 0x500,
            in_window(scanout, 5, sx - viewport.extra, viewport.extra));
      memcpy(row + viewport.extra, f->stock + y * 256, 256 * sizeof(*out));
      expand_line(out, row, y, viewport.width, scale);
      if (native) memcpy(native + y * viewport.width, row, viewport.width * sizeof(*native));
      if (batch->jobs && !publish_native(batch->jobs, y, row, viewport.width)) batch->jobs_ok = false;
      continue;
    }
    FzeroMode7Line transform = FzeroMode7Transform(scanout->m7matrix, scanout->m7sel, y + 1);
    int camera_x = course.camera_x, camera_y = course.camera_y;
    int centre_x = course_centre(scanout->m7matrix, 4);
    int centre_y = course_centre(scanout->m7matrix, 5);
    FzeroCourseLine reference = course_line(camera_x, camera_y, centre_x, centre_y);
    FzeroCourseCache cache = kCourseCacheEmpty;
    if (mode == 7 && interpolate && alpha < 1) {
      Ppu old;
      memcpy(&old, previous->lines[y].registers, PPU_SAVESTATE_REGS_SIZE);
      if ((old.bgmode & 7) == 7) {
        FzeroMode7Line before = FzeroMode7Transform(old.m7matrix, old.m7sel, y + 1);
        /* FzeroMode7Interpolate keeps the current scanline whenever it cannot
         * blend, so the centre and the camera must follow the same decision:
         * measuring a blended texel against an unblended centre, or the other
         * way round, moves every sample a whole map period. */
        double blend = FzeroMode7Blend(&before, &transform, alpha);
        transform = course.valid?course_interpolate(before,transform,alpha,
            course_centre(old.m7matrix,4),course_centre(old.m7matrix,5),centre_x,centre_y):
            FzeroMode7Interpolate(before, transform, alpha);
        if (blend < 1) {
          reference.offset_x=course_blended_offset(read_i16(previous->ram+0xb70),camera_x,
              course_centre(old.m7matrix,4),centre_x,8192,blend);
          reference.offset_y=course_blended_offset(read_i16(previous->ram+0xb90),camera_y,
              course_centre(old.m7matrix,5),centre_y,4096,blend);
        }
      }
    }
    if (world || results)
      sprites(scanout, f, interpolate ? previous : NULL, alpha, y, viewport,
              race_hud, results, object_pixels);
    else
      memset(object_pixels, 0, (size_t)viewport.width * sizeof(*object_pixels));
    /* Successful results retain the frozen course. Its spatial sampling
     * stays HD even though actor/camera interpolation has stopped. */
    bool hd_line = (scale > 1 || batch->jobs) && (world || result_scenery) && mode == 7 &&
        !((scanout->mosaic & 1) && (scanout->mosaic >> 4)) &&
        !(scanout->setini & 0x49) && !(scanout->cgwsel & 1);
    uint32_t colors[8][256];
    unsigned colors_ready=0;
    if (!hd_line || native || batch->jobs) {
      for (int sx = 0; sx < viewport.width; ++sx) {
        int x = sx - viewport.extra;
        uint16_t screens[2] = {0x500, 0x500};
        for (int sub = 0; sub < 2; ++sub) {
          for (int layer = 0; layer < (mode == 7 ? 1 : 3); ++layer) {
            if (!(scanout->screenEnabled[sub] & (1u << layer))) continue;
            if ((scanout->screenWindowed[sub] & (1u << layer)) && in_window(scanout, layer, x, viewport.extra)) continue;
            uint16_t pixel;
            if (mode == 7) {
              FzeroMode7Texel texel = FzeroMode7Locate(&transform, x);
              unsigned index = FzeroMode7Fetch(&transform, f->vram, texel,
                  course_sample(&course, &reference, &cache, texel));
              pixel = index ? 0x5000 | index : 0;
            } else {
              int bx = x;
              if (layer == 2 && results && y < 32 && sx < 128) {
                bx = sx; /* Top-left score; lap table/menu stays centered. */
              } else if (layer == 2 && !race_hud) {
                if (x < 0 || x >= 256 || (results && y < 32 && x < 128)) continue;
              }
              if (layer == 2 && race_hud) {
                bx = sx < viewport.width / 2 ? sx : sx - 2 * viewport.extra;
                if ((sx < viewport.width / 2 && bx >= 128) ||
                    (sx >= viewport.width / 2 && bx < 128)) continue;
              }
              pixel = background_pixel(scanout, f->vram, layer, bx, y + 1,
                                       viewport.enhanced && (x < 0 || x >= 256));
            }
            if (pixel > screens[sub]) screens[sub] = pixel;
          }
          if ((scanout->screenEnabled[sub] & 16) &&
              (!(scanout->screenWindowed[sub] & 16) || !in_window(scanout, 4, x, viewport.extra)) &&
              object_pixels[sx] > screens[sub]) screens[sub] = object_pixels[sx];
        }
        /* The power meter is filled by the colour window, not a BG tile.
         * Its HDMA band must travel with the right-anchored BG3 outline. */
        /* Loss keeps its score at the left edge, but its collapsed colour
         * window must not expand into the former race meter/HDMA panel. */
        /* A successful finish draws its large placing numeral through the
         * colour window over the frozen course. Keep that native graphic
         * centered; only the collapsed black-results window is edge based. */
        bool black_results = results && !result_scenery;
        int colour_x = black_results ? sx : x;
        if (race_hud && mode == 1 && y >= 19 && y <= 27 &&
            scanout->window1left >= 176 && scanout->window1right <= 239)
          colour_x -= viewport.extra;
        row[sx] = colour(scanout, l->palette, screens[0], screens[1],
            in_window(scanout, 5, colour_x, black_results ? 0 : viewport.extra));
      }
      /* Preserve the meter's composed fill, including its fixed-colour HDMA,
       * without letting a different section of skyline show through it. */
      if (race_hud && viewport.enhanced && mode == 1 && y >= 19 && y <= 27)
        memcpy(row + 176 + 2 * viewport.extra,
               f->stock + y * 256 + 176, 64 * sizeof(*out));
      /* Title/menu graphics remain an exact centered group. Only their live
       * scenery expands; hidden/reused OBJ reservations cannot leak into it. */
      if (!world && !results)
        memcpy(row + viewport.extra,
               f->stock + y * 256, 256 * sizeof(*out));
      if (native) memcpy(native + y * viewport.width, row, viewport.width * sizeof(*native));
      if (!hd_line || batch->jobs) expand_line(out, row, y, viewport.width, scale);
    }
    if (!hd_line) {
      if (batch->jobs && !publish_native(batch->jobs, y, row, viewport.width)) batch->jobs_ok = false;
      continue;
    }

    /* Window membership and sprite priority are native-pixel properties.
     * Resolve them once for all subpixels. In columns without sprites, the
     * colour math depends only on the sampled palette index: cache that
     * mapping per scanline/window combination, including transparent zero.
     * Sprite columns retain the full main/subscreen colour calculation. */
    FzeroHdPixelContext contexts[FZERO_MAX_WIDTH];
    for (int sx = 0; sx < viewport.width; ++sx) {
      int x = sx - viewport.extra;
      FzeroHdPixelContext *c = &contexts[sx];
      c->flags = in_window(scanout, 5, x, viewport.extra) ? 4 : 0;
      for (int sub = 0; sub < 2; ++sub) {
        if ((scanout->screenEnabled[sub] & 1) &&
            (!(scanout->screenWindowed[sub] & 1) ||
             !in_window(scanout, 0, x, viewport.extra))) c->flags |= 1u << sub;
        c->objects[sub] = (scanout->screenEnabled[sub] & 16) &&
            (!(scanout->screenWindowed[sub] & 16) ||
             !in_window(scanout, 4, x, viewport.extra)) ? object_pixels[sx] : 0;
      }
      c->colors = NULL;
      if (!(c->objects[0] | c->objects[1])) {
        unsigned key = c->flags;
        if (!(colors_ready & (1u << key))) {
          for (unsigned index = 0; index < 256; ++index)
            colors[key][index] = colour(scanout, l->palette,
                (key & 1) && index ? (uint16_t)(0x5000 | index) : 0x500,
                (key & 2) && index ? (uint16_t)(0x5000 | index) : 0x500,
                (key & 4) != 0);
          colors_ready |= 1u << key;
        }
        c->colors = colors[key];
      }
    }
    FzeroMode7Line hd = hd_frame_transform(f, previous, y, alpha, interpolate,course.valid);
    FzeroMode7Line next = hd;
    bool adjacent = false;
    if (y + 1 < 224) {
      const uint8_t *next_registers = f->lines[y + 1].registers;
      /* Smooth only the same camera's contiguous Mode 7 band. HUD, IRQ
       * splits, flips, fades and changes of coordinate origin are boundaries. */
      adjacent = (next_registers[offsetof(Ppu, bgmode)] & 7) == 7 &&
          next_registers[offsetof(Ppu, m7sel)] == scanout->m7sel &&
          next_registers[offsetof(Ppu, inidisp)] == scanout->inidisp &&
          next_registers[offsetof(Ppu, setini)] == scanout->setini &&
          next_registers[offsetof(Ppu, mosaic)] == scanout->mosaic &&
          !memcmp(next_registers + offsetof(Ppu, m7matrix) + 8, scanout->m7matrix + 4, 8);
      if (adjacent) next = hd_frame_transform(f, previous, y + 1, alpha, interpolate,course.valid);
    }
    SnesMode7HdTransform affine = SnesMode7HdMakeTransform(
        scanout->m7matrix, scanout->m7sel, (unsigned)y + 1);
    if (batch->jobs) {
      SnesHdLine j = {0}; j.kind = kSnesHdAffine;
      j.source = (uint16_t)batch->picture;
      j.tilemap = batch->tilemap < 0 ? 0 : (uint16_t)(batch->tilemap + 1);
      j.map_offset_x = reference.offset_x; j.map_offset_y = reference.offset_y;
      j.affine[0] = hd.origin_x; j.affine[1] = hd.origin_y;
      j.affine[2] = hd.step_x; j.affine[3] = hd.step_y;
      j.affine[4] = adjacent ? next.origin_x - hd.origin_x : affine.row_x * 256;
      j.affine[5] = adjacent ? next.origin_y - hd.origin_y : affine.row_y * 256;
      j.affine[6] = adjacent ? next.step_x - hd.step_x : 0;
      j.affine[7] = adjacent ? next.step_y - hd.step_y : 0;
      j.screen_left = -viewport.extra; j.active_left = -viewport.extra;
      j.active_right = 256 + viewport.extra; j.control = hd.control;
      j.fixed_color = scanout->fixedColor; j.brightness = scanout->inidisp & 15;
      j.cgwsel = scanout->cgwsel; j.cgadsub = scanout->cgadsub;
      memcpy(j.palette, l->palette, sizeof(j.palette));
      memcpy(j.native, row, viewport.width * sizeof(*row));
      for (int sx = 0; sx < viewport.width; ++sx) {
        const FzeroHdPixelContext *c = &contexts[sx];
        j.object[sx] = object_pixels[sx]; j.object_color[sx] = l->palette[object_pixels[sx] & 255];
        j.visibility[sx] = (c->flags & 3) | ((c->flags & 4) << 2) |
            (c->objects[0] ? 4 : 0) | (c->objects[1] ? 8 : 0);
      }
      if (!SnesHdFrameSetLine(batch->jobs, y, &j)) batch->jobs_ok = false;
      continue;
    }
    for (unsigned sy = 0; sy < scale; ++sy) {
      double fraction = (double)sy / scale;
      FzeroMode7Line subline = hd;
      if (adjacent) subline = course_interpolate(hd,next,fraction,0,0,0,0);
      else {
        subline.origin_x += affine.row_x * fraction * 256;
        subline.origin_y += affine.row_y * fraction * 256;
      }
      uint32_t *destination = out + ((size_t)y * scale + sy) * viewport.width * scale;
      bool bounded = bounded_line(&subline, viewport);
      FzeroMode7Texel last = {NAN, NAN};
      int last_x = 1000000, last_y = 1000000;
      unsigned index = 0;
      for (int sx = 0; sx < viewport.width; ++sx) {
        int x = sx - viewport.extra;
        const FzeroHdPixelContext *c = &contexts[sx];
        for (unsigned sample = 0; sample < scale; ++sample) {
          double position = x + sample_offset[sample];
          /* Near-camera HD samples often hit the same source texel. The
           * immutable texture/course lookup is independent of screen masks
           * and sprites, so reuse its index across those column boundaries. */
          if (bounded) {
            int tx = SnesMode7HdFloorInt((subline.origin_x + position * subline.step_x) / 256);
            int ty = SnesMode7HdFloorInt((subline.origin_y + position * subline.step_y) / 256);
            if (tx != last_x || ty != last_y) {
              index = SnesMode7HdFetchInt(subline.control, f->vram, tx, ty,
                  course_sample_at(&course, &reference, &cache, tx, ty));
              last_x = tx; last_y = ty;
            }
          } else {
            FzeroMode7Texel texel = FzeroMode7Locate(&subline, position);
            if (texel.x != last.x || texel.y != last.y) {
              index = FzeroMode7Fetch(&subline, f->vram, texel,
                  course_sample(&course, &reference, &cache, texel));
              last = texel;
            }
          }
          if (c->colors) {
            destination[sx * scale + sample] = c->colors[index];
          } else {
            uint16_t screens[2] = {0x500, 0x500};
            for (int sub = 0; sub < 2; ++sub) {
              if ((c->flags & (1u << sub)) && index) screens[sub] = (uint16_t)(0x5000 | index);
              if (c->objects[sub] > screens[sub]) screens[sub] = c->objects[sub];
            }
            destination[sx * scale + sample] = colour(scanout, l->palette,
                screens[0], screens[1], (c->flags & 4) != 0);
          }
        }
      }
    }
  }
}

static bool render_frame(FzeroRenderView *view, uint32_t *out, FzeroViewport viewport, double alpha,
                          unsigned scale, uint32_t *native, SnesHdFrame *jobs) {
  if (!view) return false;
  const FzeroSourceFrame *f = &view->frames[view->current];
  const FzeroSourceFrame *previous = &view->frames[view->current ^ 1];
  Ppu *scanout = &view->scanout;
  if (!f->valid || !out || viewport.width < 256 || viewport.width > FZERO_MAX_WIDTH ||
      viewport.width != 256 + 2 * viewport.extra) return false;
  memset(out, 0, (size_t)viewport.width * 224 * scale * scale * sizeof(*out));
  if (native) memset(native, 0, (size_t)viewport.width * 224 * sizeof(*native));
  /* $81 selects live track scenery on the title screen as well as in races.
   * Scene $54=2 additionally owns vehicle identity and adaptive race HUD. */
  bool scenery = f->ram[0x81] != 0;
  /* Scene 3 also contains exits from a live race. The native result setup
   * ($03:99C5) sets $5F bit 7 for both successful and failed races. Successful
   * results keep the frozen track visible ($97); losses use a black backdrop
   * ($94). Testing only the background mask misclassified a successful
   * result's phase-5 fade as a race HUD and split the lap/rank digits. */
  memcpy(scanout, f->lines[100].registers, PPU_SAVESTATE_REGS_SIZE);
  bool results = scenery && f->ram[0x54] == 3 &&
      ((f->ram[0x5f] & 0x80) || !(scanout->screenEnabled[0] & 3));
  bool result_scenery = results && (scanout->screenEnabled[0] & 3);
  bool race_exit = f->ram[0x54] == 3 &&
      !results && (f->ram[0x55] == 4 || f->ram[0x55] == 5);
  bool world = scenery && (f->ram[0x54] == 2 || race_exit);
  /* Phase 6 is still the live YOU LOST view in both GP and Training; its
   * timer, power fill and spare machines all retain the race HUD layout. */
  /* $8ACD installs the race HUD before $8B11 advances setup substate $56.
   * Setup phase $55=2 then displays it while waiting to enter active phase 3.
   * Anchor tiles, sprites and the power meter as soon as that HUD is ready;
   * the preceding course-title/setup phase still uses centered reservations. */
  bool race_hud = world && (f->ram[0x55] >= 3 ||
                            (f->ram[0x55] == 2 && f->ram[0x56] != 0));
  bool interpolate = world && previous->valid && previous->frame + 1 == f->frame &&
      !memcmp(previous->ram + 0x54, f->ram + 0x54, 3) &&
      previous->ram[0x81] == f->ram[0x81];
  if (interpolate) {
    int angle_change = abs((int)previous->ram[0xac] - f->ram[0xac]);
    if (angle_change > 96) angle_change = 192 - angle_change;
    if (angle_change > 16 ||
        abs((int)remainder(read_i16(previous->ram + 0xb70) - read_i16(f->ram + 0xb70), 8192)) > 128 ||
        abs((int)remainder(read_i16(previous->ram + 0xb90) - read_i16(f->ram + 0xb90), 4096)) > 128)
      interpolate = false;
  }
  /* A widened viewport can reach outside retail's streamed square. Both a
   * live race and its frozen finish backdrop retain the course tables. */
  bool race = view->race[view->current];
  if (race) race_hud = false;
  FzeroCourse course = course_open(f, race || world || result_scenery);
  /* Resolve the full course for every race/aspect. The guest's 1024-unit
   * VRAM streaming square is a cache, never the maximum visible distance. */
  course.complete = race || world || result_scenery;
  int picture = -1, tilemap = -1;
  if (jobs) {
    picture = SnesHdFrameAddPicture(jobs, f->vram);
    if (picture < 0) return false;
    if (course.valid) {
      const uint8_t *map = course_map(view, &course);
      if (!map || (tilemap = SnesHdFrameAddTilemap(jobs, map, 1024, 512)) < 0) return false;
    }
  }
  /* Only substantial HD scenery uses workers. Native composition and flat
   * menus stay serial. The call joins before another source frame, state load,
   * rewind action or shutdown can replace the immutable snapshot. */
  SnesRenderWorkers *pool = NULL;
  if (scale > 1 && (world || result_scenery)) {
    prepare_workers(view);
    pool = view->render_workers;
  }
  FzeroRenderBatch batch = {
    .frame = f, .previous = previous, .out = out, .native = native,
    .scratch = pool ? view->worker_scratch : scanout,
    .viewport = viewport, .course = course, .alpha = alpha, .scale = scale,
    .scenery = scenery, .results = results, .result_scenery = result_scenery,
    .world = world, .race_hud = race_hud, .interpolate = interpolate,
    .jobs = jobs, .picture = picture, .tilemap = tilemap, .jobs_ok = true
  };
  for (unsigned sample = 0; sample < scale; ++sample)
    batch.sample_offset[sample] = (double)sample / scale;
  return SnesRenderWorkersRun(pool, 224, 8, render_lines, &batch) && batch.jobs_ok;
}

bool FzeroRenderViewDraw(FzeroRenderView *view, uint32_t *out, FzeroViewport viewport, double alpha) {
  return render_frame(view, out, viewport, alpha, 1, NULL, NULL);
}

bool FzeroRendererDrawHd(uint32_t *out, size_t capacity,
                         FzeroViewport viewport, double alpha, unsigned scale) {
  return FzeroRendererDrawPresentation(NULL, out, capacity, viewport, alpha, scale);
}

bool FzeroRenderViewDrawPresentation(FzeroRenderView *view, uint32_t *native, uint32_t *out, size_t capacity,
                                   FzeroViewport viewport, double alpha, unsigned scale) {
  if (!FzeroValidHdScale(scale) || viewport.width < 256 ||
      viewport.width > FZERO_MAX_WIDTH ||
      capacity < (size_t)viewport.width * 224 * scale * scale) return false;
  return render_frame(view, out, viewport, alpha, scale, native, NULL);
}

SnesHdFrame *FzeroRenderViewPrepareHd(FzeroRenderView *view, uint32_t *native,
                                     FzeroViewport viewport, double alpha) {
  if (!native) return NULL;
  SnesHdFrame *jobs = SnesHdFrameCreate(viewport.width, 224);
  if (jobs && render_frame(view, native, viewport, alpha, 1, NULL, jobs)) return jobs;
  SnesHdFrameDestroy(jobs); return NULL;
}
SnesHdFrame *FzeroRendererPrepareHd(uint32_t *native, FzeroViewport viewport, double alpha) {
  return FzeroRenderViewPrepareHd(&single_player, native, viewport, alpha);
}

void FzeroRendererReset(void) { FzeroRenderViewReset(&single_player); }
void FzeroRendererShutdown(void) { shutdown_view(&single_player); }
unsigned FzeroRendererWorkerCount(void) { return FzeroRenderViewWorkerCount(&single_player); }
void FzeroRendererBeginFrame(const uint8_t ram[0x20000], unsigned frame) {
  FzeroRenderViewBeginFrame(&single_player, ram, frame);
}
void FzeroRendererCaptureLine(const Ppu *p, unsigned line) {
  FzeroRenderViewCaptureLine(&single_player, p, line);
}
void FzeroRendererEndFrame(const Ppu *p, const uint32_t stock[256 * 224]) {
  FzeroRenderViewEndFrame(&single_player, p, stock);
}
bool FzeroRendererDraw(uint32_t *out, FzeroViewport viewport, double alpha) {
  return FzeroRenderViewDraw(&single_player, out, viewport, alpha);
}
bool FzeroRendererDrawPresentation(uint32_t *native, uint32_t *out, size_t capacity,
                                   FzeroViewport viewport, double alpha, unsigned scale) {
  return FzeroRenderViewDrawPresentation(&single_player, native, out, capacity, viewport, alpha, scale);
}
bool FzeroRendererHasFrame(void) { return FzeroRenderViewHasFrame(&single_player); }
bool FzeroRendererLoadCapture(const char *path) { return FzeroRenderViewLoadCapture(&single_player, path); }
const uint32_t *FzeroRendererStockFrame(void) { return FzeroRenderViewStockFrame(&single_player); }
