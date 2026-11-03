#include "fzero_callouts.h"
#include "fzero_video.h"
#include "mod_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do{if(!(e)){fprintf(stderr,"%d: %s\n",__LINE__,#e);exit(1);}}while(0)
static void le(FILE *f,unsigned v,unsigned bytes) {while(bytes--){fputc(v&255,f);v>>=8;}}
static void wav(const char *path,int sample) {
  FILE *f=fopen(path,"wb");CHECK(f);
  fwrite("RIFF",1,4,f);le(f,36+22050,4);fwrite("WAVEfmt ",1,8,f);
  le(f,16,4);le(f,1,2);le(f,1,2);le(f,11025,4);le(f,22050,4);le(f,2,2);le(f,16,2);
  fwrite("data",1,4,f);le(f,22050,4);for(unsigned i=0;i<11025;++i)le(f,(unsigned)sample,2);
  CHECK(!fclose(f));
}
static bool audible(uint64_t generation) {
  (void)generation;
  int16_t output[2048]={0};snes_mod_audio_mix(output,1024,32040,2);
  for(unsigned i=0;i<2048;++i)if(output[i])return true;return false;
}
int main(int argc,char **argv) {
  const char *boost="fzero callout boost.wav",*final="fzero callout final.wav",*finish="fzero finish fanfare.wav";
  wav(boost,1000);wav(final,2000);wav(finish,3000);
  CHECK(!FzeroCalloutsValid("missing.wav") && FzeroCalloutsValid(boost));
  CHECK(!FzeroCalloutsConfigure(true,boost,"",false,""));
  FzeroLapObservation s={.generation=1,.race=7,.tick=100,.total=5,.racing=true};
  CHECK(FzeroCalloutsObserve(&s)==0);s.tick++;s.completed[0]=1;
  CHECK(FzeroCalloutsObserve(&s)==0 && !audible(1));
  CHECK(FzeroCalloutsConfigure(true,boost,final,false,""));s.completed[0]=0;
  CHECK(FzeroCalloutsObserve(&s)==0);s.tick++;s.completed[0]=1;
  CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_BOOST && audible(1));
  CHECK(!FzeroCalloutsObserve(&s)); /* repeated presentation */
  ++s.tick;s.completed[0]=2;CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_BOOST);
  ++s.tick;s.completed[0]=3;CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_BOOST);
  --s.tick;s.completed[0]=0;CHECK(!FzeroCalloutsObserve(&s)); /* speculative rollback */
  s.tick+=2;s.completed[0]=1;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.completed[0]=4;s.completed[1]=1;
  CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_FINAL); /* final wins simultaneous boost */
  int16_t saturated[2048];for(unsigned i=0;i<2048;++i)saturated[i]=32000;
  snes_mod_audio_mix(saturated,1024,48000,2);CHECK(saturated[1000]==32767);
  ++s.generation;snes_mod_audio_stop_all();CHECK(!audible(s.generation)); /* engine load notification */
  CHECK(!FzeroCalloutsObserve(&s) && !audible(s.generation));
  ++s.tick;s.completed[0]=5;CHECK(!FzeroCalloutsObserve(&s)); /* race finish has no new lap */
  ++s.tick;s.racing=false;s.completed[0]=1;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.racing=true;s.completed[0]=0;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.completed[0]=1;CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_BOOST);
  ++s.tick;s.paused=true;CHECK(!FzeroCalloutsObserve(&s) && !audible(s.generation));
  ++s.tick;s.paused=false;CHECK(!FzeroCalloutsObserve(&s) && audible(s.generation));
  /* The optional timing is one shared announcement on the first real lap-2
   * crossing, including P3/P4, without changing final-lap priority. */
  CHECK(FzeroCalloutsConfigure(true,boost,final,true,""));
  s=(FzeroLapObservation){.generation=2,.race=8,.tick=100,.total=5,.racing=true};
  CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.completed[2]=1;CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_BOOST && audible(s.generation));
  ++s.tick;s.completed[0]=1;CHECK(!FzeroCalloutsObserve(&s)); /* another human's lap 2 */
  ++s.tick;s.completed[2]=2;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.completed[2]=3;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.completed[2]=4;s.completed[3]=1;CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_FINAL);
  ++s.tick;s.completed[3]=2;CHECK(!FzeroCalloutsObserve(&s));
  /* A new race/rematch re-arms; re-presenting or rewinding the crossing does not. */
  ++s.race;++s.tick;memset(s.completed,0,sizeof(s.completed));CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.completed[3]=1;CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_BOOST);
  CHECK(!FzeroCalloutsObserve(&s));--s.tick;s.completed[3]=0;CHECK(!FzeroCalloutsObserve(&s));
  s.tick+=2;s.completed[3]=1;CHECK(!FzeroCalloutsObserve(&s));
  /* Loading after lap 2, or skipping/pausing its crossing, anchors silently. */
  ++s.generation;s.completed[3]=2;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.completed[1]=1;CHECK(!FzeroCalloutsObserve(&s));
  ++s.race;++s.tick;memset(s.completed,0,sizeof(s.completed));CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.completed[0]=2;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.completed[1]=1;CHECK(!FzeroCalloutsObserve(&s));
  ++s.race;++s.tick;memset(s.completed,0,sizeof(s.completed));CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.paused=true;s.completed[0]=1;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.paused=false;s.completed[1]=1;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.completed[3]=4;CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_FINAL);
  /* Ordinary default mode still observes all four humans. */
  CHECK(FzeroCalloutsConfigure(true,boost,final,false,""));
  s=(FzeroLapObservation){.generation=3,.race=9,.tick=100,.total=5,.racing=true};CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.completed[3]=2;CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_BOOST);
  ++s.tick;s.completed[2]=4;CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_FINAL);
  /* Final finish crossing can change directly to results. Keep its voice
   * alive there; DNF, loads, repeated frames and rewinds never announce it. */
  CHECK(FzeroCalloutsConfigure(true,boost,final,false,finish));
  s=(FzeroLapObservation){.generation=4,.race=10,.tick=100,.total=5,.racing=true,.completed={4,0,0,4}};
  CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.racing=false;s.results=true;s.completed[3]=5;
  CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_FINISH && audible(s.generation));
  CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;CHECK(!FzeroCalloutsObserve(&s) && audible(s.generation));
  --s.tick;s.completed[3]=4;CHECK(!FzeroCalloutsObserve(&s));
  s.tick+=2;s.completed[3]=5;CHECK(!FzeroCalloutsObserve(&s));
  ++s.generation;CHECK(!FzeroCalloutsObserve(&s) && !audible(s.generation));
  ++s.race;s.racing=true;s.results=false;s.completed[3]=4;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.racing=false;s.results=true;CHECK(!FzeroCalloutsObserve(&s)); /* DNF, still only 4 laps */
  ++s.race;s.racing=true;s.results=false;s.completed[3]=4;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.paused=true;s.completed[3]=5;CHECK(!FzeroCalloutsObserve(&s));
  ++s.tick;s.paused=false;CHECK(!FzeroCalloutsObserve(&s));
  CHECK(FzeroCalloutsConfigure(true,boost,final,false,"missing optional.wav"));
  s=(FzeroLapObservation){.generation=5,.race=12,.tick=100,.total=5,.racing=true};
  CHECK(!FzeroCalloutsObserve(&s));++s.tick;s.completed[0]=1;
  CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_BOOST); /* Optional WAV cannot disable the required pair. */
  FzeroVideoSettings config,loaded;FzeroVideoDefaults(&config);CHECK(!config.lap_callouts && !config.lap_boost_second_lap_only);
  config.lap_callouts=true;config.lap_boost_second_lap_only=true;strcpy(config.lap_boost_wav,boost);strcpy(config.final_lap_wav,final);strcpy(config.finish_wav,finish);
  CHECK(FzeroVideoSave(&config,"fzero-callout-test.ini") && FzeroVideoLoad(&loaded,"fzero-callout-test.ini"));
  CHECK(loaded.lap_callouts && loaded.lap_boost_second_lap_only && !strcmp(loaded.lap_boost_wav,boost) && !strcmp(loaded.final_lap_wav,final) && !strcmp(loaded.finish_wav,finish));
  FILE *legacy=fopen("fzero-callout-test.ini","w");CHECK(legacy);
  fputs("[FZeroVideo]\nLapCallouts=0\n",legacy);CHECK(!fclose(legacy));
  CHECK(FzeroVideoLoad(&loaded,"fzero-callout-test.ini") && !loaded.lap_boost_second_lap_only);
  legacy=fopen("fzero-callout-test.ini","w");CHECK(legacy);
  fputs("[FZeroVideo]\nLapBoostSecondLapOnly=2\n",legacy);CHECK(!fclose(legacy));
  CHECK(!FzeroVideoLoad(&loaded,"fzero-callout-test.ini"));
  if(argc>=3) {
    CHECK(FzeroCalloutsValid(argv[1]) && FzeroCalloutsValid(argv[2]));
    CHECK(FzeroCalloutsConfigure(true,argv[1],argv[2],false,argc>3?argv[3]:""));
    s.tick++;s.completed[0]=0;CHECK(!FzeroCalloutsObserve(&s));
    s.tick++;s.completed[0]=1;CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_BOOST);
    CHECK(audible(s.generation));
    if(argc>3) {
      CHECK(FzeroCalloutsValid(argv[3]));++s.generation;s.completed[0]=4;CHECK(!FzeroCalloutsObserve(&s));
      ++s.tick;s.completed[0]=5;s.racing=false;s.results=true;
      CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_FINISH);
      bool heard=false;for(unsigned n=0;n<20;++n)heard|=audible(s.generation);CHECK(heard);
    }
  }
  FzeroCalloutsShutdown();CHECK(!remove(finish));CHECK(!remove(boost) && !remove(final) && !remove("fzero-callout-test.ini"));
  puts("PASS lap callouts: both WAVs required, conversion, mixing, clipping, lap/final priority, load/rollback, pause, four humans, lap-2-only/rematch and config paths");
  return 0;
}
