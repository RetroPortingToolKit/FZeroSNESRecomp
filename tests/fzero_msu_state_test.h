#pragma once
#include "snes/msu1.h"

/* Private-ROM integration: exercise the actual title snapshot callbacks and
 * next presentation update, with nonconstant PCM so a restart cannot pass. */
static void msu_state_samples(int16_t *out) {
  memset(out,0,512*2*sizeof(*out));
  RtlApuLock();msu1_mix(out,512);RtlApuUnlock();
}
static bool msu_state_check(long frame) {
  const char *path=getenv("FZERO_TEST_MSU_STATE");
  static bool done;
  if(!path || done || frame<1000 || !(msu1_read(0x2000)&0x10))return true;
  uint8_t bookmark[MSU1_PLAYBACK_STATE_SIZE],live[MSU1_PLAYBACK_STATE_SIZE],after[MSU1_PLAYBACK_STATE_SIZE];
  msu1_capture_playback(bookmark);
  uint64_t elapsed=0;for(unsigned i=0;i<8;++i)elapsed|=(uint64_t)bookmark[16+i]<<(8*i);
  if(elapsed<44100)return true;
  size_t cap=RtlRollbackSnapshotBound(),size=0,canonical_size=0;
  uint8_t *saved=malloc(cap),*canonical=malloc(cap),*other=malloc(cap);
  int16_t expected[1024],actual[1024];bool ok=saved && canonical && other;
  if(ok)ok=(size=RtlSaveSnapshotToMemory(saved,cap))!=0 && RtlSaveSnapshot(path);
  if(ok)ok=(canonical_size=RtlNetplaySnapshotToMemory(canonical,cap))!=0;
  if(ok) {
    msu_state_samples(expected);
    /* Deliberately close the saved course stream and select a different song. */
    msu1_write(0x2004,5);msu1_write(0x2005,0);msu1_write(0x2007,3);
    msu_state_samples(actual);
    size_t n=RtlNetplaySnapshotToMemory(other,cap);
    ok=n==canonical_size && !memcmp(canonical,other,RtlNetplaySnapshotHashedBytes(n));
  }
  for(unsigned disk=0;ok && disk<2;++disk) {
    ok=disk?RtlLoadSnapshot(path):RtlLoadSnapshotFromMemory(saved,size);
    if(!ok)break;
    FzeroMsuUpdateVersus(); /* must acknowledge the load without restarting */
    msu1_capture_playback(after);
    ok=!memcmp(bookmark,after,sizeof(after));
    msu_state_samples(actual);
    ok=ok && !memcmp(expected,actual,sizeof(actual));
  }
  if(ok) {
    size_t n=RtlRollbackSaveToMemory(other,cap);
    msu_state_samples(actual);msu1_capture_playback(live);
    ok=n && RtlRollbackLoadFromMemory(other,n);
    FzeroMsuUpdateVersus();msu1_capture_playback(after);
    ok=ok && !memcmp(live,after,sizeof(after));
  }
  if(ok) {
    ok=RtlLoadSnapshotFromMemory(saved,size);
    snes_rewind_set_defaults(1,4,1);snes_rewind_configure();
    snes_rewind_note_frame();msu_state_samples(actual);snes_rewind_note_frame();
    ok=ok && snes_rewind_open();
    if(ok){snes_rewind_step(-1);snes_rewind_commit();}
    FzeroMsuUpdateVersus();msu1_capture_playback(after);
    ok=ok && !memcmp(bookmark,after,sizeof(after));
    msu_state_samples(actual);ok=ok && !memcmp(expected,actual,sizeof(actual));
    snes_rewind_shutdown();
  }
  if(saved && size)ok=RtlLoadSnapshotFromMemory(saved,size) && ok;
  free(saved);free(canonical);free(other);done=true;
  fprintf(stderr,"msu-state: %s frame=%ld source-frames=%llu memory/disk exact PCM, canonical neutrality, rollback continuity, rewind\n",
      ok?"PASS":"FAIL",frame,(unsigned long long)elapsed);
  return ok;
}
