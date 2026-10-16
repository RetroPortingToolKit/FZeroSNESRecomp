/*
 * F-Zero (SNES) game-side runtime for SNESRecomp.
 *
 * F-Zero is a plain LoROM cartridge with 2 KiB of battery-backed SRAM and no
 * enhancement chip, so this file carries no coprocessor wiring. What it does
 * carry is the piece every Mode 7 title needs: the CPU runs a whole frame
 * ahead of the deferred line renderer, so any PPU register the game changes
 * from a raster IRQ - and F-Zero changes the Mode 7 matrix, the scroll
 * registers and the layer enables from its horizon and HUD splits - would
 * otherwise be visible to the renderer on *every* line rather than only from
 * the line the split happens on.
 *
 * The fix is the same one Super Mario Kart uses: snapshot the PPU register
 * file at the top of the frame, record what each IRQ changed and on which
 * scanline, then replay those deltas at the matching line while the deferred
 * renderer walks the frame. Unlike SMK's version this one carries no
 * per-screen special cases - it is a straight generic replay.
 */

#include "fzero_runtime.h"
#include "fzero_renderer.h"
#include "fzero_deluxe.h"
#include "fzero_tracks.h"
#include "fzero_title.h"
#include "fzero_course_runtime.h"
#include "fzero_records.h"
#include "fzero_hdma.h"
#include "fzero_state_mode.h"
#include "fzero_msu.h"
#include "fzero_gameplay.h"
#include "fzero_vehicles.h"
#include "fzero_local.h"
#include "fzero_vs_menu.h"
#include "snapshot_guard.h"

#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/cart.h"
#include "snes/dma.h"
#include "snes/interp_bridge.h"
#include "snes/ppu.h"
#include "snes/saveload.h"
#include "snes/snes.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  kMasterClocksPerLine = 1364u,
  kLinesPerFrame = 262u,
  kVblankStartLine = 225u,
  kMasterClocksPerFrame = kMasterClocksPerLine * kLinesPerFrame,
  kFirstVblankMaster = kMasterClocksPerLine * kVblankStartLine,
  kMaximumSlicesPerFrame = 8192,
  kMaximumInterruptsPerFrame = 128,
  /* F-Zero's race screen splits the sky, the Mode 7 track and the HUD; a
   * handful of comparator events per frame covers that with headroom. */
  kMaximumIrqEventsPerFrame = 16,
};

typedef struct FzeroIrqEvent {
  uint16_t line;
  uint8_t after[PPU_SAVESTATE_REGS_SIZE];
  uint8_t changed[PPU_SAVESTATE_REGS_SIZE];
  uint16_t oam[0x100];
  uint8_t high_oam[0x20];
} FzeroIrqEvent;

static bool s_initialized;
static uint32_t s_resume_pc;
static uint64_t s_next_frame_master;
static unsigned s_host_frames;
static bool s_restoring_local_assets;
static int s_last_lle_result = 1;
static uint8_t s_frame_hdmaen;
static bool s_loaded_runtime_state;
static uint8_t *s_output_pixels;
static size_t s_output_pitch;
static uint32_t s_stock_pixels[256 * 224];
static FzeroViewport s_viewport = {256, 0, 4.0 / 3.0, false};
static uint8_t s_published_ram[0x20000];
static unsigned s_wide_projection_accepts;
static bool s_deferred_presentation;
static uint32_t *s_hd_pixels;
static size_t s_hd_capacity;
static unsigned s_hd_scale;
static bool s_hd_ready;

static void widened_projection(CpuState *cpu, uint32_t pc) {
  (void)pc;
  if (!s_viewport.enhanced) return;
  int x = (int16_t)cpu->A;
  /* Retail DCC6..DCD2 rejects X outside [-32,288); the caller uses the
   * returned carry to activate/deactivate opponent graphics and state.
   * Admit only the newly exposed range; all original accepted/rejected
   * paths otherwise retain their opcodes and register behavior. */
  if ((x < -32 || x >= 288) && x >= -32 - s_viewport.extra && x < 288 + s_viewport.extra) {
    cpu->_flag_Z = 0;
    cpu->_flag_N = ((uint16_t)(x - (288 + s_viewport.extra)) & 0x8000) != 0;
    interp_bridge_pre_opcode_redirect(0x00dcd3);
    ++s_wide_projection_accepts;
    if (s_wide_projection_accepts <= 4 || s_wide_projection_accepts % 600 == 0)
      fprintf(stderr, "[fzero-visibility] frame=%u x=%d extra=%d accepted=%u\n",
              s_host_frames, x, s_viewport.extra, s_wide_projection_accepts);
  }
}

/* Frame-start snapshot the deferred renderer rewinds to. */
static uint8_t s_frame_ppu_start[PPU_SAVESTATE_REGS_SIZE];
static uint16_t s_frame_oam_start[0x100];
static uint8_t s_frame_high_oam_start[0x20];
static DmaChannel s_frame_dma_channels[8];

static FzeroIrqEvent s_irq_events[kMaximumIrqEventsPerFrame];
static uint8_t s_irq_event_count;

/* The game-owned compositor handles wide output. Keep the shared PPU's
 * smaller widescreen buffers disabled. */
bool g_ws_active = false;
int g_ws_extra = 0;

static bool raster_trace_enabled(void) {
  static bool initialized;
  static bool enabled;
  if (!initialized) {
    const char *value = getenv("SNESRECOMP_RASTER_TRACE");
    enabled = value && value[0] == '1';
    initialized = true;
  }
  return enabled;
}

static uint16_t read_vector(uint16_t address) {
  uint8_t low = cpu_read8(&g_cpu, 0x00, address);
  uint8_t high = cpu_read8(&g_cpu, 0x00, (uint16_t)(address + 1u));
  return (uint16_t)(low | ((uint16_t)high << 8));
}

static uint16_t nmi_vector_address(void) {
  return g_cpu.emulation ? 0xfffau : 0xffeau;
}

static uint16_t irq_vector_address(void) {
  return g_cpu.emulation ? 0xfffeu : 0xffeeu;
}

static void synchronize_hardware(void) {
  snes_sync_master_clock(g_snes, g_cpu.master_cycles);
  cart_sync_coprocessors(g_snes->cart, g_cpu.master_cycles);
}

static int run_interrupt(uint16_t vector_address, uint64_t deadline) {
  cpu_push_interrupt_frame_at(&g_cpu, s_resume_pc);
  interp_bridge_set_master_deadline(deadline);
  int result =
      interp_bridge_run_interrupt(&g_cpu, read_vector(vector_address));
  interp_bridge_set_master_deadline(0);
  return result;
}

