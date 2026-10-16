#pragma once
#include "fzero_renderer.h"
#include "fzero_versus.h"

typedef struct FzeroVersusVideo FzeroVersusVideo;
FzeroVersusVideo *FzeroVersusVideoCreate(void);
void FzeroVersusVideoDestroy(FzeroVersusVideo *video);
void FzeroVersusVideoReset(FzeroVersusVideo *video);
void FzeroVersusVideoMenu(FzeroVersusVideo *video,unsigned selection,unsigned result_ticks);
/* Copy a completed session; no live pointers are retained. assets may be NULL
 * for the legacy guest capture, used only as an immutable course art source. */
bool FzeroVersusVideoCapture(FzeroVersusVideo *video, const FzeroRenderView *assets,
                             const FzeroVersus *session, FzeroActorTables tables);
/* Each pane is width x224; the output is width x448 (P1 above P2). The host
 * calculates the viewport from one drawable HALF, preserving pixel aspect.
 * scale=1 is native; HD retains crisp ship/HUD pixels over resampled track. */
bool FzeroVersusVideoDraw(FzeroVersusVideo *video, uint32_t *output, size_t capacity,
                          FzeroViewport viewport, double alpha, unsigned scale);
