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
  const char *boost="fzero callout boost.wav",*final="fzero callout final.wav";
  wav(boost,1000);wav(final,2000);
  CHECK(!FzeroCalloutsValid("missing.wav") && FzeroCalloutsValid(boost));
  CHECK(!FzeroCalloutsConfigure(true,boost,""));
  FzeroLapObservation s={.generation=1,.race=7,.tick=100,.total=5,.racing=true};
  CHECK(FzeroCalloutsObserve(&s)==0);s.tick++;s.completed[0]=1;
  CHECK(FzeroCalloutsObserve(&s)==0 && !audible(1));
  CHECK(FzeroCalloutsConfigure(true,boost,final));s.completed[0]=0;
  CHECK(FzeroCalloutsObserve(&s)==0);s.tick++;s.completed[0]=1;
  CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_BOOST && audible(1));
  CHECK(!FzeroCalloutsObserve(&s)); /* repeated presentation */
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
  FzeroVideoSettings config,loaded;FzeroVideoDefaults(&config);CHECK(!config.lap_callouts);
  config.lap_callouts=true;strcpy(config.lap_boost_wav,boost);strcpy(config.final_lap_wav,final);
  CHECK(FzeroVideoSave(&config,"fzero-callout-test.ini") && FzeroVideoLoad(&loaded,"fzero-callout-test.ini"));
  CHECK(loaded.lap_callouts && !strcmp(loaded.lap_boost_wav,boost) && !strcmp(loaded.final_lap_wav,final));
  if(argc==3) {
    CHECK(FzeroCalloutsValid(argv[1]) && FzeroCalloutsValid(argv[2]));
    CHECK(FzeroCalloutsConfigure(true,argv[1],argv[2]));
    s.tick++;s.completed[0]=0;CHECK(!FzeroCalloutsObserve(&s));
    s.tick++;s.completed[0]=1;CHECK(FzeroCalloutsObserve(&s)==FZERO_CALLOUT_BOOST);
    CHECK(audible(s.generation));
  }
  FzeroCalloutsShutdown();CHECK(!remove(boost) && !remove(final) && !remove("fzero-callout-test.ini"));
  puts("PASS lap callouts: both WAVs required, conversion, mixing, clipping, lap/final priority, load/rollback, pause and config paths");
  return 0;
}