/* Service one IRQ and record the PPU delta it produced, tagged with the
 * scanline its comparator fired on, for the deferred renderer to replay. */
static int run_irq(uint64_t deadline) {
  const uint16_t irq_line = g_snes->vTimer;
  uint8_t before[PPU_SAVESTATE_REGS_SIZE];
  memcpy(before, g_ppu, sizeof(before));

  int result = run_interrupt(irq_vector_address(), deadline);

  if (s_irq_event_count < kMaximumIrqEventsPerFrame) {
    FzeroIrqEvent *event = &s_irq_events[s_irq_event_count++];
    event->line = irq_line;
    memcpy(event->after, g_ppu, sizeof(event->after));
    memcpy(event->oam, g_ppu->oam, sizeof(event->oam));
    memcpy(event->high_oam, g_ppu->highOam, sizeof(event->high_oam));
    for (size_t i = 0; i < sizeof(event->changed); i++)
      event->changed[i] = (uint8_t)(before[i] != event->after[i]);
  }
  return result;
}

static bool irq_pending(void) { return g_snes->inIrq; }

/* Deadline for the next execution slice.
 *
 * Never let the CPU run a whole frame in one slice. `snes->inIrq` is a single
 * latch, so a slice that sweeps past several comparator crossings delivers ONE
 * IRQ and silently drops the rest. F-Zero chains four raster splits per frame
 * (each handler re-arms vTimer for the next band: 18 -> 28 -> 47 -> 86), so a
 * frame-long slice collapses that chain to the first band and the Mode 7 track
 * renders with the sky's matrix. Cap every slice at the next comparator edge so
 * each split is delivered at its own beam position. */
static uint64_t slice_deadline(uint64_t frame_deadline) {
  uint64_t irq_at = 0;
  if (!snes_next_irq_master(g_snes, g_cpu.master_cycles, &irq_at))
    return frame_deadline;
  /* One clock past the edge so the post-slice beam sync actually crosses it. */
  if (irq_at < g_cpu.master_cycles) return frame_deadline;
  irq_at += 1u;
  return irq_at < frame_deadline ? irq_at : frame_deadline;
}

static bool run_main_slice(uint64_t deadline) {
  const uint64_t before = g_cpu.master_cycles;
  interp_bridge_set_master_deadline(deadline);
  s_last_lle_result = interp_bridge_run_until_quiescent(&g_cpu, s_resume_pc);
  interp_bridge_set_master_deadline(0);
  uint32_t resume = interp_bridge_lle_resume_pc();
  if (resume) s_resume_pc = resume;
  synchronize_hardware();
  return s_last_lle_result && g_cpu.master_cycles != before;
}

static void run_one_frame(void) {
  if(FzeroLocalTick((uint32_t)g_snes->input1_currentState|((uint32_t)g_snes->input2_currentState<<12))) {
    /* One host tick advances one shared race. Keep SPC/beam clocks running
     * for the single soundtrack; neither camera invokes a guest frame. */
    g_cpu.master_cycles+=kMasterClocksPerFrame;
    synchronize_hardware();s_next_frame_master=g_cpu.master_cycles+kMasterClocksPerFrame;
    ++s_host_frames;FzeroLocalPublish();return;
  }
  FzeroVehiclesSync();
  /* This host replays HDMA during deferred scanout. The clock-driven engine
   * must not also consume its tables while the CPU runs ahead of that frame. */
  snes_set_hdma_beam_enabled(g_snes, false);
  RtlSetPadState(0,FzeroLocalEnabled()?FzeroLocalSetupInput():
      FzeroVsMenuInput(FzeroTracksMenuInput(FzeroGameplayMenuInput(g_snes->input1_currentState,g_ram),g_ram),g_ram));
  /* A consumed host menu confirmation must not execute the native handler
   * later in this frame. Its delayed joypad sample could otherwise select GP
   * before the host saves the single-player return point (notably with HD). */
  if(FzeroVsMenuPending()) {
    /* Keep the single SPC/beam clock running while the CPU menu is parked,
     * just as during a versus pause. No stale native confirmation executes. */
    g_cpu.master_cycles+=kMasterClocksPerFrame;synchronize_hardware();
    s_next_frame_master=g_cpu.master_cycles+kMasterClocksPerFrame;++s_host_frames;return;
  }
  const uint32_t previous_scene = g_ram[0x54] | (uint32_t)g_ram[0x55] << 8 | (uint32_t)g_ram[0x56] << 16;
  if (!s_initialized) {
    uint64_t reset_master = g_cpu.master_cycles;
    cpu_state_init(&g_cpu, g_ram);
    /* RtlReset keeps APU/beam sync anchored to the monotonic master clock. */
    g_cpu.master_cycles = reset_master;
    g_cpu.coprocessor_master_cycles = reset_master;
    s_resume_pc = read_vector(0xfffcu);
    /* The first interrupt edge is scanline 225, not the end of scanline 261.
     * Keeping every recurring deadline on that beam phase matters for games
     * that only work while $4212 reports vblank. */
    s_next_frame_master = reset_master + kFirstVblankMaster;
    s_host_frames = 0;
    s_last_lle_result = 1;
    s_initialized = true;
    fprintf(stderr, "[fzero] boot RESET=$%06x\n", (unsigned)s_resume_pc);
  }

  while (s_next_frame_master <= g_cpu.master_cycles)
    s_next_frame_master += kMasterClocksPerFrame;
  const uint64_t deadline = s_next_frame_master;
  unsigned slices = 0;
  unsigned interrupts = 0;
  s_irq_event_count = 0;

  if (s_host_frames > 0 && g_snes->nmiEnabled) {
    g_snes->inNmi = true;
    s_last_lle_result = run_interrupt(nmi_vector_address(), deadline);
    g_snes->inNmi = false;
    s_frame_hdmaen = g_snesrecomp_last_hdmaen;
  }

  /* Everything the renderer rewinds to. Taken after NMI because that is where
   * the game uploads the next frame's VRAM, OAM and HDMA tables. */
  memcpy(s_frame_ppu_start, g_ppu, sizeof(s_frame_ppu_start));
  memcpy(s_frame_oam_start, g_ppu->oam, sizeof(s_frame_oam_start));
  memcpy(s_frame_high_oam_start, g_ppu->highOam,
         sizeof(s_frame_high_oam_start));
  memcpy(s_frame_dma_channels, g_dma->channel, sizeof(s_frame_dma_channels));
  memcpy(s_published_ram, g_ram, sizeof(s_published_ram));

  while (s_last_lle_result && g_cpu.master_cycles < deadline &&
         slices++ < kMaximumSlicesPerFrame) {
    const bool progressed = run_main_slice(slice_deadline(deadline));
    const bool took_wai = interp_bridge_lle_took_wai() != 0;

    if (irq_pending() && !g_cpu._flag_I) {
      if (interrupts++ >= kMaximumInterruptsPerFrame) {
        fprintf(stderr, "[fzero] IRQ storm at frame=%u resume=$%06x\n",
                s_host_frames, (unsigned)s_resume_pc);
        s_last_lle_result = 0;
        break;
      }
      s_last_lle_result = run_irq(deadline);
      synchronize_hardware();
      continue;
    }

    if (g_cpu.master_cycles >= deadline) break;

    if (took_wai) {
      /* WAI resumes only on an interrupt edge. Reconstructing the bridge one
       * scanline later loses Interp816::waiting, so park the CPU explicitly
       * while the beam keeps running. */
      bool woke_for_irq = false;
      while (g_cpu.master_cycles < deadline) {
        const uint64_t remaining = deadline - g_cpu.master_cycles;
        const uint32_t idle = remaining > kMasterClocksPerLine
                                  ? kMasterClocksPerLine
                                  : (uint32_t)remaining;
        g_cpu.master_cycles += idle;
        synchronize_hardware();
        if (irq_pending() && !g_cpu._flag_I) {
          if (interrupts++ >= kMaximumInterruptsPerFrame) {
            s_last_lle_result = 0;
            break;
          }
          s_last_lle_result = run_irq(deadline);
          synchronize_hardware();
          woke_for_irq = s_last_lle_result != 0;
          break;
        }
      }
      if (!woke_for_irq) break;
      continue;
    }

    if (!progressed) {
      const uint64_t remaining = deadline - g_cpu.master_cycles;
      const uint32_t idle = remaining > kMasterClocksPerLine
                                ? kMasterClocksPerLine
                                : (uint32_t)remaining;
      g_cpu.master_cycles += idle;
      synchronize_hardware();
    }
  }

  if (slices >= kMaximumSlicesPerFrame) {
    fprintf(stderr, "[fzero] slice cap at frame=%u resume=$%06x master=%llu\n",
            s_host_frames, (unsigned)s_resume_pc,
            (unsigned long long)g_cpu.master_cycles);
    s_last_lle_result = 0;
  }

  if (g_cpu.master_cycles < deadline) {
    g_cpu.master_cycles = deadline;
    synchronize_hardware();
  }

  if (!s_host_frames) s_frame_hdmaen = g_snesrecomp_last_hdmaen;
  s_next_frame_master += kMasterClocksPerFrame;
  s_host_frames++;
  FzeroTracksMenuTick(g_ram, previous_scene);
  FzeroLocalSetupTick(s_host_frames);
  FzeroVsMenuTick(g_ram);

#if SNESRECOMP_TRACE
  if (s_host_frames <= 16 || (s_host_frames % 600u) == 0) {
    fprintf(stderr,
            "[fzero] frame=%u resume=$%06x P=%02x E=%u master=%llu "
            "slices=%u irq=%u hdma=%02x\n",
            s_host_frames, (unsigned)s_resume_pc, g_cpu.P, g_cpu.emulation,
            (unsigned long long)g_cpu.master_cycles, slices, interrupts,
            s_frame_hdmaen);
  }
#else
  (void)slices;
  (void)interrupts;
#endif
}

