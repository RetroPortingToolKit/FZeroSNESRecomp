#include "fzero_course_runtime.h"
#include "fzero_tracks.h"
#include "fzero_deluxe.h"
#include "fzero_title.h"
#include "fzero_native_font.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/cart.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include <stdio.h>
#include <string.h>
extern Snes *g_snes;

static unsigned r16(unsigned a) {
  return g_ram[a] | (unsigned)g_ram[a + 1] << 8;
}
static void w16(unsigned a, unsigned v) {
  g_ram[a] = (uint8_t)v;
  g_ram[a + 1] = (uint8_t)(v >> 8);
}
static void accum(CpuState *cpu, unsigned value) {
  cpu_write_a_m(cpu, (uint16_t)value);
  cpu->_flag_Z = value == 0;
  cpu->_flag_N = (value & (cpu->m_flag ? 128 : 32768)) != 0;
}
static void width8(CpuState *cpu) {
  cpu_mirrors_to_p(cpu);
  cpu->P |= 0x30;
  cpu_p_to_mirrors(cpu);
  cpu->X &= 255;
  cpu->Y &= 255;
}
static void vram(unsigned word, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n / 2; ++i)
    g_ppu->vram[(word + i) & 0x7fff] = p[i * 2] | (uint16_t)p[i * 2 + 1] << 8;
}
static void restore_intro_tiles(const FzeroCourse *c) {
  if (!c->intro_glyph_count) return;
  /* Decode the active engine's native $0F:8000 OBJ stream. Only the 2bpp
   * letter tiles overridden for this intro need restoring. Read from ROM,
   * not a host-side VRAM backup, so loading a state during the intro works. */
  const Cart *cart = g_snes->cart;
  for (unsigned i = 0; i < c->intro_glyph_count; ++i)
    for (unsigned half = 0; half < 2; ++half) {
      unsigned tile = c->intro_glyphs[i].code + half * 16;
      const uint8_t *pixels = FzeroNativeLetterTile(cart->rom, cart->romSize, tile);
      if (pixels) {
        unsigned word = 0x5000 + tile * 16;
        vram(word, pixels, 16);
        memset(g_ppu->vram + word + 8, 0, 16);
      }
    }
}
/* The main CPU can retire intro tiles before the deferred visible frame has
 * consumed its OAM. Reapply letters only to that published intro, then restore
 * the CPU's atlas. Loading a state needs no pending host-only flag. */
