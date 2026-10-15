/* Private-ROM differential fixtures for the portable human-actor adapters.
 * The reference executes the original ROM routine, with real M/X widths and
 * hardware multiply/divide semantics, on a separate callback bus. It cannot
 * modify a live guest, play sound or write cartridge-derived artifacts. */
#include "fzero_actor.h"
#include "fzero_versus.h"
#include "fzero_camera.h"
#include "snes/interp816.h"
#include "sha256.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)
static uint8_t rom[0x80000], ram[0x20000];
static uint8_t ram_reads[0x20000], ram_writes[0x20000];
static bool audit_enabled;
static uint8_t multiplier, multiplicand;
static uint16_t dividend, quotient, product;
static unsigned random_state = 0x8a5cd789;
static FzeroActor initial_actor;
static FzeroActorScratch initial_scratch;
static unsigned random_word(void) {
  random_state ^= random_state << 13; random_state ^= random_state >> 17;
  random_state ^= random_state << 5; return random_state & 65535;
}
static uint8_t rom_read(void *context, uint32_t address) {
  (void)context;
  CHECK((address & 0xffff) >= 0x8000 && (address >> 16) < 16);
  return rom[(address >> 16) * 0x8000 + (address & 0x7fff)];
}
static int ram_offset(uint32_t address) {
  unsigned bank = address >> 16, offset = address & 65535;
  if (bank == 0x7e || bank == 0x7f) return (int)(address - 0x7e0000);
  if ((bank & 0x7f) < 0x40 && offset < 0x2000) return (int)offset;
  return -1;
}
static uint8_t read_bus(void *context, uint32_t address) {
  (void)context;
  int offset = ram_offset(address);
  if (offset >= 0) { if (audit_enabled) ram_reads[offset]=1; return ram[offset]; }
  switch (address) {
    case 0x4214: return (uint8_t)quotient;
    case 0x4215: return (uint8_t)(quotient >> 8);
    case 0x4216: return (uint8_t)product;
    case 0x4217: return (uint8_t)(product >> 8);
  }
  return rom_read(NULL, address);
}
static void write_bus(void *context, uint32_t address, uint8_t value) {
  (void)context;
  int offset = ram_offset(address);
  if (offset >= 0) { if (audit_enabled) ram_writes[offset]=1; ram[offset] = value; return; }
  switch (address) {
    case 0x4202: multiplicand = value; return;
    case 0x4203: multiplier = value; product = multiplicand * multiplier; return;
    case 0x4204: dividend = (dividend & 0xff00) | value; return;
    case 0x4205: dividend = (dividend & 255) | (uint16_t)value << 8; return;
    case 0x4206:
      quotient = value ? dividend / value : 0xffff;
      product = value ? dividend % value : dividend; return;
    default: fprintf(stderr, "unexpected oracle write %06x\n", address); exit(1);
  }
}
typedef struct Field { unsigned address; size_t offset, size; const char *name; } Field;
#define FIELD(address, name) {address, offsetof(FzeroActor, name), sizeof(((FzeroActor *)0)->name), #name}
static const Field actor_fields[] = {
#include "fzero_actor_fields.inc"
};
#undef FIELD
#define FIELD(address, name) {address, offsetof(FzeroActorScratch, name), sizeof(((FzeroActorScratch *)0)->name), #name}
static const Field scratch_fields[] = {
  FIELD(0x02,top_speed), FIELD(0x04,turn), FIELD(0x0e,speed), FIELD(0x12,facing),
  FIELD(0x10,momentum), FIELD(0x17,grip), FIELD(0x14,gravity), FIELD(0x19,off_gas),
  FIELD(0x1b,over_speed), FIELD(0x2e,spin_decay), FIELD(0x08,vx), FIELD(0x06,vx_fraction),
  FIELD(0x0c,vy), FIELD(0x0a,vy_fraction)
};
static void audit_ownership(unsigned pc) {
  bool ok=true;
  for (unsigned address=0;address<sizeof(ram);++address) {
    if (!ram_reads[address] && !ram_writes[address]) continue;
    bool known=address<0x30 || (address>=0x100 && address<0x200) ||
               (address>=0x200 && address<0xb00);
    for (unsigned i=0;i<sizeof(actor_fields)/sizeof(*actor_fields);++i)
      if (address>=actor_fields[i].address && address<actor_fields[i].address+actor_fields[i].size) known=true;
    /* Fixture-owned shared inputs and presentation-only accesses. $B11/$D52
     * are masked-off high bytes of M16 action/state reads. Launch's CPU speed writes
     * are deliberately discarded; the mine coordinates are returned events. */
    if (address==0x51 || address==0xd1 || address==0xdc8 || address==0xf50 || address==0xf51 ||
        address==0xb00 || address==0xc60 || address==0xb11 || address==0xd52 || address==0xf0 || address==0xe23 ||
        address==0xb23 || address==0xb25 || address==0xb27 ||
        (address>=0xcfa && address<=0xcfd)) known=true;
    if (pc==0xe5cf || pc==0xe67b || pc==0xde07 || pc==0xdd25 || pc==0xd9c8) {
      if (address>=0x30 && address<=0x3c) known=true;
      if (!ram_writes[address] && ((address>=0x1200 && address<0x1b00) || address==0xad ||
          address==0x10da || address==0x10d6 || address==0x10d8 || address==0x10ab || address==0x10b7 || address==0x1075)) known=true;
    }
    if (pc==0xdbc4 || pc==0xbd51 || pc==0xbc4a || pc==0xbe02 || pc==0x87a3 || pc==0x8808) {
      if (pc==0x87a3 && (address==0xf8 || address==0xf9)) known=true;
      if (address>=0x30 && address<=0x31) known=true;
      if ((address>=0x1172 && address<=0x1173) || (address>=0x1182 && address<=0x1183)) known=true;
      for (unsigned i=0;i<sizeof(actor_fields)/sizeof(*actor_fields);++i) {
        const Field *f=&actor_fields[i];
        if (f->address>=0xb00 && f->address<0xd98 && address>=f->address+2 && address<f->address+2+f->size) known=true;
      }
    }
    if (!known) {
      fprintf(stderr,"%06x unmapped RAM %05x read=%u write=%u\n",pc,address,ram_reads[address],ram_writes[address]);
      ok=false;
    }
  }
  CHECK(ok);
}
static unsigned field_value(const void *object, const Field *f) {
  unsigned value = 0;
  if (f->size == 1) value = *((const uint8_t *)object + f->offset);
  else { uint16_t word; memcpy(&word, (const uint8_t *)object + f->offset, 2); value = word; }
  return value;
}
static void write_fields(const void *object, const Field *fields, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    unsigned value = field_value(object, &fields[i]);
    ram[fields[i].address] = (uint8_t)value;
    if (fields[i].size == 2) ram[fields[i].address + 1] = (uint8_t)(value >> 8);
  }
}
static void compare_fields(const void *object, const Field *fields, size_t count, unsigned pc, unsigned sample) {
  for (size_t i = 0; i < count; ++i) {
    const Field *f = &fields[i];
    unsigned expected = ram[f->address];
    if (f->size == 2) expected |= (unsigned)ram[f->address + 1] << 8;
    unsigned actual = field_value(object, f);
    if (expected != actual) {
      fprintf(stderr, "%06x sample=%u machine=%u %s: stock=%04x adapter=%04x seed=%08x\n",
              pc, sample, ram[0x52], f->name, expected, actual, random_state);
      fprintf(stderr,"initial speed=%04x fraction=%04x action=%02x state=%02x ground=%02x slide=%02x jump=%02x top=%04x grip=%04x\n",
              initial_actor.speed,initial_actor.speed_fraction,initial_actor.action,initial_actor.state,
              initial_actor.ground_flags,initial_actor.slide,initial_actor.jump_timer,initial_scratch.top_speed,initial_scratch.grip);
      exit(1);
    }
  }
}
#define COUNT(a) (sizeof(a)/sizeof(*(a)))
static bool reference_carry;
static uint16_t reference_until(unsigned pc,unsigned stop) {
  Interp816 cpu = {0};
  cpu.mem = NULL; cpu.read = read_bus; cpu.write = write_bus;
  cpu.sp = 0x1fd; cpu.pc = (uint16_t)pc; cpu.k = (uint8_t)(pc >> 16);
  cpu.mf = cpu.xf = true; cpu.c = false;
  if (pc>>16) { cpu.sp=0x1fc;ram[0x1fd]=255; } /* RTL display entries */
  if (pc==0x980e) cpu.c=true; /* explicit inverse-angle interface */
  if (pc==0xfa1c) cpu.a=ram[0x66];
  if (pc==0xde07) cpu.y=ram[0xd00];
  if (pc==0xdbc4) { cpu.mf=false;cpu.x=2; }
  if (pc==0x87a3) cpu.x=2;
  if (pc==0xbc4a || pc==0xbe02) cpu.y=2;
  ram[0x1fe] = ram[0x1ff] = 255;
  unsigned steps = 0;
  do { CHECK(++steps < 10000); interp816_runOpcode(&cpu); }
  while (cpu.sp != 0x1ff && !(stop && cpu.k==0 && cpu.pc==stop &&
         (cpu.sp==0x1fd || (stop==0x99c0 && cpu.sp==0x1fb))));
  CHECK(stop && stop!=0x99c0 ? cpu.pc==stop : cpu.pc==0 || cpu.pc==stop);
  reference_carry=cpu.c;
  return cpu.a;
}
static uint16_t reference(unsigned pc) { return reference_until(pc,0); }
static void call_adapter(unsigned pc, FzeroActor *a, FzeroActorScratch *s, uint8_t phase) {
  FzeroActorTables t = {rom_read, NULL};
  switch (pc) {
    case 0x9377: FzeroActorSlide(a); break;
    case 0x91bc: FzeroActorTopSpeed(a,s,t); break;
    case 0x9264: FzeroActorGrip(a,s,t); break;
    case 0x930d: FzeroActorSpinState(a,s); break;
    case 0x938d: FzeroActorSpeed(a,s,t,phase); break;
    case 0x953a: FzeroActorApplySpin(a,s); break;
    case 0x9503: FzeroActorTurn(a,s); break;
    case 0x9589: FzeroActorApplyGrip(a,s); break;
    case 0x95e7: FzeroActorVelocity(s,t); break;
    case 0x9672: FzeroActorWind(a,s,t,phase); break;
    case 0x96f2: FzeroActorStoreVelocity(a,s); break;
    case 0x9753: FzeroActorMove(a,s); break;
    case 0x981c: FzeroActorDash(a); break;
    case 0xb7cb: FzeroActorInputAction(a,t); break;
    case 0xb94e: FzeroActorTurnStage(a); break;
    case 0xb881: FzeroActorTurnPerformance(a,t); break;
    case 0x9b4f: FzeroActorAltitude(a,s,t,ram[0x51]); break;
    case 0x98a4: FzeroActorDamageSurface(a,t); break;
    case 0x8e36: FzeroActorSurface(a); break;
    case 0x9d9e: { FzeroActorMineEvent event; FzeroActorMine(a,t,&event); break; }
    case 0xfa1c: FzeroActorPreRace(a,t); break;
    case 0x8d12: FzeroActorLaunch(a); break;
    case 0xb5ec: FzeroActorFailedStart(a); break;
    case 0xba13: FzeroActorSpinTrigger(a); break;
    case 0xba70: FzeroActorAnimation(a,t); break;
    case 0xb6cc: FzeroActorRecovery(a); break;
    case 0xb61b: FzeroActorPit(a,t); break;
    case 0xd9c8: FzeroActorRecoveryTrigger(a,ram,t); break;
    default: CHECK(false);
  }
}

