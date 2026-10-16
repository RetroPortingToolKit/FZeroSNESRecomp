#pragma once
#include "fzero_online.h"
#include "recomp_launcher.h"

void FzeroNetplayLauncher(RecompLauncherCGameInfo *info);
bool FzeroNetplayWanted(const RecompLauncherCSettings *settings);
bool FzeroNetplayStart(const RecompLauncherCSettings *settings);
/* Full negotiated viewport, independent of the drawable and offline prefs. */
FzeroViewport FzeroNetplayViewport(const FzeroVideoSettings *settings,int width,int height);
bool FzeroNetplayAdmit(uint16_t local_pad,int *running);
void FzeroNetplayFinish(void);
void FzeroNetplayStop(void);
bool FzeroNetplayResume(void);
