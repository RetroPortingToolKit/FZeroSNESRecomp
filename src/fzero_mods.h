#pragma once
#include "fzero_video.h"
#include "recomp_launcher.h"
/* Actual race presentation; retained for the next visit to Mods. A different
 * requested detail/smoothing setting must not reuse an old runtime result.
 * effective=0 means native fallback; 1 is a real 1x HD sampling setting. */
void FzeroModsNoteHdScale(unsigned requested, unsigned samples, unsigned effective);
const RecompLauncherCModProvider *FzeroModsProvider(FzeroVideoSettings *settings,
                                                   const char *path, bool bundled_music);