void FzeroTracksIntroFrame(Ppu *ppu,const uint8_t *ram,bool begin) {
  static uint16_t backup[28][2][16];static const FzeroCourse *active;
  if(!begin) {
    if(active)for(unsigned i=0;i<active->intro_glyph_count;++i)for(unsigned half=0;half<2;++half)
      memcpy(ppu->vram+0x5000+(active->intro_glyphs[i].code+half*16)*16,backup[i][half],32);
    active=NULL;return;
  }
  active=FzeroTracksCurrentCourse();
  if(!active || ram[0x54]!=2 || ram[0x55]>2) {active=NULL;return;}
  for(unsigned i=0;i<active->intro_glyph_count;++i)for(unsigned half=0;half<2;++half) {
    unsigned word=0x5000+(active->intro_glyphs[i].code+half*16)*16;
    memcpy(backup[i][half],ppu->vram+word,32);
    for(unsigned y=0;y<8;++y) {
      const uint8_t *pixels=active->intro_glyphs[i].pixels+half*16+y*2;
      ppu->vram[word+y]=pixels[0]|(uint16_t)pixels[1]<<8;ppu->vram[word+y+8]=0;
    }
  }
}
static void terrain(CpuState *cpu, const FzeroCourse *c) {
  unsigned x = cpu->X & 255, tile = g_ram[0xcd0 + x];
  g_ram[0xd70 + x] = c->terrain[tile];
  g_ram[0xd50 + x] = c->terrain[0x100 + tile];
  if (!x)
    g_ram[0xc7] = c->terrain[0x200 + tile];
  unsigned flags = c->terrain[0x300 + tile];
  g_ram[0xcc0 + x] = (uint8_t)flags;
  if (flags & 128)
    w16(0xcc0 + x, 0xff80);
  else if (flags & 64) {
    w16(0xcc0 + x, 0x340);
    if (r16(0xb20 + x) < 0x80)
      w16(0xb20 + x, 0x80);
  } else if (flags & 32) {
    unsigned state = g_ram[0xcc1 + x];
    if (!(state & 128)) {
      if (!state)
        w16(0xcc0 + x, 0x120);
      else if (state == 2)
        w16(0xcc0 + x, 0x340);
      else if (state != 3) {
        ++g_ram[0xcc1 + x];
        goto done;
      }
    }
    if (r16(0xb20 + x) < 0x80)
      w16(0xb20 + x, 0x80);
  } else
    g_ram[0xcc1 + x] = 0;
  if ((flags & 128) || ((flags & 32) && (g_ram[0xcc1 + x] & 128)))
    if (r16(0xb20 + x) < 0x100)
      w16(0xb20 + x, 0x100);
  w16(0xc00 + x, r16(0xcc0 + x));
done:
  width8(cpu);
  interp_bridge_pre_opcode_redirect(0x008e28);
}
static void course_hook(CpuState *cpu, uint32_t pc) {
  if (pc == 0x1ebbf7) {
    accum(cpu, FzeroTracksPracticeInput());
    interp_bridge_pre_opcode_redirect(0x1ebceb); /* Native RTL, no PHP pushed. */
    return;
  }
  if (pc == 0x009f08)
    FzeroTracksRefreshCourse();
  unsigned cup_size = FzeroTracksCurrentCupSize();
  if (cup_size && (pc == 0x00cba5 || pc == 0x009a6b || pc == 0x00b4a9 || pc == 0x1eba88)) {
    unsigned limit = pc == 0x00cba5 ? cup_size : cup_size - 1;
    unsigned a = cpu->A & 255, diff = (a - limit) & 255;
    cpu->_flag_C = a >= limit;
    cpu->_flag_Z = !diff;
    cpu->_flag_N = (diff & 128) != 0;
    interp_bridge_pre_opcode_redirect(pc + 2);
    return;
  }
  const FzeroCourse *c = FzeroTracksCurrentCourse();
  if (!c)
    return;
  switch (pc) {
  case 0x0088d3:
    /* Both native engines just uploaded their stock OBJ atlas. Restore only
     * the imported intro's reviewed letter tiles, never the donor HUD/cars.
     * These tiles are shared with race messages; release them at countdown. */
    for (unsigned i = 0; i < c->intro_glyph_count; ++i)
      for (unsigned half = 0; half < 2; ++half) {
        unsigned word = 0x5000 + (c->intro_glyphs[i].code + half * 16) * 16;
        vram(word, c->intro_glyphs[i].pixels + half * 16, 16);
        memset(g_ppu->vram + word + 8, 0, 16);
      }
    break;
  case 0x008bb4:
    /* Course text has finished in both GP and Practice. In particular, the
     * imported Z occupies A7/B7, also used by the native S OK! notification. */
    restore_intro_tiles(c);
    break;
  case 0x00f7e6:
    if (c->has_music) {
      /* Retail derives this from the venue; Deluxe reads its native course
       * metadata. Imported tracks supply their own SPC upload-list offset.
       * Keep the real uploader and MSU missing-track fallback unchanged. */
      accum(cpu, c->music);
      interp_bridge_pre_opcode_redirect(0x00f7f1);
    }
    break;
  case 0x00c1cf:
    if (c->has_palette_cycles) {
      FzeroCourseCyclePalette(c, g_ram + 0x520);
      width8(cpu);
      interp_bridge_pre_opcode_redirect(0x00c231);
    }
    break;
  case 0x009f08:
    break;
  case 0x009f1b:
  case 0x009f28:
    accum(cpu, c->setting);
    interp_bridge_pre_opcode_redirect(pc + 4);
    break;
  case 0x009f4c:
  case 0x1ea7fa:
    memcpy(g_ram + 0x10000, c->pool, sizeof(c->pool));
    memcpy(g_ram + 0x14e00, c->blocks, c->block_size);
    memcpy(g_ram + 0x17000, c->grid, c->grid_size);
    w16(0xb0, 0x4e00);
    g_ram[0xade] = c->setting;
    g_ram[0xcf5] = c->setting & 15;
    g_ram[0xcff] = (c->setting >> 4) - 12;
    g_ram[0xad8] = c->setting & 15;
    fprintf(stderr, "[track-library] load %s ordinal=%u setting=%02x\n", FzeroTracksActiveId(),
            g_ram[0x53], c->setting);
    if (pc == 0x009f4c)
      interp_bridge_pre_opcode_redirect(0x009fbd);
    break;
  case 0x00a0b0:
    for (unsigned i = 0; i < 0x4000; ++i)
      g_ppu->vram[i] = (g_ppu->vram[i] & 255) | (uint16_t)c->graphics[i] << 8;
    width8(cpu);
    interp_bridge_pre_opcode_redirect(0x008e28);
    break;
  case 0x00a10d:
    g_ram[0x9c] = c->gradient;
    g_ram[0xadd] = 0;
    memcpy(g_ram + 0x520, c->palette, 0xe0);
    cpu_mirrors_to_p(cpu);
    cpu->P &= ~0x30;
    cpu_p_to_mirrors(cpu);
    interp_bridge_pre_opcode_redirect(0x00a133);
    break;
  case 0x00a11d:
    g_ram[0x9c] = c->gradient;
    interp_bridge_pre_opcode_redirect(0x00a122);
    break;
  case 0x00a127:
    memcpy(g_ram + 0x520, c->palette, 0xe0);
    interp_bridge_pre_opcode_redirect(0x00a133);
    break;
  case 0x00a4ab:
    vram(0x6000, c->sky_graphics, sizeof(c->sky_graphics));
    interp_bridge_pre_opcode_redirect(0x00a4b1);
    break;
  case 0x00a4d1:
    memcpy(g_ram + 0x13000, c->sky_back, sizeof(c->sky_back));
    break;
  case 0x00a51f:
    memcpy(g_ram + 0x13000, c->sky_front, sizeof(c->sky_front));
    break;
  case 0x008895:
    w16(0xad9, c->map_x);
    w16(0xadb, c->map_y);
    for (unsigned row = 0; row < 32; ++row) {
      vram(0x5e00 + row * 16, c->minimap + row * 16, 16);
      memset(g_ppu->vram + 0x5e08 + row * 16, 0, 16);
    }
    interp_bridge_pre_opcode_redirect(0x0088bf);
    break;
  case 0x00abcb:
    /* Deluxe bypasses the retail name loop with a metadata-buffer copy. */
    memcpy(g_ram + 0x480, c->name, sizeof(c->name));
    width8(cpu);
    interp_bridge_pre_opcode_redirect(0x00ac0b);
    break;
  case 0x00abd3:
    memcpy(g_ram + 0x480 + (cpu->X & 255), c->name, sizeof(c->name));
    width8(cpu);
    interp_bridge_pre_opcode_redirect(0x00abe9);
    break;
  case 0x00d609:
    memcpy(g_ram + 0x11fe, c->path, sizeof(c->path));
    g_ram[0xad] = c->last_checkpoint;
    g_ram[0x10d8] = c->finish_checkpoint;
    g_ram[0x10d6] = c->pit_checkpoint;
    g_ram[0x10da] = c->has_pit ? 255 : 0;
    g_ram[0x106c] = 0;
    g_ram[0x1075] = 0;
    g_ram[0x1066] = c->opponents[g_ram[0x57] < 3 ? g_ram[0x57] : 2];
    width8(cpu);
    interp_bridge_pre_opcode_redirect(0x008e28);
    break;
  case 0x00a30d:
    cpu_mirrors_to_p(cpu);
    cpu->P &= ~0x30;
    cpu_p_to_mirrors(cpu);
    interp_bridge_pre_opcode_redirect(0x00a349);
    break;
  case 0x008e36:
    terrain(cpu, c);
    break;
  case 0x009c9a: {
    /* FZEdit surfaces are property-driven. The native 69/A5/D0 tile-ID
     * tests kill valid custom railroad/trampoline landings. Match the donor
     * consumer of the normalized surface state, including intentional pits. */
    unsigned surface = g_ram[0xc00];
    cpu->Y = (uint16_t)surface;
    cpu->_flag_C = g_ram[0xd51] & 1;
    accum(cpu, surface);
    bool fatal = (surface & 0x80) || (!cpu->_flag_C && (surface & 0x20));
    interp_bridge_pre_opcode_redirect(fatal ? 0x009caf : 0x009cc1);
    break;
  }
  case 0x00eb90:
    /* Recovery uses the signed normalized ground height, not the numeric
     * range of the graphics tile. Native courses retain their own checks. */
    accum(cpu, g_ram[0xc01]);
    interp_bridge_pre_opcode_redirect((g_ram[0xc01] & 0x80) ? 0x00eb97 : 0x00ebad);
    break;
  case 0x00da04:
    for (unsigned i = 0; i < 16; ++i) {
      const uint8_t *p = c->shortcuts + i * 17;
      unsigned left = p[0] | p[1] << 8;
      if (left & 0x8000)
        break;
      unsigned bounds[8];
      for (unsigned j = 0; j < 8; ++j)
        bounds[j] = p[j * 2] | p[j * 2 + 1] << 8;
      if (r16(0x106d) >= bounds[0] && r16(0x106d) < bounds[2] && r16(0x106f) >= bounds[1] &&
          r16(0x106f) < bounds[3] && r16(0x1071) >= bounds[4] && r16(0x1071) < bounds[6] &&
          r16(0x1073) >= bounds[5] && r16(0x1073) < bounds[7]) {
        cpu->Y = p[16];
        interp_bridge_pre_opcode_redirect(0x00da8d);
        return;
      }
    }
    interp_bridge_pre_opcode_redirect(0x00da0a);
    break;
  }
}
static void title_hook(CpuState *cpu, uint32_t pc) {
  (void)cpu; (void)pc;
  FzeroTitleLoad(g_ppu->vram, g_ram + 0x600);
}
void FzeroTracksInstallHooks(void) {
  if (FzeroTitleHash())
    interp_bridge_set_pre_opcode_hook(0x0380f5, title_hook);
  if (!FzeroTracksActive())
    return;
  const uint32_t sites[] = {0x009f08, 0x009f1b, 0x009f28, 0x009f4c, 0x00a0b0, 0x00a10d, 0x00a11d,
                            0x00a127, 0x00a4ab, 0x00a4d1, 0x00a51f, 0x008895, 0x00abd3, 0x00d609,
                            0x00a30d, 0x008e36, 0x009c9a, 0x00eb90, 0x00da04, 0x0088d3, 0x008bb4,
                            0x00cba5, 0x009a6b, 0x00b4a9, 0x00c1cf, 0x00f7e6};
  for (unsigned i = 0; i < sizeof(sites) / sizeof(*sites); ++i)
    interp_bridge_set_pre_opcode_hook(sites[i], course_hook);
  if (FzeroDeluxeActive()) {
    interp_bridge_set_pre_opcode_hook(0x00abcb, course_hook);
    interp_bridge_set_pre_opcode_hook(0x1ea7fa, course_hook);
    interp_bridge_set_pre_opcode_hook(0x1eba88, course_hook);
    interp_bridge_set_pre_opcode_hook(0x1ebbf7, course_hook);
  }
}