void FzeroBeginDrawing(uint8_t *pixels, size_t pitch) {
  s_output_pixels = pixels;
  s_output_pitch = pitch;
  PpuBeginDrawing(g_ppu, pixels, pitch, kPpuRenderFlags_NewRenderer);
}

int FzeroFrameWidth(void) { return s_viewport.width; }
int FzeroFrameHeight(void) { return FzeroLocalEnabled()?448:224; }

void FzeroSetViewport(FzeroViewport viewport) {
  if (viewport.width != s_viewport.width) {
    /* Versus parks the native CPU and reuses this course asset capture.
     * A viewport change invalidates presentation, not those immutable assets. */
    if(!FzeroLocalSession())FzeroRendererReset();
    s_hd_ready = false;
  }
  s_viewport = viewport;
}

void FzeroPresent(double alpha) {
  if(FzeroLocalEnabled() && !FzeroLocalSession()) {
    s_hd_ready=false;
    if(s_output_pixels) {
      uint32_t *p=(uint32_t *)s_output_pixels;
      FzeroUiCanvas canvas={p,s_viewport.width,448,1};
      const FzeroNativeUi *ui=FzeroLocalUi();
      FzeroUiRect(canvas,0,0,s_viewport.width,448,0xff000000);
      FzeroUiCenter(ui,canvas,176,"VERSUS MODE",FZERO_UI_MENU,FZERO_UI_YELLOW);
      FzeroUiCenter(ui,canvas,208,FzeroUiCourseName(FzeroLocalConfig().course),FZERO_UI_MENU,FZERO_UI_CYAN);
      FzeroUiCenter(ui,canvas,240,"PLEASE WAIT",FZERO_UI_SMALL,FZERO_UI_GREY);
    }
    return;
  }
  if(FzeroLocalSession()) {
    s_hd_ready=s_hd_scale && s_hd_pixels &&
        FzeroLocalDraw(s_hd_pixels,s_hd_capacity,s_viewport,alpha,s_hd_scale);
    if(s_output_pixels)FzeroLocalDraw((uint32_t *)s_output_pixels,(size_t)s_viewport.width*448,s_viewport,alpha,1);
    return;
  }
  s_hd_ready = s_hd_scale && s_hd_pixels &&
      FzeroRendererDrawPresentation(s_viewport.enhanced ? (uint32_t *)s_output_pixels : NULL,
          s_hd_pixels, s_hd_capacity, s_viewport, alpha, s_hd_scale);
  if (!s_hd_ready && s_viewport.enhanced && s_output_pixels)
    FzeroRendererDraw((uint32_t *)s_output_pixels, s_viewport, alpha);
  if(s_hd_ready)FzeroTracksOverlay(s_hd_pixels,s_viewport.width*s_hd_scale,224*s_hd_scale,s_viewport.width*s_hd_scale*4);
  else if(s_output_pixels)FzeroTracksOverlay((uint32_t *)s_output_pixels,s_viewport.width,224,s_output_pitch);
  if(s_hd_ready)FzeroVsMenuDraw((FzeroUiCanvas){s_hd_pixels,s_viewport.width*(int)s_hd_scale,224*(int)s_hd_scale,s_hd_scale},s_viewport.extra,g_ram);
  else if(s_output_pixels)FzeroVsMenuDraw((FzeroUiCanvas){(uint32_t *)s_output_pixels,s_viewport.width,224,1},s_viewport.extra,g_ram);
}
void FzeroSetDeferredPresentation(bool deferred) { s_deferred_presentation = deferred; }

