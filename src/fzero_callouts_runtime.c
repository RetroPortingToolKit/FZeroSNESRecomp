#include "fzero_callouts.h"
#include "fzero_local.h"
#include "fzero_online.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include <stdio.h>

void FzeroCalloutsUpdate(void) {
  FzeroLapObservation s={.generation=RtlStateGeneration(),.tick=g_cpu.master_cycles,.total=5};
  if(FzeroLocalEnabled()) {
    const FzeroVersusSnapshot *race=FzeroVersusCompleted(FzeroLocalSession());
    s.race=FzeroLocalEpoch();
    if(race) {
      s.tick=race->tick;s.racing=race->phase==FZERO_VS_RACING;s.paused=race->pause!=0;
      s.total=FZERO_VERSUS_LAPS;
      if(FzeroOnlineEnabled()) {
        int player=FzeroOnlineView();
        if(player>=0 && player<(int)FzeroVersusPlayers(race->config))s.completed[0]=race->racers[player].completed_laps;
      }
      else for(unsigned i=0;i<FzeroVersusPlayers(race->config) && i<FZERO_CALLOUT_PLAYERS;++i)s.completed[i]=race->racers[i].completed_laps;
    }
  } else {
    s.race=g_ram[0x53]|(unsigned)g_ram[0x57]<<8;
    s.racing=g_ram[0x54]==2 && g_ram[0x55]==3 && !g_ram[0x5b];
    s.completed[0]=(g_ram[0xd40]&0x80)?0:g_ram[0xd40]; /* unstarted grid sentinel */
  }
  unsigned cue=FzeroCalloutsObserve(&s);
  if(cue)fprintf(stderr,"[lap-callouts] %s\n",cue==FZERO_CALLOUT_FINAL?"Final lap":"Boost power");
}
