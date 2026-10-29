#include "fzero_sound.h"

FzeroSoundCommands FzeroSoundActor(const FzeroActor *a) {
  FzeroSoundCommands out={{0,0,0,0}};
  if(!a)return out;
  unsigned pitch=a->speed>=0x900?63:a->speed<0x200?(a->speed*4)>>8:((a->speed-0x100)*8)>>8;
  if(!pitch)pitch=1;
  out.ports[1]=(uint8_t)(pitch|(a->exhaust?0x80:0));
  unsigned effect=a->sound_flags2,skid=0;
  if(a->sound_flags&0x80)skid=0x30;
  else if(a->sound_flags&0x40)skid=0x20;
  else if(a->sound_flags&0x20)skid=0x10;
  if(skid>=0x20)effect&=~0x10u;
  unsigned hit=(effect&0x80)?4:(effect&0x40)?3:(effect&0x20)?2:effect?1:0;
  out.ports[2]=(uint8_t)(a->sound_port3|skid|hit);
  return out;
}
void FzeroSoundInit(FzeroSoundState *s,uint8_t music) {
  *s=(FzeroSoundState){.music=(uint8_t)(music&15),.phase=255};
}
static uint8_t merge_effects(uint8_t a,uint8_t b) {
  /* Stock's SPC has one engine pitch and encoded effect groups. Choose the
   * faster engine and the higher-priority event in each encoded group;
   * independent command bits from both humans are retained. Symmetric in IDs,
   * independent of either camera. OR-ing encoded IDs invents a third sound. */
  unsigned low=(a&7)>(b&7)?a&7:b&7,skid=(a&0x30)>(b&0x30)?a&0x30:b&0x30;
  return (uint8_t)(low|skid|((a|b)&0xc8));
}
FzeroSoundCommands FzeroSoundTick(FzeroSoundState *s,const FzeroVersusSnapshot *v,bool advanced) {
  FzeroSoundCommands out={{0,0,0,0}};
  if(!s || !v)return out;
  unsigned phase=v->phase|(v->pause?4:0);
  bool launch=s->phase==FZERO_VS_COUNTDOWN && v->phase==FZERO_VS_RACING && advanced;
  /* $8CA6 selects the pre-race program; $AD9F selects the uploaded course
   * song at GO. The parked guest never reaches AD9F during a VS race.
   * Also repair a resumed/loaded race with the older pre-race audio latch. */
  /* $B3CF uses arrival program 3; $8CA6 starts rev program 1 only AFTER
   * arrival. Starting it while the engine command is still zero leaves
   * its startup tone held. */
  if(v->phase==FZERO_VS_COUNTDOWN)s->music=v->countdown>FZERO_VERSUS_REV_COUNTDOWN?3:1;
  else if(v->phase==FZERO_VS_RACING)s->music=6;
  if(s->phase!=phase) {
    if(v->phase==FZERO_VS_COUNTDOWN) {s->landing=s->lap_beep=0;s->lap[0]=s->lap[1]=0;}
    if(v->phase==FZERO_VS_RESULTS)s->music|=0x80;
    else s->music&=15;
    s->phase=(uint8_t)phase;
  }
  out.ports[0]=s->music;
  /* Native pause leaves the song running and clears vehicle/effect ports.
   * $2142 bit7 belongs to the crash sequence ($B334), not a pause command;
   * holding it retriggers the crash and can strand the music after resume. */
  if(v->pause)return out;
  if(!advanced || v->phase==FZERO_VS_READY)return out;
  /* $8C87 keeps the arrival engine command at 3, before $8CA6 starts revs. */
  if(v->phase==FZERO_VS_COUNTDOWN && v->countdown>FZERO_VERSUS_REV_COUNTDOWN) {
    out.ports[1]=3;return out;
  }
  for(unsigned i=0;i<2;++i) {
    const FzeroVersusRacer *r=&v->racers[i];const FzeroActor *a=&r->actor;
    /* Reuse Practice's $9AF7 lap cue: $16 ticks normally, $6C on the
     * final lap, emitted by $FA0F on the existing native beep channel. */
    if(r->completed_laps>s->lap[i] && r->completed_laps<FZERO_VERSUS_LAPS) {
      unsigned duration=r->completed_laps==FZERO_VERSUS_LAPS-1?0x6c:0x16;
      if(duration>s->lap_beep)s->lap_beep=(uint8_t)duration;
    }
    s->lap[i]=r->completed_laps;
    if(r->status!=FZERO_VS_ACTIVE && r->finish_tick!=v->race_tick)continue;
    FzeroSoundCommands voice=FzeroSoundActor(a);
    if(v->phase==FZERO_VS_COUNTDOWN)voice.ports[1]=a->countdown_acceleration?a->countdown_acceleration&63:1;
    if(r->status==FZERO_VS_ACTIVE) {
      unsigned pitch=voice.ports[1]&63;
      if(pitch>(out.ports[1]&63))out.ports[1]=(uint8_t)((out.ports[1]&0xc0)|pitch);
      out.ports[1]|=voice.ports[1]&0xc0;
    }
    if(a->landing_sound>s->landing)s->landing=a->landing_sound;
    if((a->pressed&0x80) && !(a->state&8) && !a->boosts && !a->boost_beep_guard)out.ports[1]|=0x40;
    if(v->contact && a->damage_cooldown==(v->contact==2?9:8))voice.ports[2]=merge_effects(voice.ports[2],2);
    if(r->status==FZERO_VS_DNF)voice.ports[2]=merge_effects(voice.ports[2],4);
    out.ports[2]=merge_effects(out.ports[2],voice.ports[2]);
  }
  if(s->landing){--s->landing;out.ports[2]|=0x40;}
  if(s->lap_beep) {--s->lap_beep;out.ports[1]|=0x40;}
  if(v->phase==FZERO_VS_COUNTDOWN && (v->countdown==179 || v->countdown==119 || v->countdown==59))
    out.ports[1]|=0x40;
  if(launch)out.ports[1]|=0x40;
  return out;
}