void FzeroSetMode7Hd(unsigned scale, uint32_t *pixels, size_t capacity) {
  if (!pixels || !FzeroValidHdScale(scale)) scale = 0;
  if (scale != s_hd_scale || pixels != s_hd_pixels) s_hd_ready = false;
  s_hd_scale = scale;
  s_hd_pixels = scale ? pixels : NULL;
  s_hd_capacity = scale ? capacity : 0;
}
const uint32_t *FzeroHdFrame(void) {
  return s_hd_ready && FzeroRendererHasFrame() ? s_hd_pixels : NULL;
}
unsigned FzeroHdScale(void) { return FzeroHdFrame() ? s_hd_scale : 1; }

static uint8_t hdma_read_bus(void *context, uint32_t address, uint8_t open_bus) {
  (void)context;
  uint8_t bank = (uint8_t)(address >> 16);
  uint16_t offset = (uint16_t)address;
  if (bank == 0x7e || bank == 0x7f) return g_ram[address & 0x1ffff];
  if ((bank & 0x7f) < 0x40 && offset < 0x2000) return g_ram[offset];
  if ((bank & 0x7f) < 0x40 && offset < 0x6000)
    return ReadRegOpenBus(offset, open_bus);
  const Cart *cart = g_snes->cart;
  if (((bank >= 0x70 && bank < 0x7e) || bank >= 0xf0) &&
      offset < 0x8000 && cart->ram && cart->ramSize)
    return cart->ram[(((bank & 15u) << 15) | offset) & (cart->ramSize - 1)];
  uint8_t *mapped = cart_getRomPtr(g_snes->cart, bank, offset);
  return mapped ? *mapped : open_bus;
}

static void hdma_write_bus(void *context, uint8_t reg, uint8_t value) {
  (void)context;
  ppu_write(g_ppu, reg, value);
}

