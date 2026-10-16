#include "fzero_local_mode.h"
#include "fzero_runtime.h"
#include "fzero_gameplay.h"
#include "fzero_tracks.h"
#include "fzero_course_runtime.h"
#include "fzero_deluxe.h"
#include "fzero_msu.h"
#include "fzero_vs_menu.h"
#include "common_rtl.h"
#include "snes/snes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern Snes *g_snes;
static uint8_t *stock,*return_state,*setup_state;
static size_t return_size,setup_size;
static char base_root[96],sp_root[96],rom_name[2048],deluxe_name[2048],music_name[1024];
static bool cgp_music,cold,loader,title,cars,tracks;
static FzeroGameplaySettings rules;
void FzeroLocalModeShutdown(void) {
  free(stock);free(return_state);free(setup_state);stock=return_state=setup_state=NULL;
  return_size=setup_size=0;
}
bool FzeroLocalModeInit(const uint8_t *data,size_t size,const char *rom_path,
                        const char *deluxe_path,const char *music,bool cgp) {
  FzeroLocalModeShutdown();if(!data || size!=0x80000)return false;
  stock=malloc(size);if(!stock)return false;memcpy(stock,data,size);
  snprintf(base_root,sizeof(base_root),"%s",RtlSaveRoot());
  snprintf(rom_name,sizeof(rom_name),"%s",rom_path?rom_path:"");
  snprintf(deluxe_name,sizeof(deluxe_name),"%s",deluxe_path?deluxe_path:"");
  snprintf(music_name,sizeof(music_name),"%s",music?music:"");cgp_music=cgp;
  return true;
}
static bool snapshot(uint8_t **data,size_t *size) {
  free(*data);*data=NULL;*size=RtlSaveSnapshotToMemory(NULL,0);
  if(!*size)return false;
  *data=malloc(*size);return *data && RtlSaveSnapshotToMemory(*data,*size)==*size;
}
static bool profile(bool local) {
  uint8_t *rom=malloc(0x80000);size_t size=0x80000;if(!rom)return false;
  memcpy(rom,stock,size);
  FzeroTrackLoaderEnable(local?false:loader);FzeroTracksEnableTitle(local?false:title);
  FzeroGameplaySettings active=local?(FzeroGameplaySettings){0}:rules;
  FzeroGameplayConfigure(&active,local?false:cars,local?false:tracks);
  bool ok=FzeroMsuConfigure(local?"":music_name,local?false:cgp_music,rom_name) &&
      FzeroTracksPrepare(&rom,&size,local?false:cars,deluxe_name);
  if(ok) {
    snes_free(g_snes);g_snes=NULL;
    RtlRegisterGame(local?FzeroLocalGameInfo():FzeroGameInfo());ok=SnesInit(rom,(int)size)!=NULL;
  }
  free(rom);return ok;
}
bool FzeroLocalModeEnter(FzeroVersusConfig config) {
  if(!stock || FzeroLocalEnabled() || return_state)return false;
  if(!snapshot(&return_state,&return_size))return false;
  rules=*FzeroGameplaySettingsCurrent();cars=FzeroBsCars();tracks=FzeroBsTracks();
  loader=FzeroTrackLoaderEnabled();title=FzeroTracksTitleEnabled();
  cold=FzeroDeluxeActive() || FzeroTracksActive() || FzeroMsuActive();
  snprintf(sp_root,sizeof(sp_root),"%s",RtlSaveRoot());
  FzeroTracksSavesFinish();RtlWriteSram();
  if(cold && !profile(true))return false;
  RtlRegisterGame(FzeroLocalGameInfo());
  RtlSetSaveRoot(base_root);if(!FzeroLocalSelectSaveRoot())return false;
  RtlReadSram();FzeroVsMenuRestore(false);
  /* Save native menu/boot state once, before enabling the local trailer.
   * Every Change Race starts from this same actual machine context. */
  if(!snapshot(&setup_state,&setup_size))return false;
  return FzeroLocalConfigure(config);
}
bool FzeroLocalModeSetup(void) {
  if(!setup_state)return false;
  FzeroLocalShutdown();RtlRegisterGame(FzeroGameInfo());
  return RtlLoadSnapshotFromMemory(setup_state,setup_size);
}
bool FzeroLocalModeReturn(void) {
  if(!return_state) {
    FzeroLocalShutdown();RtlRegisterGame(FzeroGameInfo());RtlSetSaveRoot(base_root);RtlSetQuickSaveRoot(NULL);
    RtlReset(1);FzeroGameInfo()->session_reset();RtlReadSram();return true;
  }
  RtlWriteSram();FzeroLocalShutdown();
  if(cold && !profile(false))return false;
  RtlRegisterGame(FzeroGameInfo());RtlSetSaveRoot(sp_root);RtlSetQuickSaveRoot(FzeroTracksSaveBaseRoot());
  bool ok=RtlLoadSnapshotFromMemory(return_state,return_size);
  if(ok) {
    free(return_state);free(setup_state);return_state=setup_state=NULL;return_size=setup_size=0;
    FzeroVsMenuRestore(false);
  }
  return ok;
}