static void test_world_sample(void) {
  memset(ram,0,sizeof(ram));
  for (unsigned i=0;i<0x2000;++i) ram[0x10000+i]=(uint8_t)random_word();
  ram[0xb0]=0; ram[0xb1]=0x80; ram[0xb2]=0x7f;
  for (unsigned block=0;block<256;++block) for (unsigned row=0;row<16;++row) {
    unsigned pointer=0x2000+(random_word()%128)*32;
    unsigned address=0x15000+block*32+row*2;
    ram[address]=(uint8_t)pointer; ram[address+1]=(uint8_t)(pointer>>8);
  }
  for (unsigned row=0;row<128;++row) for (unsigned group=0;group<16;++group) {
    unsigned pointer=(random_word()%2048)*4;
    unsigned address=0x12000+row*32+group*2;
    ram[address]=(uint8_t)pointer; ram[address+1]=(uint8_t)(pointer>>8);
  }
  for (unsigned i=0;i<512;++i) ram[0x18000+i]=(uint8_t)random_word();
  for (unsigned i=0;i<40000;++i) {
    unsigned x=random_word()&8191,y=random_word()&4095;
    ram[0x12]=(uint8_t)x;ram[0x13]=(uint8_t)(x>>8);
    ram[0x14]=(uint8_t)y;ram[0x15]=(uint8_t)(y>>8);
    unsigned actual=FzeroActorSampleSurface(ram,(uint16_t)x,(uint16_t)y);
    CHECK((reference(0xa1e5)&255)==actual);
  }
  puts("00a1e5: 40000 full-world sampling fixtures passed");
}