void FzeroDrawPpuFrame(void) {
  if(FzeroLocalSession() && !s_restoring_local_assets) {if(!s_deferred_presentation)FzeroPresent(1);return;}
  const bool capture = FzeroLocalEnabled() || s_viewport.enhanced || s_hd_scale || getenv("FZERO_CAPTURE_FRAME") != NULL ||
                       getenv("FZERO_CAPTURE_FRAMES") != NULL;
  if (capture) {
    FzeroRendererBeginFrame(s_published_ram, s_host_frames);
    PpuBeginDrawing(g_ppu, (uint8_t *)s_stock_pixels, 256 * 4, kPpuRenderFlags_NewRenderer);
  }
  SimpleHdma channels[8];
  FzeroHdma bus_channels[8] = {0};
  FzeroHdmaBus bus = {.read = hdma_read_bus, .write = hdma_write_bus};
  const bool deluxe_hdma = FzeroDeluxeActive() || FzeroTracksActive();
  bool active[8] = {false};
  uint8_t cpu_ppu_registers[PPU_SAVESTATE_REGS_SIZE];
  uint16_t cpu_oam[0x100];
  uint16_t cpu_cgram[0x100];
  uint8_t cpu_cgram_pointer = g_ppu->cgramPointer;
  uint8_t cpu_cgram_buffer = g_ppu->cgramBuffer;
  bool cpu_cgram_second_write = g_ppu->cgramSecondWrite;
  uint8_t cpu_high_oam[0x20];
  DmaChannel cpu_dma_channels[8];
  uint8_t post_wrap_event = 0;
  bool crossed_vblank = false;

  /* The CPU is a frame ahead of the renderer; park its live view of the PPU
   * and restore it once the deferred frame has been walked. */
  memcpy(cpu_ppu_registers, g_ppu, sizeof(cpu_ppu_registers));
  memcpy(cpu_oam, g_ppu->oam, sizeof(cpu_oam));
  /* The BS carousel rewrites shared palettes between rows. Those raster
   * colors and the CGRAM write latch belong only to this scanout; leaving
   * them live recolors (or blanks) other cars on the following frame. */
  memcpy(cpu_cgram, g_ppu->cgram, sizeof(cpu_cgram));
  memcpy(cpu_high_oam, g_ppu->highOam, sizeof(cpu_high_oam));
  memcpy(cpu_dma_channels, g_dma->channel, sizeof(cpu_dma_channels));

  if (raster_trace_enabled()) {
    fprintf(stderr, "[fzero-raster] frame=%u hdma=%02x events=%u",
            s_host_frames ? s_host_frames - 1u : 0u, s_frame_hdmaen,
            s_irq_event_count);
    for (uint8_t i = 0; i < s_irq_event_count; i++) {
      unsigned changed = 0;
      for (size_t j = 0; j < sizeof(s_irq_events[i].changed); j++)
        changed += s_irq_events[i].changed[j] != 0;
      fprintf(stderr, " line%u=%u/%u/m%02x", i, s_irq_events[i].line, changed,
              s_irq_events[i].after[offsetof(Ppu, bgmode)]);
    }
    fputc('\n', stderr);
    for (int c = 0; c < 8; ++c) if (s_frame_hdmaen & (1u << c)) {
      const DmaChannel *d = &s_frame_dma_channels[c];
      fprintf(stderr, "[fzero-hdma] channel=%d reg=%02x bank=%02x address=%04x indirect=%d indbank=%02x mode=%d live_enable=%02x\n",
              c, d->bAdr, d->aBank, d->aAdr, d->indirect, d->indBank, d->mode,
              g_snesrecomp_last_hdmaen);
    }
  }

  memcpy(g_ppu, s_frame_ppu_start, sizeof(s_frame_ppu_start));
  memcpy(g_ppu->oam, s_frame_oam_start, sizeof(s_frame_oam_start));
  memcpy(g_ppu->highOam, s_frame_high_oam_start,
         sizeof(s_frame_high_oam_start));
  memcpy(g_dma->channel, s_frame_dma_channels, sizeof(s_frame_dma_channels));
  PpuSetExtraSpace(g_ppu, 0);

  /* The host frame window opens at the vblank edge, so recorded IRQ lines run
   * 225..261 and then wrap to 0..224. Find the wrap and treat the last
   * pre-wrap event's OAM as the state the visible frame starts from. */
  for (uint8_t i = 1; i < s_irq_event_count; i++) {
    if (s_irq_events[i].line < s_irq_events[i - 1].line) {
      post_wrap_event = i;
      crossed_vblank = true;
      memcpy(g_ppu->oam, s_irq_events[i - 1].oam,
             sizeof(s_irq_events[i - 1].oam));
      memcpy(g_ppu->highOam, s_irq_events[i - 1].high_oam,
             sizeof(s_irq_events[i - 1].high_oam));
      break;
    }
  }

  dma_startDma(g_dma, s_frame_hdmaen, true);
  for (int channel = 0; channel < 8; channel++) {
    active[channel] = g_dma->channel[channel].hdmaActive;
    if (active[channel] && deluxe_hdma) {
      const DmaChannel *d = &g_dma->channel[channel];
      bus_channels[channel] = (FzeroHdma){.table = d->aAdr, .bank = d->aBank,
        .indirect_bank = d->indBank, .mode = d->mode, .reg = d->bAdr,
        .indirect = d->indirect, .active = true};
    } else if (active[channel])
      SimpleHdma_Init(&channels[channel], &g_dma->channel[channel]);
  }

  FzeroVehiclesBeginFrame(g_ppu, s_published_ram);
  for (int line = 0; line <= 224; line++) {
    for (uint8_t index = 0; index < s_irq_event_count; index++) {
      const FzeroIrqEvent *event = &s_irq_events[index];
      const size_t inidisp_offset = offsetof(Ppu, inidisp);
      uint16_t replay_line = (uint16_t)(event->line + 1u);
      /* Forced blank affects the active line; unblank resumes rendering on the
       * following one. ppu_runLine(N) writes output row N-1. */
      if (event->changed[inidisp_offset] &&
          !(event->after[inidisp_offset] & 0x80))
        replay_line++;
      if (replay_line != (uint16_t)line) continue;

      uint8_t *registers = (uint8_t *)g_ppu;
      if (crossed_vblank && index == post_wrap_event) {
        /* Rebase the first visible-line IRQ against the post-NMI frame state
         * rather than against the pre-wrap IRQ that preceded it in CPU time. */
        memcpy(registers, event->after, sizeof(event->after));
      } else {
        for (size_t i = 0; i < sizeof(event->changed); i++)
          if (event->changed[i]) registers[i] = event->after[i];
      }
      if (!crossed_vblank || index >= post_wrap_event) {
        memcpy(g_ppu->oam, event->oam, sizeof(event->oam));
        memcpy(g_ppu->highOam, event->high_oam, sizeof(event->high_oam));
      }
    }
    FzeroVehiclesRaster(g_ppu, s_published_ram, (unsigned)line);
    if (capture) FzeroRendererCaptureLine(g_ppu, line);
    ppu_runLine(g_ppu, line);
    for (int channel = 0; channel < 8; channel++)
      if (active[channel]) {
        if (deluxe_hdma) FzeroHdmaLine(&bus_channels[channel], &bus);
        else SimpleHdma_DoLine(&channels[channel]);
      }
  }
  (void)ppu_checkOverscan(g_ppu);
  ppu_handleVblank(g_ppu);
  if (capture) {
    FzeroRendererEndFrame(g_ppu, s_stock_pixels);
    if (s_viewport.enhanced || s_hd_scale) {
      if (!s_deferred_presentation && !s_restoring_local_assets) FzeroPresent(1);
    }
    if (!s_restoring_local_assets && s_output_pixels && !s_viewport.enhanced) for (unsigned y = 0; y < 224; ++y)
        memcpy(s_output_pixels + y * s_output_pitch, s_stock_pixels + y * 256, 256 * 4);
    if(s_output_pixels)PpuBeginDrawing(g_ppu, s_output_pixels, s_output_pitch, kPpuRenderFlags_NewRenderer);
  }
  if(!s_restoring_local_assets && s_output_pixels && !s_viewport.enhanced)
    FzeroTracksOverlay((uint32_t *)s_output_pixels,256,224,s_output_pitch);
  FzeroVehiclesEndFrame(g_ppu);

  /* Window bounds are hardware latches, including when the next scene turns
   * their HDMA channel off. Carry the last scanline forward unless the CPU
   * wrote a different bound after the frame-start snapshot. Restoring all
   * four from the CPU-only view resurrected the car-menu window on results.
   * CGRAM/OAM remain separately restored: their menu raster reuse is local. */
  for (size_t i = offsetof(Ppu, window1left); i <= offsetof(Ppu, window2right); ++i)
    if (cpu_ppu_registers[i] == s_frame_ppu_start[i])
      cpu_ppu_registers[i] = ((const uint8_t *)g_ppu)[i];
  memcpy(g_ppu, cpu_ppu_registers, sizeof(cpu_ppu_registers));
  memcpy(g_ppu->oam, cpu_oam, sizeof(cpu_oam));
  memcpy(g_ppu->cgram, cpu_cgram, sizeof(cpu_cgram));
  g_ppu->cgramPointer = cpu_cgram_pointer;
  g_ppu->cgramBuffer = cpu_cgram_buffer;
  g_ppu->cgramSecondWrite = cpu_cgram_second_write;
  memcpy(g_ppu->highOam, cpu_high_oam, sizeof(cpu_high_oam));
  memcpy(g_dma->channel, cpu_dma_channels, sizeof(cpu_dma_channels));
  FzeroLocalCapture();
}

static void session_reset(void) {
  FzeroLocalShutdown();
  FzeroRendererReset();
  FzeroTracksMenuReset();
  s_wide_projection_accepts = 0;
  interp_bridge_set_pre_opcode_hook(0, NULL);
  interp_bridge_set_pre_opcode_hook(0x00dcc6, widened_projection);
  FzeroTracksInstallHooks();
  FzeroGameplayInstallHooks();
  FzeroVehiclesInstallHooks();
  FzeroRecordsInstallHooks();
  /* The runtime's own baseline is stock, not the shipped defaults: a host that
   * offers video settings calls FzeroSetViewport with them, and one that does
   * not (headless captures, tools) must stay at 4:3 unless FZERO_ASPECT opts
   * in. Seeding this with the shipped defaults would silently widen every
   * headless run and every frame drawn before the host's first viewport. */
  FzeroVideoSettings video;
  FzeroVideoStock(&video);
  const char *aspect = getenv("FZERO_ASPECT");
  if (aspect && FzeroParseAspect(aspect, &video.aspect)) video.enhanced = true;
  s_viewport = FzeroCalculateViewport(&video, 1920, 1080);
  s_initialized = false;
  s_resume_pc = 0;
  s_next_frame_master = 0;
  s_host_frames = 0;
  s_last_lle_result = 1;
  s_frame_hdmaen = 0;
  s_irq_event_count = 0;
  s_loaded_runtime_state = false;
  memset(s_irq_events, 0, sizeof(s_irq_events));
  interp_bridge_set_master_deadline(0);
}

enum {
  kFzeroStateMagic = 0x4f52465au, /* "zFRO" */
  kFzeroStateVersion = 1u,
};

