#include "fzero_callouts.h"
#include "desktop/sdl_compat.h"
#include "mod_audio.h"
#include <limits.h>
#include <string.h>

typedef struct Clip {int16_t *samples;unsigned frames;} Clip;
static SNESModAudioClip clips[3];
extern void RtlApuLock(void);
extern void RtlApuUnlock(void);
static FzeroLapObservation previous;
static bool enabled,observed,second_lap_only,boost_announced;
static SNESModAudioVoice voice;
static unsigned voice_kind;

static bool load_clip(const char *path,Clip *out) {
  memset(out,0,sizeof(*out));
  if(!path || !*path)return false;
  /* Bound author-supplied input and decoded duration before conversion. */
#if SNESRECOMP_SDL3
  SDL_IOStream *file=SDL_IOFromFile(path,"rb");
  if(!file)return false;
  Sint64 size=SDL_GetIOSize(file);SDL_CloseIO(file);
#else
  SDL_RWops *file=SDL_RWFromFile(path,"rb");
  if(!file)return false;
  Sint64 size=SDL_RWsize(file);SDL_RWclose(file);
#endif
  if(size<44 || size>16*1024*1024)return false;
  SDL_AudioSpec spec;Uint8 *data=NULL;Uint32 bytes=0;
  if(!SDL_LoadWAV(path,&spec,&data,&bytes))return false;
  unsigned frame_bytes=(SDL_AUDIO_BITSIZE(spec.format)/8)*spec.channels;
  bool ok=spec.freq>=8000 && spec.freq<=192000 && spec.channels>=1 && spec.channels<=8 &&
      frame_bytes && bytes && bytes<=INT_MAX && bytes%frame_bytes==0 &&
      bytes/frame_bytes<=(unsigned)spec.freq*30;
  Uint8 *converted=NULL;int length=0;
  if(ok) {
#if SNESRECOMP_SDL3
    SDL_AudioSpec target={.format=SDL_AUDIO_S16,.channels=2,.freq=44100};
    ok=SDL_ConvertAudioSamples(&spec,data,(int)bytes,&target,&converted,&length);
#else
    SDL_AudioCVT cvt;
    ok=SDL_BuildAudioCVT(&cvt,spec.format,spec.channels,spec.freq,AUDIO_S16SYS,2,44100)>=0;
    if(ok && (uint64_t)bytes*(unsigned)cvt.len_mult<=16*1024*1024) {
      cvt.buf=SDL_malloc((size_t)bytes*cvt.len_mult);cvt.len=(int)bytes;
      ok=cvt.buf!=NULL;
      if(ok) {memcpy(cvt.buf,data,bytes);ok=SDL_ConvertAudio(&cvt)==0;converted=cvt.buf;length=cvt.len_cvt;}
    } else ok=false;
#endif
  }
  SDL_free(data);
  ok=ok && length>=4 && length%4==0 && length<=44100*30*4;
  if(ok) {*out=(Clip){(int16_t *)converted,(unsigned)length/4};return true;}
  SDL_free(converted);return false;
}
bool FzeroCalloutsValid(const char *path) {
  Clip clip;bool ok=load_clip(path,&clip);SDL_free(clip.samples);return ok;
}
void FzeroCalloutsShutdown(void) {
  RtlApuLock();
  enabled=observed=boost_announced=false;voice=0;voice_kind=0;
  for(unsigned i=0;i<3;++i){snes_mod_audio_unregister(clips[i]);clips[i]=0;}
  RtlApuUnlock();
}
bool FzeroCalloutsConfigure(bool wanted,const char *boost,const char *final,bool lap_two_only,const char *finish) {
  FzeroCalloutsShutdown();second_lap_only=lap_two_only;
  if(!wanted)return true;
  Clip loaded[3]={{0}};
  bool ok=load_clip(boost,&loaded[0]) && load_clip(final,&loaded[1]);
  bool has_finish=finish && *finish && load_clip(finish,&loaded[2]);
  RtlApuLock();
  if(ok)for(unsigned i=0;i<(has_finish?3u:2u);++i) {
    clips[i]=snes_mod_audio_register_pcm_s16(loaded[i].samples,loaded[i].frames,44100,2);
    if(!clips[i])ok=false;
  }
  for(unsigned i=0;i<3;++i)SDL_free(loaded[i].samples);
  if(ok)enabled=true;else FzeroCalloutsShutdown();
  RtlApuUnlock();return ok;
}
unsigned FzeroCalloutsObserve(const FzeroLapObservation *s) {
  if(!s)return FZERO_CALLOUT_NONE;
  RtlApuLock();unsigned cue=FZERO_CALLOUT_NONE;
  bool reset=!observed || s->generation!=previous.generation || s->race!=previous.race ||
      s->total!=previous.total || (!s->racing && !s->results) || (!previous.racing && !previous.results);
  if(reset) {
    previous=*s;observed=true;boost_announced=false;
    /* Loading/re-anchoring after lap 2 has begun must not announce it later
     * when another racer reaches the same lap. Restart at lap 1 re-arms. */
    for(unsigned i=0;i<FZERO_CALLOUT_PLAYERS;++i)if(s->completed[i])boost_announced=true;
    snes_mod_audio_stop_voice(voice);voice=0;
  }
  else if(s->tick>previous.tick) {
    if(enabled && !s->paused)for(unsigned i=0;i<FZERO_CALLOUT_PLAYERS;++i)
      if(s->completed[i]>previous.completed[i] && s->completed[i]<=s->total) {
        unsigned next=s->completed[i]==s->total?FZERO_CALLOUT_FINISH:
            s->completed[i]==s->total-1?FZERO_CALLOUT_FINAL:FZERO_CALLOUT_BOOST;
        if(next==FZERO_CALLOUT_FINISH && (!clips[2] || previous.completed[i]+1!=s->total))continue;
        if(next==FZERO_CALLOUT_BOOST && second_lap_only && (boost_announced || s->completed[i]!=1))continue;
        if(next>cue)cue=next;
      }
    /* High water marks suppress duplicate crossings after rollback and
     * backwards driving. User loads change generation and re-anchor above. */
    for(unsigned i=0;i<FZERO_CALLOUT_PLAYERS;++i)if(s->completed[i]>previous.completed[i]) {
      previous.completed[i]=s->completed[i];
      /* Consume a paused/skipped crossing too: this mode never announces
       * an old lap-2 event on a later racer or after resuming. */
      if(s->completed[i])boost_announced=true;
    }
    previous.tick=s->tick;previous.paused=s->paused;previous.racing=s->racing;previous.results=s->results;
    if(cue && cue<voice_kind && snes_mod_audio_voice_active(voice))cue=FZERO_CALLOUT_NONE;
    if(cue) {
      snes_mod_audio_stop_voice(voice);voice=snes_mod_audio_play_voice(clips[cue-1],100);voice_kind=cue;
      if(!voice)cue=FZERO_CALLOUT_NONE;
    }
    snes_mod_audio_set_voice_gain(voice,s->paused?0:100);
  }
  RtlApuUnlock();return cue;
}