static void test_sequences(void) {
  const unsigned input_routines[]={0xb61b,0xb94e,0xb7cb,0xb881,0xba13,0xb5ec};
  const unsigned movement_routines[]={0x9377,0x98a4,0x981c,0x91bc,0x9264,0x930d,
                                     0x9b4f,0x938d,0x953a,0x9503,0x9589,0x95e7,0x9672,0x96f2,0x9753,0xba70};
  uint64_t hash=UINT64_C(14695981039346656037);
  for (unsigned machine=0;machine<4;++machine) {
    FzeroActor a={0}; FzeroActorScratch s;
    a.machine=(uint8_t)machine; a.health=0x800; a.facing=a.momentum=0x1800;
    a.acceleration_index=rom_read(NULL,0xfa79+machine); a.turning_index=rom_read(NULL,0xfa7d+machine);
    a.x=8190; a.y=2; a.boosts=3;
    memset(ram,0,sizeof(ram)); write_fields(&a,actor_fields,COUNT(actor_fields));
    ram[0xd1]=machine*2;ram[0xdc8]=1;
    uint16_t held=0;
    for (unsigned tick=0;tick<12000;++tick) {
      /* Sustained throttle, taps, reversals, shoulder combinations, airborne
       * steering and neutral intervals; all generated solely as inputs. */
      if (tick%37==0) held=(uint16_t)((random_word()&0x0fb0)|(tick%222?0x8000:0));
      uint16_t previous=(uint16_t)(ram[0x65]|(unsigned)ram[0x66]<<8);
      uint16_t pressed=held&(uint16_t)~previous,released=previous&(uint16_t)~held;
      ram[0x65]=(uint8_t)held;ram[0x66]=(uint8_t)(held>>8);
      ram[0x67]=(uint8_t)pressed;ram[0x68]=(uint8_t)(pressed>>8);
      ram[0x69]=(uint8_t)released;ram[0x6a]=(uint8_t)(released>>8);
      FzeroActorLatch(&a,held);
      /* Terrain is an input to this routine sequence, not a fabricated race
       * outcome. World sampling and complete host ticks have separate gates. */
      if (tick%43==0) {
        static const uint8_t tiles[]={0x82,0x9c,0xa0,0xa6,0xa7,0xb0,0xba,0xc3,0x72,0x68,0xd1};
        a.surface_id=ram[0xcd0]=tiles[(tick/43)%COUNT(tiles)];
      }
      a.surface_flags=ram[0xc7]=0;
      FzeroActorSurface(&a); reference(0x8e36);
      for (unsigned r=0;r<COUNT(input_routines);++r) {
        call_adapter(input_routines[r],&a,&s,a.race_state); reference(input_routines[r]);
      }
      FzeroActorScratchReset(&s);
      ram[0x19]=4;ram[0x1a]=0;ram[0x1b]=24;ram[0x1c]=0;ram[0x14]=24;ram[0x15]=0;
      ram[0x51]=(uint8_t)tick;
      for (unsigned r=0;r<COUNT(movement_routines);++r) {
        call_adapter(movement_routines[r],&a,&s,a.race_state); reference(movement_routines[r]);
      }
      if (a.no_control&255) --a.no_control;
      if (ram[0xd20]) --ram[0xd20];
      if (a.damage_cooldown) --a.damage_cooldown;
      if (ram[0xc8]) --ram[0xc8];
      initial_actor=a;initial_scratch=s;
      compare_fields(&a,actor_fields,COUNT(actor_fields),0x9144,tick);
      compare_fields(&s,scratch_fields,COUNT(scratch_fields),0x9144,tick);
      /* Canonical field order and little-endian values, without struct padding. */
      for (unsigned i=0;i<COUNT(actor_fields);++i) {
        unsigned value=field_value(&a,&actor_fields[i]);
        for (unsigned byte=0;byte<actor_fields[i].size;++byte) {
          hash^=(value>>(byte*8))&255;hash*=UINT64_C(1099511628211);
        }
      }
    }
  }
  printf("48000 chained actor ticks: %016llx\n",(unsigned long long)hash);
}
static void randomize(FzeroActor *a, FzeroActorScratch *s, unsigned machine) {
  memset(a,0,sizeof(*a)); FzeroActorScratchReset(s);
  a->machine = (uint8_t)machine;
  a->speed = random_word() % 0x1000; a->speed_fraction = random_word();
  a->x = random_word() & 0x1fff; a->y = random_word() & 0xfff;
  a->x_fraction = random_word(); a->y_fraction = random_word();
  a->facing = random_word() % 0xc000; a->momentum = random_word() % 0xc000;
  a->turning = random_word() % 0x1000; a->spin = random_word() % 0x1000 | (random_word() & 0x8000);
  a->no_control = random_word() & 31; a->health = random_word() % 0x801;
  a->action = (uint8_t)random_word(); a->state = (uint8_t)random_word();
  a->surface = (uint8_t)random_word(); a->ground_flags = (uint8_t)random_word();
  a->surface_flags = (uint8_t)random_word(); a->checkpoint_angle = random_word() % 192;
  a->slide = random_word() % 97; a->instant_grip = random_word() & 3;
  a->jump_timer = random_word() & 31; a->sound_flags = (uint8_t)random_word();
  a->acceleration_index = rom_read(NULL, 0xfa79 + machine);
  a->turning_index = rom_read(NULL, 0xfa7d + machine);
  a->held = random_word() & 0xfff0; a->pressed = random_word() & a->held;
  a->released = random_word() & (uint16_t)~a->held;
  a->turn_stage = random_word() % 0x2000 | (random_word() & 0x8000);
  a->turn_acceleration = random_word() % 0x800;
  a->steering_history = (uint8_t)random_word();
  a->boosts = random_word() % 4; a->boost_timer = (uint8_t)random_word(); a->boost_award = random_word() & 3;
  a->surface_id = (uint8_t)random_word(); a->ground_timer = (uint8_t)random_word();
  a->boundary_momentum = random_word() % 0xc000;
  a->height = random_word(); a->height_velocity = random_word();
  a->extra_height_index = random_word() % 7; a->extra_height = random_word() % 5;
  a->exhaust = random_word() & 1 ? 0x80 : random_word() & 1;
  a->damage_cooldown = random_word() & 1 ? 0 : 9;
  a->electric_flash = random_word() & 1 ? 0 : 8;
  a->track_flash = random_word() & 1 ? 0 : 9;
  a->effect_timer = (uint8_t)random_word(); a->animation = (uint8_t)random_word();
  a->recovery_active = random_word() % 16 == 0;
  a->boost_beep_guard = random_word() & 1;
  if (random_word() & 1) a->health = (uint16_t)(0u - random_word() % 256 - 1);
  a->recovery_x=random_word()&8191;a->recovery_y=random_word()&4095;
  a->recovery_angle=random_word()%192;a->pit_phase=random_word()%36;
  a->pit_departure=random_word()%17;a->beam_top=(uint8_t)random_word();a->beam_bottom=(uint8_t)random_word();
  a->jump_watch=random_word()&1?255:0;a->takeoff_x=random_word()&8191;a->takeoff_y=random_word()&4095;
  a->landing_x=random_word()&8191;a->landing_y=random_word()&4095;
  s->top_speed = random_word() % 0x1000; s->turn = random_word() % 0x1000;
  s->facing = random_word() % 0xc000; s->momentum = random_word() % 0xc000;
  s->speed = random_word() % 0x1000; s->grip = random_word() % 0x1000;
  s->spin_decay = random_word() & 31;
  s->vx = random_word(); s->vy = random_word();
  s->vx_fraction = random_word(); s->vy_fraction = random_word();
}
static void test_progress(void) {
  const unsigned routines[]={0xe5cf,0xe67b,0xde07,0xdd25};
  FzeroActorTables t={rom_read,NULL};
  for (unsigned r=0;r<COUNT(routines);++r) {
    memset(ram_reads,0,sizeof(ram_reads));memset(ram_writes,0,sizeof(ram_writes));audit_enabled=true;
    for (unsigned sample=0;sample<40000;++sample) {
      FzeroActor a;FzeroActorScratch s;randomize(&a,&s,sample%4);
      a.next_checkpoint_x=random_word()&8191;a.next_checkpoint_y=random_word()&4095;
      a.previous_checkpoint_x=random_word()&8191;a.previous_checkpoint_y=random_word()&4095;
      a.next_checkpoint_angle=random_word()%192;a.previous_checkpoint_angle=random_word()%192;
      a.next_checkpoint_flags=(uint8_t)random_word();a.previous_checkpoint_flags=(uint8_t)random_word();
      a.checkpoint=random_word()%32;a.status2=(uint8_t)random_word();a.branch=(uint8_t)random_word();
      a.wall_angle=random_word()%192;a.finish_side=(uint8_t)random_word();
      if (sample%8==0) a.x=a.next_checkpoint_x=a.previous_checkpoint_x;
      if (sample%8==1) a.y=a.next_checkpoint_y=a.previous_checkpoint_y;
      if (sample%8==2) { a.x=a.next_checkpoint_x=a.previous_checkpoint_x;a.y=a.next_checkpoint_y=a.previous_checkpoint_y; }
      memset(ram,0,sizeof(ram));write_fields(&a,actor_fields,COUNT(actor_fields));
      ram[0xb00]=0x88;ram[0xad]=31;ram[0x51]=(uint8_t)random_word();
      ram[0x10da]=random_word()&1;ram[0x10d6]=4;ram[0x10d8]=12;
      for (unsigned i=0;i<256;++i) {
        unsigned x=random_word()&8191,y=random_word()&4095;
        ram[0x1200+i*2]=(uint8_t)x;ram[0x1201+i*2]=(uint8_t)(x>>8);
        ram[0x1400+i*2]=(uint8_t)y;ram[0x1401+i*2]=(uint8_t)(y>>8);
        ram[0x1600+i]=random_word()%192;ram[0x1700+i]=(uint8_t)random_word();
      }
      initial_actor=a;initial_scratch=s;
      switch (routines[r]) {
        case 0xe5cf:FzeroActorCheckpointForward(&a,ram,t);reference(0xe5cf);break;
        case 0xe67b:FzeroActorCheckpointBackward(&a,ram,t);reference(0xe67b);break;
        case 0xde07:FzeroActorCheckpointInitialize(&a,ram);reference(0xde07);break;
        case 0xdd25:FzeroActorProgressTick(&a,ram,t,ram[0x51]);reference_until(0xdd25,0xdda3);break;
      }
      compare_fields(&a,actor_fields,COUNT(actor_fields),routines[r],sample);
    }
    audit_enabled=false;audit_ownership(routines[r]);
    printf("%06x: 40000 human checkpoint/boundary fixtures passed\n",routines[r]);fflush(stdout);
  }
}
static unsigned ram_word(unsigned address) { return ram[address]|(unsigned)ram[address+1]<<8; }
static void set_word(unsigned address,unsigned value) { ram[address]=(uint8_t)value;ram[address+1]=(uint8_t)(value>>8); }
static void pair_fields(const FzeroActor *a,const FzeroActor *b) {
  memset(ram,0,sizeof(ram));write_fields(a,actor_fields,COUNT(actor_fields));
  set_word(0xb22,b->speed);set_word(0xbe2,b->momentum);set_word(0xbd2,b->facing);
  set_word(0xc22,b->spin);set_word(0xd22,b->no_control);ram[0xd53]=b->state;
  set_word(0xb72,b->x);set_word(0xb92,b->y);ram[0xd1]=a->machine*2;
}
static void test_contact(void) {
  FzeroActorTables t={rom_read,NULL};
  memset(ram_reads,0,sizeof(ram_reads));memset(ram_writes,0,sizeof(ram_writes));audit_enabled=true;
  for (unsigned sample=0;sample<80000;++sample) {
    FzeroActor a,b;FzeroActorScratch s;randomize(&a,&s,sample%4);randomize(&b,&s,(sample/4)%4);
    /* Include near contacts, projection edges, toroidal seams and the full map. */
    if (sample%2) { b.x=(a.x+random_word()%257-128)&8191;b.y=(a.y+random_word()%257-128)&4095; }
    pair_fields(&a,&b);
    unsigned angle=a.facing>>8;angle=angle?192-angle:0;
    ram[0x14]=rom_read(NULL,0x08ec00+angle);ram[0x15]=rom_read(NULL,0x08ecf0+angle);
    ram[0x16]=rom_read(NULL,0x08ec30+angle);ram[0x17]=rom_read(NULL,0x08ed20+angle);
    FzeroActorProjection p=FzeroActorProject(&a,&b,t);
    unsigned projected=reference(0xdbc4);
    CHECK((uint16_t)p.right==ram_word(0x1172));CHECK((uint16_t)p.forward==ram_word(0x1182));
    CHECK(p.stock_visible==!reference_carry);
    if (p.projectable) { CHECK((uint16_t)p.x==projected);CHECK((unsigned)p.y==ram_word(0x2c)); }
  }
  audit_enabled=false;audit_ownership(0xdbc4);
  puts("00dbc4: 80000 physical projection fixtures passed");
  const unsigned routines[]={0xbd51,0xbc4a,0xbe02,0x87a3,0x8808};
  for (unsigned r=0;r<COUNT(routines);++r) {
    memset(ram_reads,0,sizeof(ram_reads));memset(ram_writes,0,sizeof(ram_writes));audit_enabled=true;
    for (unsigned sample=0;sample<40000;++sample) {
      FzeroActor a,b;FzeroActorScratch s;randomize(&a,&s,sample%4);randomize(&b,&s,(sample/4)%4);
      unsigned geometry=(sample&3)*2,separation=24+sample%16;
      /* Stock's second slot is a CPU. A human already spinning has its own
       * explicit response override; compare the shared stock branch here. */
      b.state&=(uint8_t)~0x40;
      pair_fields(&a,&b);ram[0x13]=(uint8_t)geometry;ram[0x14]=(uint8_t)separation;
      ram[0x11]=0;ram[0x12]=2;
      initial_actor=a;initial_scratch=s;
      switch (routines[r]) {
        case 0xbd51:FzeroActorContactSpeed(&a,&b);break;
        case 0xbc4a:FzeroActorContactLateral(&a,&b,t,(uint8_t)geometry,separation);break;
        case 0xbe02:FzeroActorContactSpin(&a,&b,t,(uint8_t)geometry);break;
        case 0x87a3:FzeroActorDamageContact(&a,&b,t,true);break;
        case 0x8808:FzeroActorDamageContact(&a,&b,t,false);break;
      }
      reference(routines[r]);compare_fields(&a,actor_fields,COUNT(actor_fields),routines[r],sample);
      CHECK(b.speed==ram_word(0xb22));CHECK(b.momentum==ram_word(0xbe2));CHECK(b.no_control==ram_word(0xd22));
      CHECK(b.state==ram[0xd53]);
      if (routines[r]!=0xbe02) CHECK(b.spin==ram_word(0xc22));
    }
    audit_enabled=false;audit_ownership(routines[r]);
    printf("%06x: 40000 contact component fixtures passed\n",routines[r]);fflush(stdout);
  }
  for (unsigned ma=0;ma<4;++ma) for (unsigned mb=0;mb<4;++mb) {
    FzeroActor a={0},b={0};
    a.machine=(uint8_t)ma;b.machine=(uint8_t)mb;a.health=b.health=0x800;
    a.x=b.x=8190;a.y=4094;b.y=4086;a.speed=0x980;b.speed=0x780;
    FzeroActor original_a=a,original_b=b;
    uint8_t contact=0;
    CHECK(FzeroActorResolveContact(&a,&b,t,&contact) && contact==2);
    CHECK(a.health<0x800 && b.health<0x800 && a.damage_cooldown==9 && b.damage_cooldown==9);
    CHECK((a.state&0x40) && (b.state&0x40));
    FzeroActor once_a=a,once_b=b;
    CHECK(FzeroActorResolveContact(&a,&b,t,&contact));
    CHECK(!memcmp(&a,&once_a,sizeof(a)) && !memcmp(&b,&once_b,sizeof(b)));
    /* Translate across both toroidal seams. Physical response must agree. */
    FzeroActor wrap_a=original_a,wrap_b=original_b;
    wrap_a.x=wrap_b.x=2;wrap_a.y=2;wrap_b.y=4090;
    uint8_t wrap_contact=0;
    CHECK(FzeroActorResolveContact(&wrap_a,&wrap_b,t,&wrap_contact));
    wrap_a.x=a.x;wrap_a.y=a.y;wrap_b.x=b.x;wrap_b.y=b.y;
    CHECK(!memcmp(&a,&wrap_a,sizeof(a)) && !memcmp(&b,&wrap_b,sizeof(b)));
    b.x=(uint16_t)((a.x+1024)&8191);CHECK(!FzeroActorResolveContact(&a,&b,t,&contact) && !contact);
    a=original_a;b=original_b;b.height=0x1000;
    CHECK(!FzeroActorResolveContact(&a,&b,t,&contact));
    CHECK(a.health==0x800 && b.health==0x800);
    b.height=0;
    bool found=false;
    for (unsigned dx=1;dx<100;++dx) {
      a=original_a;b=original_b;b.x=(uint16_t)((a.x+dx)&8191);
      FzeroActorProjection p=FzeroActorProject(&a,&b,t);
      if (p.x-128<24 || p.x-128>=40) continue;
      contact=0;CHECK(FzeroActorResolveContact(&a,&b,t,&contact) && contact==1);
      CHECK(a.health<0x800 && b.health<0x800 && a.damage_cooldown==8 && b.damage_cooldown==8);
      CHECK(a.momentum!=original_a.momentum && b.momentum!=original_b.momentum);
      found=true;break;
    }
    CHECK(found);
  }
  puts("two-human contact: all 16 machine pairs, latch, seams, altitude, direct/lateral responses passed");
}
static void test_finish(void) {
  for (unsigned sample=0;sample<40000;++sample) {
    memset(ram,0,sizeof(ram));
    FzeroActor a={0};a.x=random_word()&8191;a.y=random_word()&4095;
    a.finish_side=(uint8_t)((sample%3)*0x40);a.lap=2;
    ram[0xae]=random_word()&31;ram[0xaf]=random_word()&15;ram[0xf53]=2;
    if (sample%2) {
      a.x=(uint16_t)(ram[0xae]*256+(sample%4==1?127:128));
      a.y=(uint16_t)(((ram[0xaf]+sample%2)*256+random_word()%256)&4095);
    }
    write_fields(&a,actor_fields,COUNT(actor_fields));
    int crossing=FzeroActorCrossFinish(&a,ram);
    reference_until(0x9968,0x99c0);
    /* Forward entry stops at $99C0 before stock lives/records code; the
     * unconditional $99B6 continuation sets the final side to $40. */
    if (ram[0xd40]==3) { CHECK(ram[0xd41]==0x80 && a.finish_side==0x40); }
    else CHECK(a.finish_side==ram[0xd41]);
    CHECK(a.lap==2);CHECK((int)ram[0xd40]==2+crossing);
  }
  puts("009968: 40000 finish-strip fixtures passed without stock rank/lives transitions");
}
static void test_mine_commit(void) {
  uint8_t *world=malloc(sizeof(ram));CHECK(world);
  for (unsigned sample=0;sample<10000;++sample) {
    memset(ram,0,sizeof(ram));ram[0xb1]=0x80;ram[0xb2]=0x7f;
    for (unsigned i=0;i<512;++i) ram[0x18000+i]=(uint8_t)random_word();
    for (unsigned i=0;i<4096;++i) set_word(0x15000+i*2,0x2000+(random_word()%128)*32);
    for (unsigned i=0;i<2048;++i) set_word(0x12000+i*2,random_word()&0x1ffc);
    FzeroActorMineEvent event={true,random_word()&8191,random_word()&4095,(uint8_t)(0xc8+sample%4)};
    set_word(0xcfa,event.x);set_word(0xcfc,event.y);ram[0xcd0]=event.surface_id;
    memcpy(world,ram,sizeof(ram));FzeroActorCommitMine(world,&event);reference(0xa38b);
    CHECK(!memcmp(world+0x10000,ram+0x10000,0x10000));
  }
  free(world);puts("00a38b: 10000 whole-course mine mutation fixtures passed");
}
static void flat_course(void) {
  /* An explicit synthetic course tests the session state machine. It is not
   * evidence of successful races on any of the fifteen stock courses. */
  memset(ram,0,sizeof(ram));ram[0xb1]=0x80;ram[0xb2]=0x7f;ram[0xad]=3;ram[0xae]=8;ram[0xaf]=8;
  for (unsigned row=0;row<16;++row) set_word(0x15000+row*2,0x2000);
  for (unsigned column=0;column<16;++column) set_word(0x12000+column*2,0x3000);
  memset(ram+0x13000,0x82,4);
  for (unsigned i=0;i<4;++i) {
    set_word(0x1200+i*2,2048+i*1024);set_word(0x1400+i*2,2048);
    ram[0x1600+i]=48;ram[0x1700+i]=4;
  }
}
static void session_roundtrip(FzeroVersus *s,FzeroVersus *copy) {
  size_t size=FzeroVersusSaveSize();uint8_t *data=malloc(size),*after=malloc(size);CHECK(data && after);
  uint64_t expected=FzeroVersusHash(s);CHECK(FzeroVersusSave(s,data,size));
  CHECK(FzeroVersusRestore(copy,data,size,NULL));CHECK(FzeroVersusHash(copy)==expected);
  CHECK(FzeroVersusSave(copy,after,size) && !memcmp(data,after,size));
  for (unsigned i=0;i<64;++i) {
    unsigned offset=(unsigned)(i*(size-1)/63);data[offset]^=0x80;
    CHECK(!FzeroVersusRestore(copy,data,size,NULL) && FzeroVersusHash(copy)==expected);
    data[offset]^=0x80;
  }
  CHECK(!FzeroVersusRestore(copy,data,size-1,NULL) && FzeroVersusHash(copy)==expected);
  FzeroVersusConfig wrong=FzeroVersusCompleted(s)->config;wrong.course=(wrong.course+1)%15;
  CHECK(!FzeroVersusRestore(copy,data,size,&wrong) && FzeroVersusHash(copy)==expected);
  free(data);free(after);
}
static void test_session(void) {
  flat_course();FzeroActor spawns[2]={{0},{0}};
  spawns[0].x=2160;spawns[1].x=2120;spawns[0].y=spawns[1].y=2144;
  spawns[0].facing=spawns[1].facing=0x9000;
  FzeroActorTables t={rom_read,NULL};uint64_t hash=0;
  for (unsigned machine=0;machine<4;++machine) {
    FzeroVersusConfig c={0,{(uint8_t)machine,(uint8_t)((machine+1)%4)}};
    FzeroVersus *s=FzeroVersusCreate(c,ram,spawns,t),*copy=FzeroVersusCreate(c,ram,spawns,t);CHECK(s && copy);
    const FzeroVersusSnapshot *v=FzeroVersusCompleted(s);uint16_t held[2]={0x8000,0};
    CHECK(!FzeroVersusTick(s,held));session_roundtrip(s,copy);
    FzeroVersusReady(s,0,true);CHECK(v->phase==FZERO_VS_READY);
    FzeroVersusReady(s,1,true);CHECK(v->phase==FZERO_VS_COUNTDOWN);
    for (unsigned i=0;i<179;++i) CHECK(FzeroVersusTick(s,held));
    CHECK(v->countdown==1 && v->race_tick==0);session_roundtrip(s,copy);
    CHECK(FzeroVersusTick(s,held) && v->phase==FZERO_VS_RACING);
    FzeroVersusPause(s,FZERO_VS_PAUSE_DEVICE2);uint64_t paused=FzeroVersusHash(s);
    for (unsigned i=0;i<120;++i) CHECK(!FzeroVersusTick(s,held));
    CHECK(FzeroVersusHash(s)==paused && !v->racers[0].actor.held && !v->racers[1].actor.held);
    session_roundtrip(s,copy);FzeroVersusResume(s);
    CHECK(FzeroVersusTick(s,held));CHECK(v->racers[0].actor.action!=v->racers[1].actor.action);
    session_roundtrip(s,copy);
    for (unsigned i=0;i<3600;++i) {
      held[0]=(uint16_t)(0x8000|(i%360<60?0x0200:0)|(i%180<30?0x20:0));
      held[1]=(uint16_t)(i%1000<700?0x8000:0x4000);
      bool advanced=FzeroVersusTick(s,held);CHECK(FzeroVersusTick(copy,held)==advanced);
      if (i%600==0) CHECK(FzeroVersusHash(s)==FzeroVersusHash(copy));
    }
    CHECK(v->racers[0].actor.x!=v->racers[1].actor.x || v->racers[0].actor.y!=v->racers[1].actor.y);
    held[0]=0;held[1]=0x1000;CHECK(!FzeroVersusTick(s,held) && v->pause==FZERO_VS_PAUSE_MANUAL);
    FzeroVersusEndRace(s);CHECK(v->phase==FZERO_VS_RESULTS && v->winner==FZERO_VS_NO_WINNER);
    session_roundtrip(s,copy);CHECK(FzeroVersusRematch(s) && v->phase==FZERO_VS_READY && !v->tick);
    CHECK(v->config.machine[0]==machine && v->racers[0].actor.x==spawns[0].x);
    CHECK(!memcmp(FzeroVersusWorld(s),ram,sizeof(ram)));session_roundtrip(s,copy);
    hash^=FzeroVersusHash(s);FzeroVersusDestroy(copy);FzeroVersusDestroy(s);
  }
  printf("session: ready/countdown, independent controls, pause, deterministic continuation, atomic restore, end/rematch passed; hash=%016llx\n",(unsigned long long)hash);
}
static void test_session_finishes(void) {
  flat_course();
  const unsigned gate_x[]={6000,6500,2000,1500},gate_y[]={1200,2800,3200,1500};
  for (unsigned i=0;i<4;++i) {
    set_word(0x1200+i*2,gate_x[i]);set_word(0x1400+i*2,gate_y[i]);
    ram[0x1600+i]=(uint8_t)((i*48+48)%192);ram[0x1700+i]=(uint8_t)(i*2);
  }
  ram[0xad]=4;set_word(0x1208,2176);set_word(0x1408,1000);ram[0x1604]=48;ram[0x1704]=0;
  ram[0xaf]=3;
  FzeroActor spawns[2]={{0},{0}};
  spawns[0].x=spawns[1].x=2160;spawns[0].y=1000;spawns[1].y=1192;
  spawns[0].facing=spawns[1].facing=0x3000;
  FzeroActorTables t={rom_read,NULL};uint16_t held[2]={0x8000,0x8000};
  for (unsigned race=0;race<2;++race) {
    FzeroVersusConfig config={0,{0,(uint8_t)(race?3:0)}};
    FzeroVersus *s=FzeroVersusCreate(config,ram,spawns,t),*copy=FzeroVersusCreate(config,ram,spawns,t);CHECK(s && copy);
    FzeroVersusReady(s,0,true);FzeroVersusReady(s,1,true);
    const FzeroVersusSnapshot *v=FzeroVersusCompleted(s);
    bool one_finished=false,pending_seen=false;FzeroActor terminal={0};unsigned finished_player=0;
    unsigned waypoint[2]={0};
    for (unsigned i=0;i<24000 && v->phase!=FZERO_VS_RESULTS;++i) {
      /* Drive only ordinary pad words around an explicit synthetic rectangle;
       * no actor pose, gate, lap, time or terminal state is injected. */
      if (v->phase==FZERO_VS_RACING) for(unsigned player=0;player<2;++player) {
        const int target_x[]={6500,6500,1500,1500},target_y[]={1000,3200,3200,1000};
        const FzeroActor *a=&v->racers[player].actor;
        int dx=target_x[waypoint[player]]-a->x,dy=target_y[waypoint[player]]+(int)player*192-a->y;
        if (dx*dx+dy*dy<400*400) {
          waypoint[player]=(waypoint[player]+1)%4;
          dx=target_x[waypoint[player]]-a->x;dy=target_y[waypoint[player]]+(int)player*192-a->y;
        }
        int best=INT32_MIN;unsigned target=0;
        for (unsigned angle=0;angle<192;++angle) {
          int sx=rom_read(NULL,0x08ec00+angle),sy=rom_read(NULL,0x08ec30+angle);
          if (rom_read(NULL,0x08ecf0+angle)) sx=-sx;
          if (!rom_read(NULL,0x08ed20+angle)) sy=-sy;
          int dot=dx*sx+dy*sy;if(dot>best) { best=dot;target=angle; }
        }
        unsigned error=(target+192-(a->facing>>8))%192;
        held[player]=(uint16_t)(0x8000|(error>1 && error<96?0x100:error>96 && error<191?0x200:0));
      }
      CHECK(FzeroVersusTick(s,held));
      if (pending_seen) {
        CHECK(FzeroVersusTick(copy,held));
        if(i%120==0) CHECK(FzeroVersusHash(s)==FzeroVersusHash(copy));
      }
      if (!pending_seen && (v->racers[0].crossing_tick || v->racers[1].crossing_tick)) {
        pending_seen=true;session_roundtrip(s,copy);
      }
      if (!one_finished && v->racers[0].status!=v->racers[1].status) {
        finished_player=v->racers[0].status==FZERO_VS_FINISHED?0:1;
        terminal=v->racers[finished_player].actor;one_finished=true;session_roundtrip(s,copy);
      } else if(one_finished) CHECK(!memcmp(&terminal,&v->racers[finished_player].actor,sizeof(terminal)));
    }
    if(v->phase!=FZERO_VS_RESULTS) fprintf(stderr,"synthetic race=%u tick=%llu laps=%u/%u gates=%d/%d cp=%u/%u started=%u/%u x=%u/%u y=%u/%u\n",race,(unsigned long long)v->race_tick,v->racers[0].completed_laps,v->racers[1].completed_laps,v->racers[0].gates,v->racers[1].gates,v->racers[0].actor.checkpoint,v->racers[1].actor.checkpoint,v->racers[0].started,v->racers[1].started,v->racers[0].actor.x,v->racers[1].actor.x,v->racers[0].actor.y,v->racers[1].actor.y);
    CHECK(v->phase==FZERO_VS_RESULTS && v->racers[0].completed_laps==5 && v->racers[1].completed_laps==5);
    CHECK(pending_seen);
    CHECK(FzeroVersusHash(s)==FzeroVersusHash(copy));
    CHECK(race ? one_finished && v->winner!=FZERO_VS_TIE : v->winner==FZERO_VS_TIE);
    CHECK(v->racers[0].actor.boosts==3 && v->racers[1].actor.boosts==3);
    session_roundtrip(s,copy);FzeroVersusDestroy(copy);FzeroVersusDestroy(s);
  }
  puts("synthetic whole-session races: five ordered laps, pending final-gate restore, same-tick tie, one-finished continuation, frozen terminal pose, capped lap boosts passed");
}
static uint16_t chase(const FzeroActor *a,const FzeroActor *b) {
  int dx=((b->x-a->x+4096)&8191)-4096,dy=((b->y-a->y+2048)&4095)-2048;
  int best=INT32_MIN;unsigned target=0;
  for (unsigned angle=0;angle<192;++angle) {
    int sx=rom_read(NULL,0x08ec00+angle),sy=rom_read(NULL,0x08ec30+angle);
    if (rom_read(NULL,0x08ecf0+angle)) sx=-sx;
    if (!rom_read(NULL,0x08ed20+angle)) sy=-sy;
    int dot=dx*sx+dy*sy;if(dot>best) { best=dot;target=angle; }
  }
  unsigned error=(target+192-(a->facing>>8))%192;
  unsigned throttle=error>12 && error<180 && a->speed>0x300?0x4000:0x8000;
  return (uint16_t)(throttle|(error>1 && error<96?0x100:error>96 && error<191?0x200:0));
}
static void test_session_contact_dnf(void) {
  flat_course();FzeroActor spawns[2]={{0},{0}};
  spawns[0].x=1000;spawns[1].x=1300;spawns[0].y=spawns[1].y=1000;
  spawns[0].facing=spawns[1].facing=0x3000;
  FzeroVersus *s=FzeroVersusCreate((FzeroVersusConfig){0,{0,0}},ram,spawns,(FzeroActorTables){rom_read,NULL});CHECK(s);
  FzeroVersusReady(s,0,true);FzeroVersusReady(s,1,true);
  const FzeroVersusSnapshot *v=FzeroVersusCompleted(s);bool terminal=false;unsigned contacts=0;
  for (unsigned tick=0;tick<100000 && !terminal;++tick) {
    uint16_t held[2]={chase(&v->racers[0].actor,&v->racers[1].actor),0};
    CHECK(FzeroVersusTick(s,held));if(v->contact)++contacts;
    for (unsigned i=0;i<2;++i) if (v->racers[i].actor.race_state&0x40) {
      CHECK(v->racers[i].status==FZERO_VS_DNF);terminal=true;
    }
  }
  if(!terminal)fprintf(stderr,"contact DNF not reached: contacts=%u health=%04x/%04x positions=%u,%u/%u,%u\n",contacts,v->racers[0].actor.health,v->racers[1].actor.health,v->racers[0].actor.x,v->racers[0].actor.y,v->racers[1].actor.x,v->racers[1].actor.y);
  CHECK(terminal && contacts>10);FzeroVersusDestroy(s);
  puts("session: repeated input-driven contacts produce DNF on the damage tick, before finish awards");
}
static void test_session_events(void) {
  FzeroActorTables t={rom_read,NULL};uint16_t held[2]={0,0};FzeroActor spawns[2]={{0},{0}};
  spawns[0].x=1000;spawns[1].x=1008;spawns[0].y=spawns[1].y=1000;
  for (unsigned mine=0;mine<2;++mine) {
    flat_course();
    if (mine) { memset(ram+0x13000,0xc8,4);memset(ram+0x13ff0,0x82,16); }
    FzeroVersusConfig c={0,{0,1}};
    FzeroVersus *s=FzeroVersusCreate(c,ram,spawns,t),*copy=FzeroVersusCreate(c,ram,spawns,t);CHECK(s && copy);
    FzeroVersusReady(s,0,true);FzeroVersusReady(s,1,true);
    for (unsigned i=0;i<FZERO_VERSUS_COUNTDOWN+1;++i) CHECK(FzeroVersusTick(s,held));
    const FzeroVersusSnapshot *v=FzeroVersusCompleted(s);
    if (mine) CHECK(v->mines[0].hit && v->mines[1].hit && memcmp(FzeroVersusWorld(s),ram,sizeof(ram)));
    else CHECK(v->contact==2 && (v->racers[0].actor.state&0x40) && (v->racers[1].actor.state&0x40));
    session_roundtrip(s,copy);
    for(unsigned i=0;i<600;++i) CHECK(FzeroVersusTick(s,held)==FzeroVersusTick(copy,held));
    CHECK(FzeroVersusHash(s)==FzeroVersusHash(copy));
    FzeroVersusEndRace(s);CHECK(FzeroVersusRematch(s) && !memcmp(FzeroVersusWorld(s),ram,sizeof(ram)));
    FzeroVersusDestroy(copy);FzeroVersusDestroy(s);
  }
  flat_course();
  /* One stationary racer stands on an electric edge; the other is on road.
   * Ordinary neutral input lets real surface damage produce the DNF. */
  for (unsigned i=256;i<512;++i) ram[0x18000+i]=1;
  for (unsigned row=0;row<16;++row) set_word(0x15020+row*2,0x2100);
  for (unsigned col=0;col<16;++col) set_word(0x12100+col*2,0x3100);
  spawns[1].y=3000;
  FzeroVersusConfig c={0,{0,0}};
  FzeroVersus *s=FzeroVersusCreate(c,ram,spawns,t),*copy=FzeroVersusCreate(c,ram,spawns,t);CHECK(s && copy);
  const FzeroVersusSnapshot *v=FzeroVersusCompleted(s);FzeroVersusReady(s,0,true);FzeroVersusReady(s,1,true);
  for(unsigned i=0;i<12000 && v->racers[1].status==FZERO_VS_ACTIVE;++i) CHECK(FzeroVersusTick(s,held));
  CHECK(v->racers[1].status==FZERO_VS_DNF && v->racers[0].status==FZERO_VS_ACTIVE && v->phase==FZERO_VS_RACING);
  FzeroActor terminal=v->racers[1].actor;uint64_t tick=v->race_tick;
  session_roundtrip(s,copy);
  for(unsigned i=0;i<300;++i) { CHECK(FzeroVersusTick(s,held) && FzeroVersusTick(copy,held)); }
  CHECK(v->race_tick==tick+300 && !memcmp(&terminal,&v->racers[1].actor,sizeof(terminal)) && FzeroVersusHash(s)==FzeroVersusHash(copy));
  FzeroVersusEndRace(s);session_roundtrip(s,copy);FzeroVersusDestroy(copy);FzeroVersusDestroy(s);
  puts("session events: shared contact/mine restore and continuation, simultaneous mine reads, rematch terrain reset, real surface-damage DNF with other racer continuing passed");
}
static void test_camera(void) {
  uint8_t negative[0x5da0];
  for (unsigned i=0;i<sizeof(negative);i+=2) {
    unsigned address=0x0e8000+i;
    uint16_t v=(uint16_t)(0u-(rom_read(NULL,address)|(unsigned)rom_read(NULL,address+1)<<8));
    negative[i]=(uint8_t)v;negative[i+1]=(uint8_t)(v>>8);
  }
  FzeroActorTables t={rom_read,NULL};
  for (unsigned sample=0;sample<40000;++sample) {
    FzeroActor a={0};a.x=random_word()&8191;a.y=random_word()&4095;a.facing=random_word()%0xc000;
    FzeroCamera camera;CHECK(FzeroCameraBuild(&camera,&a,t));
    memset(ram,0,sizeof(ram));memcpy(ram+0x8000,negative,sizeof(negative));
    write_fields(&a,actor_fields,COUNT(actor_fields));ram[0x81]=1;ram[0x82]=(sample&1)*16;
    reference(0x97bc);reference(0x980e);reference(0xa60c);reference(0x97e3);reference(0x0394cf);
    CHECK(camera.inverse_angle==ram[0xac]);
    CHECK(camera.center_x==ram_word(0x7d) && camera.center_y==ram_word(0x7f));
    unsigned alternate=ram[0x74];
    CHECK(camera.scroll_x==ram_word(0xea6+alternate) && camera.scroll_y==ram_word(0xea8+alternate));
    CHECK(camera.background_h==ram_word(0xea1+alternate) && camera.background_v==ram_word(0xea3+alternate));
    CHECK(camera.skyline_h==ram_word(0x79) && camera.skyline_v==ram_word(0x7b));
    alternate=ram[0x82];
    unsigned pointers[]={ram_word(0xa84+alternate),ram_word(0xaa4+alternate),ram_word(0xaa4+alternate),ram_word(0xa84+alternate)};
    for (unsigned row=0;row<176;++row) for(unsigned component=0;component<4;++component) {
      unsigned address=((unsigned)ram[0x84+component]<<16)+pointers[component]+row*2;
      unsigned expected=read_bus(NULL,address)|(unsigned)read_bus(NULL,address+1)<<8;
      CHECK((uint16_t)camera.coefficients[row][component]==expected);
    }
    int16_t matrix[8];CHECK(!FzeroCameraAtLine(&camera,47,matrix));CHECK(!FzeroCameraAtLine(&camera,225,matrix));
    CHECK(FzeroCameraAtLine(&camera,48,matrix) && matrix[0]==camera.coefficients[0][0]);
    CHECK(FzeroCameraAtLine(&camera,224,matrix) && matrix[0]==camera.coefficients[175][0]);
  }
  puts("camera: 40000 independent pose/skyline/center/perspective fixtures passed against five stock display entries (normalized inverse-angle carry)");
}
int main(int argc, char **argv) {
  CHECK(argc == 2);
  FILE *f = fopen(argv[1], "rb"); CHECK(f);
  CHECK(fread(rom,1,sizeof(rom),f) == sizeof(rom) && fgetc(f) == EOF); fclose(f);
  static const uint8_t expected_hash[32] = {
    0xbf,0x16,0xc3,0xc8,0x67,0xc5,0x8e,0x2a,0xb0,0x61,0xc7,0x0d,0xe9,0x29,0x5b,0x69,
    0x30,0xd6,0x3f,0x29,0xf8,0x1c,0xc9,0x86,0xf5,0xec,0xae,0x03,0xe0,0xad,0x18,0xd2
  };
  uint8_t hash[32]; sha256_compute(rom,sizeof(rom),hash);
  CHECK(!memcmp(hash,expected_hash,sizeof(hash)));
  const unsigned routines[] = {0x9377,0x91bc,0x9264,0x930d,0x938d,0x953a,0x9503,0x9589,0x95e7,0x9672,0x96f2,0x9753,0x981c,0xb7cb,0xb94e,0xb881,0x9b4f,0x98a4,0x8e36,0x9d9e,0xfa1c,0x8d12,0xb5ec,0xba13,0xba70,0xb6cc,0xb61b,0xd9c8};
  unsigned total = 0;
  for (unsigned r=0;r<COUNT(routines);++r) {
    memset(ram_reads,0,sizeof(ram_reads));memset(ram_writes,0,sizeof(ram_writes));audit_enabled=true;
    for (unsigned machine=0;machine<4;++machine) for (unsigned sample=0;sample<10000;++sample) {
      FzeroActor a; FzeroActorScratch s; randomize(&a,&s,machine);
      uint8_t phase = (uint8_t)(sample % 2 ? 0 : 1); a.race_state = phase;
      if (routines[r] == 0x8e36) a.surface_flags = 0;
      if (routines[r] == 0x9d9e) a.surface_id=(uint8_t)(0xc8+sample%4);
      if ((routines[r] == 0xb6cc || routines[r] == 0xb61b) && sample%2 == 0) {
        a.speed=0;
        if (sample%8==0) { a.recovery_x=a.x; a.recovery_y=a.y; }
        if (sample%16==0) {
          a.recovery_angle=(uint8_t)(a.facing>>8);
          a.height=(uint16_t)((sample%4+3)*256);
        }
      }
      initial_actor=a; initial_scratch=s;
      memset(ram,0,sizeof(ram));
      write_fields(&a,actor_fields,COUNT(actor_fields)); write_fields(&s,scratch_fields,COUNT(scratch_fields));
      ram[0xd1] = machine * 2;
      ram[0x51] = (uint8_t)random_word(); ram[0xdc8] = 1;
      ram[0xb00]=0x88;ram[0xc60]=0x80;
      if (routines[r]==0xd9c8) {
        ram[0xad]=255;ram[0x1075]=(uint8_t)(sample%7*2);
        for (unsigned i=0;i<256;++i) {
          set_word(0x1200+i*2,random_word()&8191);set_word(0x1400+i*2,random_word()&4095);
          ram[0x1600+i]=random_word()%192;ram[0x1700+i]=(uint8_t)random_word();
        }
      }
      call_adapter(routines[r],&a,&s,phase); reference(routines[r]);
      compare_fields(&a,actor_fields,COUNT(actor_fields),routines[r],sample);
      if (routines[r] < 0xb000) compare_fields(&s,scratch_fields,COUNT(scratch_fields),routines[r],sample);
      ++total;
    }
    audit_enabled=false;audit_ownership(routines[r]);
    printf("%06x: 40000 scalar fixtures passed\n",routines[r]); fflush(stdout);
  }
  printf("%u stock scalar comparisons passed (four machines)\n",total);
  test_world_sample();
  test_sequences();
  test_progress();
  test_contact();
  test_finish();
  test_mine_commit();
  test_session();
  test_session_finishes();
  test_session_events();
  test_session_contact_dnf();
  test_camera();
  return 0;
}