/* ── Snapshot mode guard ──────────────────────────────────────────────────
 *
 * The engine applies a snapshot's guest blob before the game's trailer is
 * ever read, and offers no pre-load veto, so a refusal has to be able to put
 * the machine back. FzeroStateGuardArm() takes one whole-machine snapshot in
 * memory; if a load is then refused, fzero_on_state_loaded restores it. The
 * host arms the guard where a load it does not make itself can begin — when
 * the save-state browser opens, which freezes the guest, so one snapshot
 * covers every load the browser can attempt. Loads the host makes itself
 * (the F-key slots, the debug server) are checked by FzeroStateFileMode()
 * before the engine is called at all, and never reach this path. */
static bool s_state_mode_refused;
static bool s_state_guard_restoring;
static uint8_t *s_state_guard_blob;
static size_t s_state_guard_len;
static size_t s_state_guard_cap;

FzeroStateMode FzeroStateModeCurrent(void) {
  if (FzeroLocalEnabled()) return kFzeroStateModeLocalVersus;
  if (FzeroTracksStateActive()) return kFzeroStateModeTrackPack;
  if (FzeroMsuActive()) return FzeroDeluxeActive() ? kFzeroStateModeDeluxeMsu : kFzeroStateModeStockMsu;
  return FzeroDeluxeActive() ? kFzeroStateModeDeluxe : kFzeroStateModeStock;
}

typedef struct FzeroRuntimeState {
  uint32_t magic;
  uint32_t version;
  CpuState cpu;
  uint32_t resume_pc;
  uint64_t next_frame_master;
  uint32_t host_frames;
  int32_t last_lle_result;
  uint8_t frame_hdmaen;
  uint8_t initialized;
  uint8_t memsel;
  uint8_t last_hdmaen;
  uint8_t irq_event_count;
  /* FzeroStateMode. Claimed out of the reserved padding rather than appended,
   * so the trailer keeps its size and field order and a 1.5.0 snapshot — which
   * wrote zero here — still loads, reading back as kFzeroStateModeUnknown. */
  uint8_t mode;
  uint8_t reserved[2];
  int32_t snes_frame;
  uint64_t main_cpu_cycles_estimate;
  uint64_t apu_pace_cycles_estimate;
  uint8_t frame_ppu_start[PPU_SAVESTATE_REGS_SIZE];
  uint16_t frame_oam_start[0x100];
  uint8_t frame_high_oam_start[0x20];
  DmaChannel frame_dma_channels[8];
  FzeroIrqEvent irq_events[kMaximumIrqEventsPerFrame];
} FzeroRuntimeState;

static void fzero_state_save_extra(SaveLoadInfo *sli) {
  if (FzeroLocalEnabled()) {
    size_t size=FzeroLocalSaveSize();uint8_t *image=calloc(1,size);
    if(image) {FzeroLocalSave(image,size);sli->func(sli,image,size);free(image);}
    else { /* Invalid image is refused by preflight, never partially resumed. */
      uint8_t zero[32]={0};
      while(size){size_t n=size<sizeof(zero)?size:sizeof(zero);sli->func(sli,zero,n);size-=n;}
    }
  }
  if (FzeroTracksStateActive() || FzeroLocalEnabled()) RtlSaveExecutionState(sli);
  FzeroRuntimeState state;
  memset(&state, 0, sizeof(state));
  state.magic = kFzeroStateMagic;
  state.version = kFzeroStateVersion;
  state.mode = (uint8_t)FzeroStateModeCurrent();
  FzeroTracksMenuState(state.reserved, false);
  if(FzeroVsMenuSelected())state.reserved[1]|=128;
  state.cpu = g_cpu;
  /* Never persist an address-space-dependent host pointer. */
  state.cpu.ram = NULL;
  state.resume_pc = s_resume_pc;
  state.next_frame_master = s_next_frame_master;
  state.host_frames = s_host_frames;
  state.last_lle_result = s_last_lle_result;
  state.frame_hdmaen = s_frame_hdmaen;
  state.initialized = s_initialized;
  state.memsel = g_memsel;
  state.last_hdmaen = g_snesrecomp_last_hdmaen;
  state.irq_event_count = s_irq_event_count;
  state.snes_frame = snes_frame_counter;
  state.main_cpu_cycles_estimate = g_main_cpu_cycles_estimate;
  state.apu_pace_cycles_estimate = g_apu_pace_cycles_estimate;
  memcpy(state.frame_ppu_start, s_frame_ppu_start,
         sizeof(state.frame_ppu_start));
  memcpy(state.frame_oam_start, s_frame_oam_start,
         sizeof(state.frame_oam_start));
  memcpy(state.frame_high_oam_start, s_frame_high_oam_start,
         sizeof(state.frame_high_oam_start));
  memcpy(state.frame_dma_channels, s_frame_dma_channels,
         sizeof(state.frame_dma_channels));
  memcpy(state.irq_events, s_irq_events, sizeof(state.irq_events));
  sli->func(sli, &state, sizeof(state));
  if (FzeroTracksStateActive() && !FzeroLocalEnabled()) {
    uint8_t hash[32]; memcpy(hash, FzeroTracksActiveHash(), 32);
    sli->func(sli, hash, sizeof(hash));
    FzeroTracksSaveState(sli, false);
  }
}

