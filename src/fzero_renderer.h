#pragma once

#include "fzero_video.h"
#include "fzero_camera.h"
#include "snes/ppu.h"

/* Each camera owns its capture history, interpolation and worker scratch.
 * A view is used by one host thread at a time; completed draws join all its
 * workers before returning. Assets passed to capture are copied, never held
 * by reference to the running guest. NULL is rejected by the view interface. */
typedef struct FzeroRenderView FzeroRenderView;
/* Set on the host thread between completed draws; shared by both views. */
FzeroRenderView *FzeroRenderViewCreate(void);
void FzeroRenderViewDestroy(FzeroRenderView *view);
void FzeroRenderViewReset(FzeroRenderView *view);
unsigned FzeroRenderViewWorkerCount(const FzeroRenderView *view);
void FzeroRenderViewBeginFrame(FzeroRenderView *view,
                               const uint8_t ram[0x20000], unsigned frame);
void FzeroRenderViewCaptureLine(FzeroRenderView *view, const Ppu *ppu, unsigned line);
void FzeroRenderViewEndFrame(FzeroRenderView *view, const Ppu *ppu,
                             const uint32_t stock[256 * 224]);
bool FzeroRenderViewDraw(FzeroRenderView *view, uint32_t *output,
                         FzeroViewport viewport, double alpha);
bool FzeroRenderViewDrawPresentation(FzeroRenderView *view, uint32_t *native,
                                     uint32_t *hd, size_t hd_capacity,
                                     FzeroViewport viewport, double alpha, unsigned scale);
bool FzeroRenderViewHasFrame(const FzeroRenderView *view);
/* Build scenery for one versus camera from a completed stock race asset
 * capture and immutable shared world. NULL assets selects the legacy view.
 * Guest HUD/OBJ are excluded: the versus display owns both racers and HUDs.
 * This never draws or advances a guest. The asset view must be distinct. */
bool FzeroRenderViewCaptureRace(FzeroRenderView *view, const FzeroRenderView *assets,
                                const uint8_t world[0x20000], const FzeroActor *actor,
                                const FzeroCamera *camera, unsigned frame);
bool FzeroRenderViewLoadCapture(FzeroRenderView *view, const char *path);
const uint32_t *FzeroRenderViewStockFrame(const FzeroRenderView *view);

/* The original interface is a wrapper around the single-player view. */
void FzeroRendererReset(void);
/* Join rendering workers on host shutdown. Reset/state loads reuse the pool. */
void FzeroRendererShutdown(void);
unsigned FzeroRendererWorkerCount(void);
void FzeroRendererBeginFrame(const uint8_t ram[0x20000], unsigned frame);
void FzeroRendererCaptureLine(const Ppu *ppu, unsigned line);
void FzeroRendererEndFrame(const Ppu *ppu, const uint32_t stock[256 * 224]);
bool FzeroRendererDraw(uint32_t *output, FzeroViewport viewport, double alpha);
/* capacity is in pixels. Native UI/OBJ stay crisp; only Mode 7 is resampled. */
bool FzeroRendererDrawHd(uint32_t *output, size_t capacity,
                         FzeroViewport viewport, double alpha, unsigned scale);
#define FZERO_RENDERER_COMBINED_PRESENTATION 1
/* Produce the native thumbnail/rewind frame and HD presentation together.
 * native may be NULL; otherwise it holds width*224 pixels. Buffers must not
 * overlap. hd_capacity is in pixels. Native composition stays exact. */
bool FzeroRendererDrawPresentation(uint32_t *native, uint32_t *hd, size_t hd_capacity,
                                   FzeroViewport viewport, double alpha, unsigned scale);
bool FzeroRendererHasFrame(void);
bool FzeroRendererLoadCapture(const char *path);
const uint32_t *FzeroRendererStockFrame(void);
