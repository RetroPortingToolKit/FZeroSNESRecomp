#include "fzero_actor.h"

int FzeroActorTick(FzeroActor *a,const uint8_t world[0x20000],FzeroActorTables t,
                   uint8_t tick,FzeroActorMineEvent *mine) {
  a->sound_flags=a->sound_flags2=a->sound_port3=a->landing_sound=0;
  if (!a->recovery_active) {
    a->surface_id=FzeroActorSampleSurface(world,a->x,a->y);
    FzeroActorSurface(a);
    if ((a->state&0x81)==0x80) a->ground_flags=a->ground_timer=0;
  } else a->surface=a->surface_extra=a->ground_flags=a->ground_timer=a->surface_flags=0;
  int gates=FzeroActorProgressTick(a,world,t,tick);
  if (!a->recovery_active) FzeroActorRecoveryTrigger(a,world,t);
  FzeroActorPit(a,t);
  if (!a->race_state && !a->recovery_active) {
    FzeroActorTurnStage(a);FzeroActorInputAction(a,t);FzeroActorTurnPerformance(a,t);
    FzeroActorSpinTrigger(a);FzeroActorFailedStart(a);
  } else if (!a->race_state && a->recovery_active) a->action=0x10;
  FzeroActorScratch s;FzeroActorScratchReset(&s);
  FzeroActorSlide(a);FzeroActorDamageSurface(a,t);FzeroActorDash(a);
  FzeroActorTopSpeed(a,&s,t);FzeroActorGrip(a,&s,t);FzeroActorSpinState(a,&s);
  FzeroActorMine(a,t,mine);FzeroActorAltitude(a,&s,t,tick);FzeroActorSpeed(a,&s,t,a->race_state);
  FzeroActorApplySpin(a,&s);FzeroActorTurn(a,&s);FzeroActorApplyGrip(a,&s);
  FzeroActorVelocity(&s,t);FzeroActorWind(a,&s,t,a->race_state);
  FzeroActorStoreVelocity(a,&s);FzeroActorMove(a,&s);FzeroActorAnimation(a,t);
  if (a->no_control&255) --a->no_control;
  if (a->damage_cooldown) --a->damage_cooldown;
  return gates;
}