static void fzero_state_load_extra(SaveLoadInfo *sli, uint32_t version) {
  FzeroRuntimeState state;
  (void)version;
  uint8_t *local=NULL;
  if(FzeroLocalEnabled()) {
    local=malloc(FzeroLocalSaveSize());
    if(!local) {s_loaded_runtime_state=false;s_state_mode_refused=true;return;}
    sli->func(sli,local,FzeroLocalSaveSize());
  }
  if ((FzeroTracksStateActive() || FzeroLocalEnabled()) && !RtlLoadExecutionState(sli)) {
    free(local);
    s_loaded_runtime_state = false; s_state_mode_refused = true; return;
  }
  memset(&state, 0, sizeof(state));
  sli->func(sli, &state, sizeof(state));
  s_loaded_runtime_state = state.magic == kFzeroStateMagic &&
                           state.version == kFzeroStateVersion;
  if (!s_loaded_runtime_state) {free(local);return;}

  uint8_t content_hash[32] = {0};
  if (state.mode == kFzeroStateModeTrackPack) sli->func(sli, content_hash, sizeof(content_hash));
  bool content_matches = state.mode != kFzeroStateModeTrackPack ||
      (FzeroTracksStateActive() && !memcmp(content_hash, FzeroTracksActiveHash(), 32));

  /* Cartridge check. The two modes keep their slots in different directories
   * under different prefixes, so a crossing file has been moved there by hand
   * — but a Deluxe state resumed on the stock ROM restores registers and RAM
   * for code that is not in the cartridge, which is a crash, not a glitch.
   * Refuse instead, and let fzero_on_state_loaded put the machine back. */
  if (!content_matches || !FzeroStateModeCompatible((FzeroStateMode)state.mode,
                                FzeroStateModeCurrent())) {
    fprintf(stderr,
            "[fzero-state] refused: snapshot is %s, this session is %s\n",
            FzeroStateModeName((FzeroStateMode)state.mode),
            FzeroStateModeName(FzeroStateModeCurrent()));
    s_loaded_runtime_state = false;
    s_state_mode_refused = true;
    free(local);
    return;
  }

  if(local) {
    bool ok=FzeroLocalRestore(local,FzeroLocalSaveSize());free(local);
    if(!ok){s_loaded_runtime_state=false;s_state_mode_refused=true;return;}
  }

  if(state.mode == kFzeroStateModeTrackPack)FzeroTracksSaveState(sli, true);

  FzeroVsMenuRestore((state.reserved[1]&128)!=0);state.reserved[1]&=127;
  FzeroTracksMenuState(state.reserved, true);
  FzeroVehiclesLoaded();
  g_cpu = state.cpu;
  g_cpu.ram = g_ram;
  s_resume_pc = state.resume_pc;
  s_next_frame_master = state.next_frame_master;
  s_host_frames = state.host_frames;
  s_last_lle_result = state.last_lle_result;
  s_frame_hdmaen = state.frame_hdmaen;
  s_initialized = state.initialized != 0;
  g_memsel = state.memsel;
  g_snesrecomp_last_hdmaen = state.last_hdmaen;
  s_irq_event_count = state.irq_event_count > kMaximumIrqEventsPerFrame
                          ? kMaximumIrqEventsPerFrame
                          : state.irq_event_count;
  snes_frame_counter = state.snes_frame;
  g_main_cpu_cycles_estimate = state.main_cpu_cycles_estimate;
  g_apu_pace_cycles_estimate = state.apu_pace_cycles_estimate;
  memcpy(s_frame_ppu_start, state.frame_ppu_start,
         sizeof(s_frame_ppu_start));
  memcpy(s_frame_oam_start, state.frame_oam_start,
         sizeof(s_frame_oam_start));
  memcpy(s_frame_high_oam_start, state.frame_high_oam_start,
         sizeof(s_frame_high_oam_start));
  memcpy(s_frame_dma_channels, state.frame_dma_channels,
         sizeof(s_frame_dma_channels));
  memcpy(s_irq_events, state.irq_events, sizeof(s_irq_events));
}

static const FzeroStateTrailer kFzeroStateTrailer = {
    sizeof(FzeroRuntimeState), offsetof(FzeroRuntimeState, mode),
    kFzeroStateMagic, kFzeroStateVersion};

static const char *fzero_snapshot_identity(void) {
  return FzeroLocalEnabled()?"fzero-local-2p-v1":NULL;
}
static bool fzero_snapshot_preflight(const void *data,size_t size) {
  if(!FzeroLocalEnabled())return true;
  FzeroStateMode mode;
  uint32_t version=0;size_t guest=RtlSnapshotGuestSize(&version);
  uint32_t header[2]={0};if(size>=sizeof(header))memcpy(header,data,sizeof(header));
  size_t total=RtlSaveSnapshotToMemory(NULL,0);
  return guest && total>SNES_SNAPSHOT_GUARD_OVERHEAD &&
      size==total-SNES_SNAPSHOT_GUARD_OVERHEAD && header[1]==version &&
      size>=guest+FzeroLocalSaveSize()+sizeof(FzeroRuntimeState) &&
      FzeroStateProbeBytes(data,size,&kFzeroStateTrailer,&mode) && mode==kFzeroStateModeLocalVersus &&
      FzeroLocalValidate((const uint8_t *)data+guest,FzeroLocalSaveSize());
}
/* File preflight also serves direct host loads. Every other engine entry
 * (browser, rewind, memory) uses the same identity and preflight callbacks. */
static uint8_t *local_state_file(const char *path,size_t *size) {
  FILE *f=fopen(path,"rb");if(!f)return NULL;
  uint8_t *data=NULL;
  if(!fseek(f,0,SEEK_END)) {
    long end=ftell(f);
    if(end>0 && end<=8*1024*1024 && !fseek(f,0,SEEK_SET)) {
      data=malloc((size_t)end);
      if(data && fread(data,1,(size_t)end,f)!=(size_t)end){free(data);data=NULL;}
      if(data)*size=(size_t)end;
    }
  }
  fclose(f);return data;
}

int FzeroStateFileMode(const char *path, FzeroStateMode *out) {
  if(FzeroLocalEnabled()) {
    size_t size=0,length=0;const void *payload=NULL;uint8_t *data=local_state_file(path,&size);
    int ok=data && snes_snapshot_guard_open(data,size,fzero_snapshot_identity(),&payload,&length) &&
        FzeroStateProbeBytes(payload,length,&kFzeroStateTrailer,out);
    free(data);return ok;
  }
  FzeroStateTrailer layout = kFzeroStateTrailer;
  if (FzeroTracksStateActive()) layout.size += 32 + FzeroTracksSaveStateSize();
  return FzeroStateProbeFile(path, &layout, out);
}

int FzeroStateFileAcceptable(const char *path) {
  if(FzeroLocalEnabled()) {
    size_t size=0,length=0;const void *payload=NULL;uint8_t *data=local_state_file(path,&size);
    int ok=data && snes_snapshot_guard_open(data,size,fzero_snapshot_identity(),&payload,&length) &&
        fzero_snapshot_preflight(payload,length);
    free(data);return ok;
  }
  FzeroStateMode mode = kFzeroStateModeUnknown;
  /* A file with no readable trailer is refused rather than guessed at: every
   * snapshot this game has ever written carries one, so its absence means the
   * file is truncated or from another game, and the engine would apply the
   * guest blob before anything noticed. */
  if (!FzeroStateFileMode(path, &mode)) return 0;
  if (!FzeroStateModeCompatible(mode, FzeroStateModeCurrent())) return 0;
  if (FzeroTracksStateActive()) {
    FILE *f = fopen(path, "rb"); uint8_t hash[32];
    if (!f) return 0;
    int ok = !fseek(f, -(long)(32 + FzeroTracksSaveStateSize()), SEEK_END) && fread(hash, 1, 32, f) == 32 &&
             !memcmp(hash, FzeroTracksActiveHash(), 32);
    fclose(f); return ok;
  }
  return 1;
}

void FzeroStateGuardArm(void) {
  size_t needed;
  s_state_mode_refused = false;
  s_state_guard_len = 0;
  /* Probe for the size the way snes_rewind does: ask with a generous buffer
   * once, then keep it. The machine is ~256 KiB; 2 MiB is headroom, not a
   * measurement. */
  if (!s_state_guard_blob) {
    s_state_guard_cap = 2u * 1024u * 1024u;
    s_state_guard_blob = (uint8_t *)malloc(s_state_guard_cap);
    if (!s_state_guard_blob) {
      s_state_guard_cap = 0;
      fprintf(stderr, "[fzero-state] guard snapshot unavailable (out of memory)\n");
      return;
    }
  }
  needed = RtlSaveSnapshotToMemory(s_state_guard_blob, s_state_guard_cap);
  if (needed == 0 || needed > s_state_guard_cap) {
    fprintf(stderr, "[fzero-state] guard snapshot failed; a refused load "
                    "cannot be undone this session\n");
    return;
  }
  s_state_guard_len = needed;
}

void FzeroStateGuardDisarm(void) { s_state_guard_len = 0; }

int FzeroStateGuardTripped(void) {
  int tripped = s_state_mode_refused ? 1 : 0;
  s_state_mode_refused = false;
  return tripped;
}

static void fzero_on_state_loaded(uint32_t version) {
  FzeroRendererReset();
  (void)version;

  /* A refused load has already overwritten the machine with the snapshot's
   * guest blob — the engine applies that before the game's trailer is read.
   * Put the armed snapshot back so the player keeps the session they were
   * in. The restore is an ordinary load, so it re-enters here; the flag stops
   * it from looking for a guard of its own. */
  if (s_state_mode_refused && !s_state_guard_restoring) {
    if (s_state_guard_len) {
      s_state_guard_restoring = true;
      if (!RtlLoadSnapshotFromMemory(s_state_guard_blob, s_state_guard_len)) {
        fprintf(stderr, "[fzero-state] could not restore the pre-load "
                        "snapshot; resetting\n");
        session_reset();
      }
      s_state_guard_restoring = false;
    } else {
      /* Nothing to go back to: a defined cold boot beats a machine holding
       * another cartridge's RAM. */
      fprintf(stderr, "[fzero-state] no guard snapshot; resetting the machine\n");
      session_reset();
    }
    return;
  }

  if (!s_loaded_runtime_state) return;

  /* Host pacing cursors, not guest state: point them at the restored counters
   * so the first APU access cannot underflow against the future state that
   * existed immediately before Load was pressed. */
  g_apu_last_sync_master = g_cpu.master_cycles;
  g_apu_last_sync_cycles = g_apu_pace_cycles_estimate;
  g_snes->beamMasterLast = g_cpu.master_cycles;
  interp_bridge_set_master_deadline(0);
  s_loaded_runtime_state = false;
  FzeroMsuRestoreAudio(g_ram);
  if (FzeroTracksStateActive() || FzeroLocalEnabled()) RtlApplyExecutionState();

  if(FzeroLocalEnabled()) {
    /* Rebuild immutable assets without executing a guest instruction. The
     * session image owns terrain mutations; the native snapshot owns VRAM,
     * palettes, raster registers and the parked bootstrap's RAM. */
    memcpy(s_published_ram,g_ram,sizeof(s_published_ram));
    s_restoring_local_assets=true;FzeroDrawPpuFrame();s_restoring_local_assets=false;
    FzeroLocalResetViews();
  }

  /* The legacy PPU snapshot chunk omits VMAIN. Deluxe records rely on the
   * $80 latch retained from native video setup (e.g. $008439).
   * Loading an overview directly after cold boot otherwise uses $00: each
   * high byte lands one word late, garbling labels, the venue and minimap.
   * Reconstruct this known native menu invariant for existing states too. */
  if (FzeroDeluxeActive() && !g_ram[0x54] && g_ram[0x55] >= 2 && g_ram[0x55] <= 3)
    ppu_write(g_ppu, 0x15, 0x80);

  /* Older snapshots discarded the race's final HDMA window latches. Results
   * have already collapsed that window and disabled its channel, so no later
   * transfer can repair it. Restore the native collapsed bounds on load. */
  if (g_ram[0x54] == 3 && (g_ram[0x55] < 4 || !(g_ppu->screenEnabled[0] & 3)) &&
      !(s_frame_hdmaen & 0x04)) {
    g_ppu->window1left = 1;
    g_ppu->window1right = 0;
    s_frame_ppu_start[offsetof(Ppu, window1left)] = 1;
    s_frame_ppu_start[offsetof(Ppu, window1right)] = 0;
  }
}

static const RtlGameInfo kFzeroGameInfo = {
    .title = "f_zero",
    .initialize = session_reset,
    .run_frame = run_one_frame,
    .draw_ppu_frame = FzeroDrawPpuFrame,
    .save_name_prefix = "fzero",
    .state_save_extra = fzero_state_save_extra,
    .state_load_extra = fzero_state_load_extra,
    .on_state_loaded = fzero_on_state_loaded,
    .session_reset = session_reset,
};

const RtlGameInfo *FzeroLocalGameInfo(void) {
  static RtlGameInfo local;
  local=kFzeroGameInfo;local.title="f_zero_local_2p";local.save_name_prefix="fzero-local-2p";
  local.snapshot_guard_identity=fzero_snapshot_identity;local.snapshot_preflight=fzero_snapshot_preflight;
  return &local;
}

const RtlGameInfo *FzeroGameInfo(void) {
  if(FzeroLocalEnabled())return FzeroLocalGameInfo();
  static RtlGameInfo deluxe;
  /* Records visibility alone must retain the existing save-slot prefix. */
  if (FzeroTracksActive() || FzeroTitleHash()) {
    deluxe = kFzeroGameInfo;
    deluxe.title = FzeroTracksActiveId(); deluxe.save_name_prefix = "fzero-library";
    return &deluxe;
  }
  if (!FzeroDeluxeActive() && !FzeroMsuActive()) return &kFzeroGameInfo;
  deluxe = kFzeroGameInfo;
  if (FzeroDeluxeActive()) {
    deluxe.title = "bs_f_zero_deluxe";
    deluxe.save_name_prefix = "fzero-bs-deluxe";
  }
  if (FzeroMsuActive()) {
    deluxe.title = FzeroDeluxeActive() ? "bs_f_zero_deluxe_msu1" : "f_zero_msu1";
    deluxe.save_name_prefix = FzeroDeluxeActive() ? "fzero-bs-deluxe-msu1" : "fzero-msu1";
  }
  return &deluxe;
}
uint32_t FzeroResumePc(void) { return s_resume_pc; }
int FzeroLastLleResult(void) { return s_last_lle_result; }
