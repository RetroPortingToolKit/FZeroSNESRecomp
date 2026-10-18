/* Private-ROM differential fixtures for the portable human-actor adapters.
 * The reference executes the original ROM routine, with real M/X widths and
 * hardware multiply/divide semantics, on a separate callback bus. It cannot
 * modify a live guest, play sound or write cartridge-derived artifacts. */
#include "fzero_actor.h"
#include "fzero_machine.h"
#include "fzero_versus.h"
#include "fzero_camera.h"
#include "fzero_ship.h"
#include "fzero_sound.h"
#include "fzero_native_ui.h"
#include "snes/interp816.h"
#include "sha256.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)
static uint8_t rom[0x80000], ram[0x20000];
static uint8_t *deluxe_rom,*cgp_images[3],*cgp_native;
static unsigned cgp_boost_entry;
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
  unsigned group=address>>24;address&=0xffffff;
  if(group || cgp_native) {
    const uint8_t *data=group && group<=3?cgp_images[group-1]:cgp_native;
    CHECK(data && (address&65535)>=0x8000);
    unsigned offset=((address>>16)&127)*0x8000+(address&0x7fff);CHECK(offset<0x400000);
    return data[offset];
  }
  if(deluxe_rom) {
    CHECK((address&0xffff)>=0x8000 && (address>>16)<32);
    return deluxe_rom[(address>>16)*0x8000+(address&0x7fff)];
  }
  if((address&0xffff)<0x8000 || (address>>16)>=16)fprintf(stderr,"unexpected oracle read %06x\n",address);
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
    if(pc==0xc507 && (address==0xe6 || address==0xe7 || address==0xed ||
        address==0xc70 || address==0xc80)) known=true;
    if(pc==0xbed7 && (address==0xc70 || address==0xc80)) known=true;
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
  if (pc>>16 && pc!=cgp_boost_entry) { cpu.sp=0x1fc;ram[0x1fd]=255; } /* RTL display entries */
  if(pc==cgp_boost_entry)cpu.c=true;
  if (pc==0x980e) cpu.c=true; /* explicit inverse-angle interface */
  if (pc==0xfa1c) cpu.a=ram[0x66];
  if (pc==0xde07) cpu.y=ram[0xd00];
  if (pc==0xdbc4) { cpu.mf=false;cpu.x=2; }
  if (pc==0x87a3) cpu.x=2;
  if (pc==0xbc4a || pc==0xbe02) cpu.y=2;
  if (pc==0xf188 || pc==0xf28b) cpu.x=2;
  if(pc==0xd107) {cpu.mf=false;cpu.a=0xcd2b;}
  if(pc==0xecc5) {cpu.mf=false;cpu.a=ram[0xce];cpu.x=0x34;cpu.y=0x4c;}
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
  FzeroMachineReader reader;t=FzeroMachineTables(&reader,t,a->machine);
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
    case 0xc507: FzeroActorExhaust(a,t); break;
    case 0xbed7: FzeroActorSpark(a); break;
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
  static uint8_t captured[0x20000],recaptured[0x20000];
  CHECK(FzeroActorCaptureWorld(captured,ram));
  CHECK(!FzeroActorCaptureWorld(captured,captured));
  for(unsigned y=0;y<4096;y+=8)for(unsigned x=0;x<8192;x+=8)
    CHECK(FzeroActorSampleSurface(captured,(uint16_t)x,(uint16_t)y)==FzeroActorSampleSurface(ram,(uint16_t)x,(uint16_t)y));
  CHECK(FzeroActorCaptureWorld(recaptured,captured) && !memcmp(captured,recaptured,sizeof(captured)));
  ram[0x51]^=73;ram[0x81]^=127;ram[0xb70]^=45;ram[0x14c00]^=21;
  CHECK(FzeroActorCaptureWorld(recaptured,ram) && !memcmp(captured,recaptured,sizeof(captured)));
  puts("course capture: all524288 surface cells, idempotence and unrelated guest/camera state exclusion passed");
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
    FzeroVersusConfig c={(uint16_t)(machine==3?1000:0),{(uint8_t)machine,(uint8_t)((machine+1)%4)}};
    FzeroVersus *s=FzeroVersusCreate(c,ram,spawns,t),*copy=FzeroVersusCreate(c,ram,spawns,t);CHECK(s && copy);
    const FzeroVersusSnapshot *v=FzeroVersusCompleted(s);uint16_t held[2]={0x8000,0};
    CHECK(!FzeroVersusTick(s,held));session_roundtrip(s,copy);
    FzeroVersusReady(s,0,true);CHECK(v->phase==FZERO_VS_READY);
    FzeroVersusReady(s,1,true);CHECK(v->phase==FZERO_VS_COUNTDOWN);
    for (unsigned i=0;i<FZERO_VERSUS_COUNTDOWN-1;++i) CHECK(FzeroVersusTick(s,held));
    CHECK(v->countdown==1 && v->race_tick==0);session_roundtrip(s,copy);
    CHECK(FzeroVersusTick(s,held) && v->phase==FZERO_VS_RACING);
    FzeroVersusPause(s,FZERO_VS_PAUSE_DEVICE2);uint64_t paused=FzeroVersusHash(s);
    for (unsigned i=0;i<120;++i) CHECK(FzeroVersusTick(s,held));
    CHECK(FzeroVersusHash(s)!=paused && !v->pause);
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
    held[0]=0;held[1]=0x1000;CHECK(FzeroVersusTick(s,held) && !v->pause);
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
  bool palette_saved=false;
  for(unsigned i=0;i<12000 && v->racers[1].status==FZERO_VS_ACTIVE;++i) {
    CHECK(FzeroVersusTick(s,held));
    if(!palette_saved && v->racers[1].actor.body_flash) {
      session_roundtrip(s,copy);CHECK(FzeroVersusCompleted(copy)->racers[1].actor.body_flash==1);palette_saved=true;
    }
  }
  CHECK(palette_saved);
  CHECK(v->racers[1].status==FZERO_VS_DNF && v->racers[0].status==FZERO_VS_SURVIVED &&
        v->phase==FZERO_VS_RESULTS && v->winner==FZERO_VS_PLAYER1);
  CHECK(!v->racers[0].completed_laps && !v->racers[1].completed_laps);
  FzeroActor terminal[2]={v->racers[0].actor,v->racers[1].actor};uint64_t tick=v->race_tick;
  session_roundtrip(s,copy);
  for(unsigned i=0;i<300;++i) { CHECK(!FzeroVersusTick(s,held) && !FzeroVersusTick(copy,held)); }
  CHECK(v->race_tick==tick && !memcmp(&terminal[0],&v->racers[0].actor,sizeof(*terminal)) &&
        !memcmp(&terminal[1],&v->racers[1].actor,sizeof(*terminal)) && FzeroVersusHash(s)==FzeroVersusHash(copy));
  FzeroVersusEndRace(s);session_roundtrip(s,copy);FzeroVersusDestroy(copy);FzeroVersusDestroy(s);
  for(unsigned both=0;both<2;++both) {
    spawns[0].y=3000;spawns[1].x=1400;spawns[1].y=both?3000:1000;
    s=FzeroVersusCreate(c,ram,spawns,t);copy=FzeroVersusCreate(c,ram,spawns,t);CHECK(s && copy);
    v=FzeroVersusCompleted(s);FzeroVersusReady(s,0,true);FzeroVersusReady(s,1,true);
    for(unsigned i=0;i<12000 && v->phase!=FZERO_VS_RESULTS;++i)CHECK(FzeroVersusTick(s,held));
    CHECK(v->phase==FZERO_VS_RESULTS && v->racers[0].status==FZERO_VS_DNF);
    CHECK(both?(v->racers[1].status==FZERO_VS_DNF && v->winner==FZERO_VS_NO_WINNER):
          (v->racers[1].status==FZERO_VS_SURVIVED && v->winner==FZERO_VS_PLAYER2));
    CHECK(!v->racers[0].completed_laps && !v->racers[1].completed_laps);
    session_roundtrip(s,copy);CHECK(FzeroVersusRematch(s));
    CHECK(v->phase==FZERO_VS_READY && v->racers[0].status==FZERO_VS_ACTIVE &&
          v->racers[1].status==FZERO_VS_ACTIVE && v->winner==FZERO_VS_NO_WINNER && !v->tick);
    FzeroVersusDestroy(copy);FzeroVersusDestroy(s);
  }
  FzeroVersusRacer field[4]={0};
  CHECK(!FzeroVersusRaceComplete(field,4));
  field[0].status=FZERO_VS_DNF;CHECK(!FzeroVersusRaceComplete(field,4));
  field[1].status=FZERO_VS_DNF;CHECK(!FzeroVersusRaceComplete(field,4));
  field[2].status=FZERO_VS_DNF;CHECK(FzeroVersusRaceComplete(field,4));
  field[1].status=FZERO_VS_FINISHED;CHECK(!FzeroVersusRaceComplete(field,4));
  field[3].status=FZERO_VS_FINISHED;CHECK(FzeroVersusRaceComplete(field,4));
  CHECK(!FzeroVersusRaceComplete(NULL,4) && !FzeroVersusRaceComplete(field,0));
  puts("session events: shared contact/mines, rematch terrain reset, input-driven elimination with survivor win, frozen terminal poses, atomic restore and four-player completion policy passed");
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
/* Execute the actual human OAM routines, then rasterize their result. This
 * catches swapped/yawed poses and wrong layout/flip/size, not just different
 * left/right pictures. Atlas loading is separately checked against native PPU
 * captures; these are the original human DMA destinations. */
static void player_reference_pixels(uint32_t out[4096],unsigned machine,int origin_x,int origin_y,bool flash) {
  uint8_t atlas[0x4000]={0};
  for(unsigned i=0;i<0xc00;++i)atlas[0xa0*32+i]=rom_read(NULL,((8+machine)<<16)| (0xf000+i));
  static const unsigned destinations[]={0x0e,0x2e,0x4e,0x6e,0x8e,0x100,0x102,0x104};
  for(unsigned pair=0;pair<8;++pair)for(unsigned half=0;half<2;++half)
    for(unsigned i=0;i<64;++i)atlas[(destinations[pair]+half*16)*32+i]=
        rom_read(NULL,((8+machine)<<16)|(0xfc00+pair*64+half*0x200+i));
  if(!(machine&1)) {
    for(unsigned pair=0;pair<4;++pair)for(unsigned half=0;half<2;++half)
      for(unsigned i=0;i<64;++i)atlas[(0x0c+pair*0x20+half*16)*32+i]=
          rom_read(NULL,0x09fc00+(machine?0x100:0)+pair*64+half*0x200+i);
    for(unsigned i=0;i<64;++i) {
      atlas[0x8c*32+i]=rom_read(NULL,0x0bfc00+i);
      atlas[0x9c*32+i]=rom_read(NULL,0x0bfdfe +i);
    }
  }
  /* Native DMA readback confirms the stream's final overlap on even cars. */
  if(!(machine&1))for(unsigned i=0;i<2;++i)atlas[0x9e*32+i]=rom_read(NULL,0x0bfe3e +i);
  memset(out,0,4096*sizeof(*out));
  for(unsigned slot=0;slot<8;++slot) {
    unsigned at=0x300+slot*4,high=ram[0xd80+slot/4]>>((slot%4)*2),attr=ram_word(at+2);
    int x=ram[at]+((high&1)?256:0);if(x>=256)x-=512;
    int y=ram[at+1];unsigned size=high&2?16:8;
    for(unsigned row=0;row<size;++row)for(unsigned col=0;col<size;++col) {
      int ox=x+(int)col-origin_x,oy=y+(int)row-origin_y;
      if(ox<0 || ox>=64 || oy<0 || oy>=64)continue;
      unsigned cx=attr&0x4000?size-1-col:col,cy=attr&0x8000?size-1-row:row;
      unsigned tile=(attr&256)|(((((attr&255)>>4)+cy/8)*16+(((attr&15)+cx/8)&15))&255);
      unsigned color=0;
      for(unsigned plane=0;plane<4;++plane)
        color|=((atlas[tile*32+(cy&7)*2+plane/2*16+plane%2]>>(7-(cx&7)))&1)<<plane;
      if(!color || out[oy*64+ox])continue;
      unsigned address=(flash?0x0aef00:0x0fcd80)+machine*32+color*2;
      unsigned rgb=rom_read(NULL,address)|(unsigned)rom_read(NULL,address+1)<<8;
      unsigned r=rgb&31,g=(rgb>>5)&31,bb=(rgb>>10)&31;
      out[oy*64+ox]=0xff000000u|(((r<<3)|(r>>2))<<16)|(((g<<3)|(g>>2))<<8)|(bb<<3)|(bb>>2);
    }
  }
}
static void test_menu_spin(void) {
  for(unsigned machine=0;machine<4;++machine)for(unsigned age=0;age<288;++age) {
    FzeroShip ship;CHECK(FzeroShipMenu(&ship,machine,age,(FzeroActorTables){rom_read,NULL}));
    int phase=8-(int)((age/6)%24);if(phase< -12)phase+=24;
    memset(ram,0,sizeof(ram));ram[0x52]=(uint8_t)machine;ram[0xce]=(uint8_t)phase;
    reference(0xecc5);
    if(ship.graphic!=ram[0x26] || ship.layout!=ram_word(0xcba))fprintf(stderr,"spin m=%u age=%u phase=%d actual=%x/%x native=%x/%x\n",machine,age,phase,ship.graphic,ship.layout,ram[0x26],ram_word(0xcba));
    CHECK(ship.visible && ship.graphic==ram[0x26] && ship.layout==ram_word(0xcba));
    CHECK(ship.size==0);
  }
  const char *folder=getenv("FZERO_TEST_SELECTOR_DUMP");
  if(folder) {
    static FzeroNativeUi ui;static uint32_t pixels[256*224];
    CHECK(FzeroNativeUiInit(&ui,(FzeroActorTables){rom_read,NULL}));
    for(unsigned machine=0;machine<4;++machine) {
      memset(pixels,0,sizeof(pixels));
      FzeroUiMachineSelect(&ui,(FzeroUiCanvas){pixels,256,224,1},machine,0,0,true,false);
      char path[2048];snprintf(path,sizeof(path),"%s/selector%u.ppm",folder,machine);
      FILE *f=fopen(path,"wb");CHECK(f);fprintf(f,"P6\n256 224\n255\n");
      for(unsigned n=0;n<256*224;++n) {
        uint8_t rgb[3]={(uint8_t)(pixels[n]>>16),(uint8_t)(pixels[n]>>8),(uint8_t)pixels[n]};
        CHECK(fwrite(rgb,3,1,f)==1);
      }
      CHECK(!fclose(f));
    }
  }
  puts("native selector spin: all four machines, every six-frame angle and both full rotations match original ECC5 layouts");
}
static void test_player_display(void) {
  FzeroActorTables tables={rom_read,NULL};
  for(unsigned machine=0;machine<4;++machine)for(unsigned pose=0;pose<13;++pose)for(unsigned scale=0;scale<7;++scale)for(unsigned height=0;height<4;++height) {
    FzeroActor a={.x=2048,.y=1024,.facing=0x3000,.machine=(uint8_t)machine,
        .steering_history=(uint8_t)(pose<11?pose:0x80+pose-11),.animation_scale=(uint8_t)scale};
    const unsigned heights[]={0,0x100,0xff00,0x700};a.height=(uint16_t)heights[height];a.body_flash=height&1;
    FzeroActor before=a;FzeroShip ship;CHECK(FzeroShipBuild(&ship,&a,&a,tables));CHECK(!memcmp(&a,&before,sizeof(a)));
    memset(ram,0,sizeof(ram));ram[0x52]=(uint8_t)machine;reference(0xd71d);
    for(unsigned i=0;i<8;++i) {ram[0x300+i*4]=128;set_word(0x302+i*4,0x105);}
    ram[0xc41]=(uint8_t)(8+machine*2);
    ram[0xdd]=a.steering_history;ram[0xde]=a.animation_scale;ram[0x10f0]=0x10; /* invalidate native OAM cache */
    set_word(0x1a,(unsigned)ship.ground_x);set_word(0x1c,(uint8_t)((unsigned)ship.ground_y-(a.height>>8)));
    reference(0xf4e9);
    uint32_t expected[4096];player_reference_pixels(expected,machine,ship.x,ship.y,a.body_flash!=0);
    for(unsigned i=0;i<4096;++i)if(expected[i]!=ship.pixels[i]) {
      fprintf(stderr,"human art mismatch machine=%u DD=%02x DE=%u pixel=%u,%u native=%08x host=%08x\n",
          machine,a.steering_history,scale,i%64,i/64,expected[i],ship.pixels[i]);
      for(unsigned part=0;part<8;++part)fprintf(stderr,"oam%u %02x %02x %04x high=%02x\n",part,ram[0x300+part*4],ram[0x301+part*4],ram_word(0x302+part*4),ram[0xd80+part/4]);
      fprintf(stderr,"header %u %u %u %u %u %u %u %u yoffset %u\n",ram[0x11e0],ram[0x11e1],ram[0x11e2],ram[0x11e3],ram[0x11e4],ram[0x11e6],ram[0x11e8],ram[0xdd],ram[0x10]);CHECK(false);
    }
  }
  puts("human display: 1456 complete steering/shoulder/height pose bitmaps match native D71D/F4E9 OAM (four machines)");
}

/* Optional private stock-PPU captures independently qualify the complete
 * texture lookup, beyond the CPU/OAM/layout differential fixtures above. */
static void test_player_captures(void) {
  const char *paths=getenv("FZERO_TEST_PLAYER_CAPTURES");if(!paths)return;
  static FzeroNativeUi ui;CHECK(FzeroNativeUiInit(&ui,(FzeroActorTables){rom_read,NULL}));
  char *list=malloc(strlen(paths)+1);CHECK(list);strcpy(list,paths);unsigned cases=0;
  for(char *path=strtok(list,";");path;path=strtok(NULL,";")) {
    struct Line {uint8_t regs[64];uint16_t palette[256],oam[256];uint8_t high[32];};
    struct Capture {struct Line lines[224];uint16_t vram[32768];uint32_t stock[256*224];uint8_t ram[131072];unsigned frame;bool valid;};
    _Static_assert(sizeof(struct Capture)==676872,"legacy private capture format");
    struct Capture *c=malloc(sizeof(*c));CHECK(c);FILE *f=fopen(path,"rb");CHECK(f);
    CHECK(fread(c,sizeof(*c),1,f)==1 && fgetc(f)==EOF);CHECK(!fclose(f));
    /* Independent native DMA/PPU readback: CPU-OAM comparisons must not
     * share an incorrectly decoded font or effect atlas. */
    for(unsigned id=0;id<44;++id)for(unsigned row=0;row<8;++row)for(unsigned col=0;col<8;++col) {
      unsigned bits=c->vram[0x7000+id*8+row],color=((bits>>(7-col))&1)|(((bits>>(15-col))&1)<<1);
      CHECK(ui.hud[id][row*8+col]==color);
    }
    static const unsigned objects[]={0x4a,0x4b,0x4c,0x4d,0x5a,0x5b,0x5c,0x5d,
        0x5f,0x6f,0x60,0x61,0x62,0x63,0x70,0x71,0x72,0x73,
        0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8d,
        0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9d,0xa8,0xb8};
    for(unsigned n=0;n<COUNT(objects);++n)for(unsigned row=0;row<8;++row)for(unsigned col=0;col<8;++col) {
      unsigned id=objects[n],color=0;
      for(unsigned plane=0;plane<4;++plane)
        color|=((c->vram[0x5000+id*16+row+plane/2*8]>>(plane%2*8+7-col))&1)<<plane;
      if(ui.objects[id][row*8+col]!=color) {
        fprintf(stderr,"native effect/font atlas %s id=%02x at=%u,%u expected=%u actual=%u\n",path,id,col,row,color,ui.objects[id][row*8+col]);CHECK(false);
      }
    }
    for(unsigned i=1;i<16;++i) {
      unsigned color=c->lines[160].palette[176+i],r=color&31,g=(color>>5)&31,b=(color>>10)&31;
      CHECK(ui.effect_palette[i]==(0xff000000u|(((r<<3)|(r>>2))<<16)|(((g<<3)|(g>>2))<<8)|(b<<3)|(b>>2)));
    }
    FzeroActor actor={0};for(unsigned field=0;field<COUNT(actor_fields);++field) {
      const Field *a=&actor_fields[field];memcpy((uint8_t *)&actor+a->offset,c->ram+a->address,a->size);
    }
    actor.machine=c->ram[0x52];FzeroActorTables tables={rom_read,NULL};FzeroShip ship;
    CHECK(FzeroShipBuild(&ship,&actor,&actor,tables));uint32_t expected[4096]={0};
    const struct Line *line=&c->lines[160];
    /* Capture RAM and displayed OAM straddle a native frame boundary. Align
     * by the first body sprite to isolate artwork from the one-frame bob. */
    memset(ram,0,sizeof(ram));ram[0x52]=actor.machine;reference(0xd71d);ram[0xc41]=8+actor.machine*2;
    ram[0xdd]=actor.steering_history;ram[0xde]=actor.animation_scale;ram[0x10f0]=0x10;
    set_word(0x1a,ship.ground_x);set_word(0x1c,(uint8_t)(ship.ground_y-(actor.height>>8)));reference(0xf4e9);
    int origin_x=ship.x+(int)(line->oam[136]&255)-ram[0x300];
    int origin_y=ship.y+(int)(line->oam[136]>>8)-ram[0x301];
    for(unsigned slot=68;slot<76;++slot) {
      unsigned position=line->oam[slot*2],attr=line->oam[slot*2+1],high=line->high[slot/4]>>((slot%4)*2);
      int x=(position&255)|((high&1)<<8);if(x>=256)x-=512;
      int y=position>>8;unsigned size=high&2?16:8;
      for(unsigned row=0;row<size;++row)for(unsigned col=0;col<size;++col) {
        int ox=x+(int)col-origin_x,oy=y+(int)row-origin_y;if(ox<0 || ox>=64 || oy<0 || oy>=64)continue;
        unsigned cx=attr&0x4000?size-1-col:col,cy=attr&0x8000?size-1-row:row;
        unsigned tile=((attr&255)/16+cy/8)*16+(((attr&15)+cx/8)&15);
        unsigned base=((line->regs[1]&7)<<13)+(attr&256?(((line->regs[1]&24)+8)<<9):0),color=0;
        for(unsigned plane=0;plane<4;++plane) {
          unsigned word=c->vram[(base+tile*16+(cy&7)+plane/2*8)&32767];
          /* The shared lower pair uploads at $49C1: its preceding word is
           * leftover VRAM. The ROM-only raster initializes that gap to zero. */
          if(!(actor.machine&1) && tile==0x9c && !(cy&7) && plane<2)word=0;
          color|=((word>>(plane%2*8+7-(cx&7)))&1)<<plane;
        }
        if(!color || expected[oy*64+ox])continue;
        /* The native engine-glow palette animates independently of pose.
         * Normalize it to the same stock palette used by the host raster. */
        unsigned rgb=rom_read(NULL,0x0fcd80+actor.machine*32+color*2)|
            (unsigned)rom_read(NULL,0x0fcd81+actor.machine*32+color*2)<<8;
        unsigned r=rgb&31,g=(rgb>>5)&31,bb=(rgb>>10)&31;
        expected[oy*64+ox]=0xff000000u|(((r<<3)|(r>>2))<<16)|(((g<<3)|(g>>2))<<8)|(bb<<3)|(bb>>2);
      }
    }
    for(unsigned i=0;i<4096;++i)if(expected[i]!=ship.pixels[i]) {
      fprintf(stderr,"stock PPU mismatch %s machine=%u DD=%02x pixel=%u,%u native=%08x host=%08x\n",
          path,actor.machine,actor.steering_history,i%64,i/64,expected[i],ship.pixels[i]);
      fprintf(stderr,"origin=%d,%d ship=%d,%d height=%04x first=%u,%u native=%u,%u\n",origin_x,origin_y,ship.x,ship.y,actor.height,ram[0x300],ram[0x301],line->oam[136]&255,line->oam[136]>>8);
      CHECK(false);
    }
    ++cases;free(c);
  }
  free(list);printf("human display/effects/HUD: %u stock-PPU body captures and race-font/boost/spark/contact atlases match (body bob/glow/unused upload word normalized)\n",cases);
}

static void test_display(void) {
  for(unsigned speed=0;speed<65536;++speed) {
    memset(ram,0,sizeof(ram));set_word(0xb20,speed);reference(0xa98a);
    unsigned expected=ram[0xa58]*100+ram[0xa59]*10+ram[0xa5a];
    CHECK(FzeroDisplaySpeed((uint16_t)speed)==expected);
  }
  FzeroActorTables tables={rom_read,NULL};
  for(unsigned sample=0;sample<16000;++sample) {
    FzeroActor camera={.x=4096,.y=2048},actor={0};
    camera.facing=(uint16_t)(random_word()%0xc000);
    actor.facing=(uint16_t)(random_word()%0xc000);actor.machine=sample%4;
    actor.x=(uint16_t)((camera.x+(int)(random_word()%1200)-600)&8191);
    actor.y=(uint16_t)((camera.y+(int)(random_word()%1200)-600)&4095);
    FzeroActorProjection p=FzeroActorProject(&camera,&actor,tables);
    FzeroShip ship;CHECK(FzeroShipBuild(&ship,&camera,&actor,tables));
    if(!p.projectable) {CHECK(!ship.visible);continue;}
    memset(ram,0,sizeof(ram));
    set_word(0xbd0,camera.facing);set_word(0xbd2,actor.facing);
    ram[0xc62]=(uint8_t)p.y;ram[0x1142]=255;ram[0x1133]=actor.machine;
    reference(0xf188);reference(0xf28b);
    CHECK(ship.visible && ship.graphic==ram[0x1142]);
    CHECK(ship.size==ram[0xc33] && ship.layout==ram_word(0xcb2));
    CHECK(ship.ground_x==p.x && ship.ground_y==p.y);
    bool painted=false;
    for(unsigned i=0;i<64*64;++i) if(ship.pixels[i])painted=true;
    CHECK(painted);
  }
  for(unsigned machine=0;machine<4;++machine) {
    FzeroActor a={.x=2048,.y=1024,.facing=0x3000,.machine=(uint8_t)machine};
    FzeroShip straight,left,right;CHECK(FzeroShipBuild(&straight,&a,&a,tables));
    a.steering_history=0x80;FzeroActor before=a;CHECK(FzeroShipBuild(&left,&a,&a,tables));CHECK(!memcmp(&a,&before,sizeof(a)));
    a.steering_history=0x81;CHECK(FzeroShipBuild(&right,&a,&a,tables));
    CHECK(memcmp(straight.pixels,left.pixels,sizeof(left.pixels)) && memcmp(left.pixels,right.pixels,sizeof(left.pixels)));
    /* The actual stock shoulder routines are included in the differential
     * oracle; exercise their public chain for each machine as well. */
    a=(FzeroActor){.machine=(uint8_t)machine,.speed=0x600,.facing=0x3000,.momentum=0x3000};
    FzeroActorLatch(&a,0x8020);FzeroActorInputAction(&a,tables);FzeroActorTurnPerformance(&a,tables);FzeroActorAnimation(&a,tables);
    CHECK(a.momentum!=0x3000 && a.steering_history==0x80);
    a=(FzeroActor){.machine=(uint8_t)machine,.speed=0x600,.facing=0x3000,.momentum=0x3000};
    FzeroActorLatch(&a,0x8010);FzeroActorInputAction(&a,tables);FzeroActorTurnPerformance(&a,tables);FzeroActorAnimation(&a,tables);
    CHECK(a.momentum!=0x3000 && a.steering_history==0x81);
  }
  puts("display: all65536 speed values and16000 ship pose/asset-selection fixtures passed against stock routines");
}

static void test_sound(void) {
  /* The real CPU's GO transition starts the already uploaded course song.
   * Audio peak/engine tests alone miss a permanently silent music program. */
  memset(ram,0,sizeof(ram));reference_until(0xad9f,0xada3);unsigned race_music=ram[0x46];CHECK(race_music==6);
  FzeroSoundState music;FzeroSoundInit(&music,3);
  FzeroVersusSnapshot arrival={.phase=FZERO_VS_COUNTDOWN,.countdown=FZERO_VERSUS_COUNTDOWN};
  for(unsigned i=0;i<FZERO_VERSUS_ARRIVAL;++i) {
    FzeroSoundCommands command=FzeroSoundTick(&music,&arrival,true);
    CHECK(command.ports[0]==3 && !command.ports[1] && !command.ports[2]);--arrival.countdown;
  }
  FzeroSoundCommands rev=FzeroSoundTick(&music,&arrival,true);
  CHECK(rev.ports[0]==1 && rev.ports[1]==1);
  FzeroSoundInit(&music,1);
  FzeroVersusSnapshot start={.phase=FZERO_VS_COUNTDOWN,.countdown=1};
  CHECK(FzeroSoundTick(&music,&start,true).ports[0]==1);
  start.pause=1;CHECK(FzeroSoundTick(&music,&start,false).ports[0]==1);
  start.pause=0;start.phase=FZERO_VS_RACING;
  CHECK(FzeroSoundTick(&music,&start,true).ports[0]==race_music);
  start.pause=1;CHECK(FzeroSoundTick(&music,&start,false).ports[0]==race_music);
  start.pause=0;CHECK(FzeroSoundTick(&music,&start,false).ports[0]==race_music);
  music.music=1;CHECK(FzeroSoundTick(&music,&start,false).ports[0]==race_music); /* old restored latch */
  start.phase=FZERO_VS_RESULTS;CHECK(FzeroSoundTick(&music,&start,true).ports[0]==(race_music|128));
  start.phase=FZERO_VS_COUNTDOWN;CHECK(FzeroSoundTick(&music,&start,true).ports[0]==1);
  for(unsigned sample=0;sample<40000;++sample) {
    FzeroActor a;FzeroActorScratch scratch;randomize(&a,&scratch,sample%4);
    a.sound_flags=(uint8_t)sample;a.sound_flags2=(uint8_t)(sample>>8);a.landing_sound=0;
    FzeroActor before=a;FzeroSoundCommands out=FzeroSoundActor(&a);CHECK(!memcmp(&a,&before,sizeof(a)));
    memset(ram,0,sizeof(ram));write_fields(&a,actor_fields,COUNT(actor_fields));
    reference(0xf8dd);reference(0xf999);reference(0xf975);
    CHECK(out.ports[1]==ram[0x47] && out.ports[2]==ram[0x48]);
    FzeroVersusSnapshot v={.phase=FZERO_VS_RACING,.race_tick=20,.tick=200};
    v.racers[0].actor=a;randomize(&v.racers[1].actor,&scratch,(sample/4)%4);
    v.racers[0].actor.landing_sound%=13;v.racers[1].actor.landing_sound%=13;
    FzeroSoundState one={2,0,FZERO_VS_RACING},two=one;
    FzeroSoundCommands mixed=FzeroSoundTick(&one,&v,true);
    FzeroVersusRacer swap=v.racers[0];v.racers[0]=v.racers[1];v.racers[1]=swap;
    FzeroSoundCommands reversed=FzeroSoundTick(&two,&v,true);
    CHECK(!memcmp(&mixed,&reversed,sizeof(mixed)) && !memcmp(&one,&two,sizeof(one)));
    unsigned landing=one.landing;v.pause=FZERO_VS_PAUSE_MANUAL;
    out=FzeroSoundTick(&one,&v,false);
    CHECK(out.ports[0]==race_music && !out.ports[1] && !out.ports[2] && !out.ports[3] && one.landing==landing);
    v.pause=0;out=FzeroSoundTick(&one,&v,false);CHECK(!out.ports[1] && !out.ports[2]);
  }
  puts("sound: 40000 native engine/effect encoders, immutable actors, symmetric two-human mixing and paused effects passed");
}

static void test_transient_effects(void) {
  for(unsigned sample=0;sample<40000;++sample) {
    FzeroActor a;FzeroActorScratch s;randomize(&a,&s,sample%4);
    a.death_animation=0;a.black_flash=(uint8_t)sample;
    unsigned phase=sample%18;
    a.effect_timer=phase==0?0:phase<=5?0x40+phase:0x80+(phase-5);
    memset(ram,0,sizeof(ram));write_fields(&a,actor_fields,COUNT(actor_fields));ram[0xb00]=0x88;
    FzeroActorTransientEffects(&a);reference(0xbf5f);reference(0xbf83);reference(0xc0ac);
    compare_fields(&a,actor_fields,COUNT(actor_fields),0xbf5f,sample);
  }
  puts("native transient effects: 40000 electric/wall/impact timers and palette-event fixtures passed");
}
static void paint_native_obj(uint32_t out[256*224],const FzeroNativeUi *ui,unsigned slot,unsigned size,int offset_x) {
  unsigned at=0x200+slot*4,attr=ram_word(at+2);
  int x=(int)ram[at]+offset_x,y=ram[at+1];
  if((ram[0x400+slot/4]>>((slot%4)*2))&1)x-=256;
  for(unsigned row=0;row<size;++row)for(unsigned col=0;col<size;++col) {
    unsigned cx=attr&0x4000?size-1-col:col,cy=attr&0x8000?size-1-row:row;
    unsigned id=((attr&255)+(cy/8)*16+cx/8)&255;
    unsigned color=ui->objects[id][(cy&7)*8+(cx&7)];
    int dx=x+(int)col,dy=(y+(int)row)&255;if(!color || dx<0 || dx>=256 || dy>=224)continue;
    unsigned address=0x0fcd00+((attr>>9)&7)*32+color*2;
    if(((attr>>9)&7)==3 && color<8)address=0x0aeee0+color*2;
    unsigned rgb=rom_read(NULL,address)|(unsigned)rom_read(NULL,address+1)<<8;
    unsigned r=rgb&31,g=(rgb>>5)&31,b=(rgb>>10)&31;
    out[dy*256+dx]=0xff000000u|(((r<<3)|(r>>2))<<16)|(((g<<3)|(g>>2))<<8)|(b<<3)|(b>>2);
  }
}
static void test_native_hud(void) {
  static FzeroNativeUi ui;CHECK(FzeroNativeUiInit(&ui,(FzeroActorTables){rom_read,NULL}));
  static uint32_t actual[256*224],expected[256*224];FzeroUiCanvas canvas={actual,256,224,1};
  memset(ram,0,sizeof(ram));
  for(unsigned i=0;i<128;++i)ram[0xa00+i]=rom_read(NULL,0x0aec00+i*2);
  for(int health=0x800;health>=0;--health) {
    set_word(0xc9,(unsigned)health);reference(0xaa97);
    CHECK(ram[0xe23]==175+(unsigned)health/32);
    memset(actual,0,sizeof(actual));FzeroUiRaceInstruments(&ui,canvas,144,15,(uint16_t)health,0,0);
    for(unsigned half=0;half<2;++half)for(unsigned col=18;col<=30;++col) {
      unsigned at=half*32+col,attr=ui.hud_map[at],id=ram[0xa00+at];
      for(unsigned row=0;row<8;++row)for(unsigned x=0;x<8;++x) {
        unsigned bits=rom_read(NULL,0x0cc000+id*16+(attr&0x8000?7-row:row)*2);
        unsigned hi=rom_read(NULL,0x0cc001+id*16+(attr&0x8000?7-row:row)*2);
        unsigned color=((bits>>(7-x))&1)|(((hi>>(7-x))&1)<<1);
        if(!color || (color==1 && col<22))continue;
        unsigned pixel=actual[(15+half*8+row)*256+col*8+x];
        if(pixel!=ui.hud_palette[(attr>>10)&3][color]) {
          fprintf(stderr,"native gauge health=%d tile=%02x col=%u half=%u pixel=%u,%u\n",health,id,col,half,x,row);CHECK(false);
        }
      }
    }
  }
  for(unsigned warning=0;warning<2;++warning) {
    set_word(0xc9,0x100);ram[0x51]=warning?8:0;reference(0xaa06);
    for(unsigned color=0;color<3;++color)CHECK(ui.power_colors[warning?4:0][1+color]==ram[0xe2a+color]);
    unsigned rgb=ram_word(0x506),r=rgb&31,g=(rgb>>5)&31,b=(rgb>>10)&31;
    uint32_t expected=0xff000000u|(((r<<3)|(r>>2))<<16)|(((g<<3)|(g>>2))<<8)|(b<<3)|(b>>2);
    CHECK((warning?ui.power_warning:ui.hud_palette[0][3])==expected);
  }
  memset(ram,0,sizeof(ram));
  for(unsigned i=0;i<128;++i)ram[0x250+i]=rom_read(NULL,0x0bec34+i);
  ram[0xc0]=0;ram[0xc1]=0x12;ram[0xc2]=0x34;reference(0xa880);
  memset(actual,0,sizeof(actual));memset(expected,0,sizeof(expected));
  for(unsigned i=32;i<44;++i)paint_native_obj(expected,&ui,i,8,0);
  for(unsigned i=0;i<COUNT(expected);++i)if(expected[i]==0xff000000)expected[i]=0;
  FzeroUiRaceClock(&ui,canvas,184,48,"0'12\"34");CHECK(!memcmp(actual,expected,sizeof(actual)));
  for(unsigned active=0;active<2;++active)for(unsigned count=0;count<4-active;++count) {
    memset(ram,0,sizeof(ram));
    for(unsigned i=0;i<128;++i)ram[0x250+i]=rom_read(NULL,0x0bec34+i);
    for(unsigned i=0;i<count+active;++i) {ram[0xcf8]=1;reference(0xb85e);}
    if(active)reference(0xb825);
    memset(actual,0,sizeof(actual));memset(expected,0,sizeof(expected));
    for(unsigned i=44;i<47;++i)paint_native_obj(expected,&ui,i,8,0);
    /* Native nonzero palette entries include the S-jet black outline. */
    FzeroUiBoosts(&ui,canvas,208,208,count,active!=0);
    if(memcmp(actual,expected,sizeof(actual))) {
      for(unsigned i=44;i<47;++i)fprintf(stderr,"native boost active=%u count=%u slot=%u xy=%04x attr=%04x\n",active,count,i,ram_word(0x200+i*4),ram_word(0x202+i*4));
      for(unsigned i=0;i<COUNT(actual);++i)if(actual[i]!=expected[i]) {fprintf(stderr,"boost pixel=%u,%u expected=%08x actual=%08x\n",i%256,i/256,expected[i],actual[i]);break;}
      CHECK(false);
    }
  }
  puts("native HUD: all2049 power levels match AA97 boundary/outline tiles, clock OAM and all available/burning/spent boost states match native output (text black backing omitted)");
}
static void test_native_flash_colors(void) {
  static const unsigned surface[]={25,26,39};
  for(unsigned sample=0;sample<1000;++sample) {
    memset(ram,0,sizeof(ram));
    for(unsigned color=0;color<5;++color)set_word(0x524+color*2,random_word()&0x7fff);
    for(unsigned color=0;color<3;++color)set_word(0x500+surface[color]*2,random_word()&0x7fff);
    reference(0xbe45);
    for(unsigned color=0;color<5;++color)for(unsigned phase=0;phase<8;++phase)
      CHECK(FzeroCameraFlashColor(ram_word(0x524+color*2),phase,false)==ram_word(0x910+color*16+phase*2));
    for(unsigned color=0;color<3;++color)for(unsigned phase=0;phase<9;++phase)
      CHECK(FzeroCameraFlashColor(ram_word(0x500+surface[color]*2),phase,true)==ram_word(0x900+(phase?8:0)+color*2));
  }
  puts("native course flashes: 40000 electric-ramp colors and27000 surface colors match BE45 construction from original course CGRAM; no initialized guest lookup tables required");
}
static void test_native_course_animation(void) {
  uint16_t base[256],actual[256];
  for(unsigned venue=0;venue<9;++venue)for(unsigned counter=0;counter<8;++counter) {
    memset(ram,0,sizeof(ram));ram[0xcf5]=(uint8_t)venue;ram[0x9a]=(uint8_t)counter;ram[0x5b]=1;
    for(unsigned i=0;i<256;++i) {base[i]=random_word()&0x7fff;set_word(0x500+i*2,base[i]);}
    for(unsigned tick=0;tick<65;++tick) {
      FzeroCameraCoursePalette(actual,base,venue,counter,tick,-1,NULL);
      for(unsigned i=0;i<256;++i)CHECK(actual[i]==ram_word(0x500+i*2));
      reference(0xc185);
    }
  }
  /* Imported programs include a rotation overlapping the C185 swaps. */
  const uint8_t offsets[]={0x30,0x40,0xc0};uint16_t expected[256];
  for(unsigned i=0;i<256;++i)base[i]=expected[i]=(uint16_t)i;
  for(unsigned tick=0;tick<1025;++tick) {
    FzeroCameraCoursePalette(actual,base,0,3,tick,3,offsets);CHECK(!memcmp(actual,expected,sizeof(actual)));
    unsigned phase=(3+tick+1)&3;
    if(phase<2) {unsigned i=42+phase*2;uint16_t swap=expected[i];expected[i]=expected[i+1];expected[i+1]=swap;}
    for(unsigned n=0;n<3;++n) {unsigned i=16+offsets[n]/2;uint16_t last=expected[i+7];memmove(expected+i+1,expected+i,14);expected[i]=last;}
  }
  for(unsigned elapsed=1;elapsed<=68;++elapsed) {
    unsigned offset=FzeroCameraArrivalOffset(elapsed);CHECK(offset<=127);
    if(elapsed>1)CHECK(offset<=FzeroCameraArrivalOffset(elapsed-1));
  }
  CHECK(FzeroCameraArrivalOffset(34)==36 && !FzeroCameraArrivalOffset(68));
  puts("course animation: all native venues/counter phases match C185/C1CF; imported overlapping cycles and native entry trajectory passed");
}
static void test_finish_presentation(void) {
  FzeroActorTables tables={rom_read,NULL};
  for(unsigned machine=0;machine<4;++machine)for(unsigned angle=0;angle<192;angle+=24)for(unsigned speed=0;speed<3;++speed) {
    FzeroActor frozen={.machine=(uint8_t)machine,.x=8190,.y=4094,.x_fraction=0xff00,.y_fraction=0x0100,
      .speed=(uint16_t)(speed==0?0:speed==1?0x200:0x800),.facing=(uint16_t)(angle<<8),.momentum=(uint16_t)(angle<<8)};
    FzeroActor original=frozen,display;
    CHECK(FzeroCameraFinishPose(&display,&frozen,tables,0)==255 && !memcmp(&display,&frozen,sizeof(frozen)));
    memset(ram,0,sizeof(ram));write_fields(&frozen,actor_fields,COUNT(actor_fields));
    FzeroActorScratch scratch={.speed=frozen.speed<0x200?0x200:frozen.speed,.momentum=frozen.momentum};
    for(unsigned tick=1;tick<=FZERO_FINISH_PRESENTATION;++tick) {
      write_fields(&scratch,scratch_fields,COUNT(scratch_fields));reference(0x95e7);reference(0x9753);
      unsigned opacity=FzeroCameraFinishPose(&display,&frozen,tables,tick);
      CHECK(display.x==ram_word(0xb70) && display.y==ram_word(0xb90));
      CHECK(display.x_fraction==ram_word(0xb80) && display.y_fraction==ram_word(0xba0));
      CHECK(opacity==(tick<=60?255:tick>=90?0:(90-tick)*255/30));
      CHECK(!memcmp(&frozen,&original,sizeof(frozen)));
    }
    FzeroActor later;CHECK(!FzeroCameraFinishPose(&later,&frozen,tables,UINT64_MAX));
    CHECK(!memcmp(&display,&later,sizeof(display)));
  }
  puts("finish presentation: four-machine coasting matches native95E7/9753, bounded fade, immutable terminal actor and long-age redraw passed");
}
static void test_deluxe_course_animation(void) {
  const char *path=getenv("FZERO_TEST_DELUXE_ROM");if(!path)return;
  FILE *file=fopen(path,"rb");CHECK(file);
  deluxe_rom=malloc(0x100000);CHECK(deluxe_rom);
  CHECK(fread(deluxe_rom,1,0x100000,file)==0x100000 && fgetc(file)==EOF);fclose(file);
  uint8_t world[0x20000]={0};uint16_t base[256],actual[256];
  for(unsigned type=0;type<4;++type)for(unsigned counter=0;counter<8;++counter) {
    memset(ram,0,sizeof(ram));ram[0x14c03]=(uint8_t)type;ram[0x9a]=(uint8_t)counter;ram[0x5b]=1;
    FzeroActorSetNativePalette(world,type);CHECK(!world[FZERO_WORLD_RESOURCE]);
    for(unsigned i=0;i<256;++i) {base[i]=random_word()&0x7fff;set_word(0x500+i*2,base[i]);}
    for(unsigned tick=0;tick<65;++tick) {
      FzeroCameraCoursePalette(actual,base,0,counter,tick,world[FZERO_WORLD_CYCLES],world+FZERO_WORLD_CYCLE_OFFSETS);
      for(unsigned i=0;i<256;++i)CHECK(actual[i]==ram_word(0x500+i*2));
      reference(0xc185);
    }
  }
  puts("native BS palette: all four metadata programs and counter phases match Deluxe CPU C185/C1CF");
  for(unsigned type=0;type<8;++type)for(unsigned sample=0;sample<2000;++sample) {
    FzeroActor a={.jump_watch=255,.takeoff_x=random_word()&8191,.takeoff_y=random_word()&4095,
      .x=random_word()&8191,.y=random_word()&4095};
    memset(ram,0,sizeof(ram));write_fields(&a,actor_fields,COUNT(actor_fields));
    ram[0xc2]=10;ram[0xad]=255;ram[0x14c2a]=(uint8_t)type;
    for(unsigned i=0;i<256;++i) {
      set_word(0x1200+i*2,random_word()&8191);set_word(0x1400+i*2,random_word()&4095);
      ram[0x1600+i]=random_word()%192;ram[0x1700+i]=(uint8_t)random_word();
    }
    CHECK(FzeroActorCaptureWorld(world,ram));world[FZERO_WORLD_NATIVE_SHORTCUT]=(uint8_t)(type+1);
    FzeroActorRecoveryTrigger(&a,world,(FzeroActorTables){rom_read,NULL});reference(0xd9c8);
    compare_fields(&a,actor_fields,COUNT(actor_fields),0xd9c8,type*2000+sample);
  }
  puts("native BS shortcuts: all eight metadata predicates match Deluxe CPU on16000 landing/recovery fixtures");
  free(deluxe_rom);deluxe_rom=NULL;
}
static void test_native_pit_art(void) {
  static FzeroNativeUi ui;CHECK(FzeroNativeUiInit(&ui,(FzeroActorTables){rom_read,NULL}));
  static uint32_t actual[256*224],expected[256*224];FzeroUiCanvas canvas={actual,256,224,1};
  static uint32_t wide[768*224];
  for(unsigned machine=0;machine<4;++machine)for(unsigned early=0;early<4;++early) {
    FzeroActor a={.machine=(uint8_t)machine,.health=0x400};
    memset(ram,0,sizeof(ram));
    ram[0xd1]=(uint8_t)(machine*2);
    memset(ram+0x400,0xff,32);for(unsigned slot=0;slot<128;++slot)ram[0x200+slot*4]=0x80;
    for(unsigned tick=0;tick<230;++tick) {
      a.surface_flags=tick<(early==0?8:early==1?80:150)?0x10:0;
      if(early==3)a.recovery_active=tick<60;
      write_fields(&a,actor_fields,COUNT(actor_fields));reference(0xb61b);FzeroActorPit(&a,(FzeroActorTables){rom_read,NULL});
      compare_fields(&a,actor_fields,COUNT(actor_fields),0xb61b,tick);
      memset(actual,0,sizeof(actual));memset(expected,0,sizeof(expected));
      for(int slot=59;slot>=52;--slot)paint_native_obj(expected,&ui,(unsigned)slot,16,0);
      for(int slot=11;slot>=0;--slot) {
        unsigned high=(ram[0x400+slot/4]>>((slot%4)*2))&3;
        paint_native_obj(expected,&ui,(unsigned)slot,high&2?16:8,0);
      }
      FzeroUiPit(&ui,canvas,0,&a);
      memset(wide,0,sizeof(wide));FzeroUiPit(&ui,(FzeroUiCanvas){wide,768,224,1},256,&a);
      for(unsigned y=0;y<224;++y)for(unsigned x=0;x<768;++x)
        CHECK(wide[y*768+x]==(x>=256 && x<512?actual[y*256+x-256]:0));
      if(memcmp(actual,expected,sizeof(actual))) {
        for(unsigned i=0;i<COUNT(actual);++i)if(actual[i]!=expected[i]) {
          fprintf(stderr,"native pit art machine=%u exit=%u tick=%u phase=%u pose=%u departure=%u pixel=%u,%u expected=%08x actual=%08x\n",machine,early,tick,a.pit_phase,a.pit_pose,a.pit_departure,i%256,i/256,expected[i],actual[i]);break;
        }
        CHECK(false);
      }
    }
  }
  puts("native pit art: all four machines, short entry and full arrival/beam/healing/departure match original CPU OAM and palettes");
}
/* Owner-authorized pose fixture on actual captured stock repair tiles.
 * Only pose/motion and initial damage are injected; pit flags, arrival,
 * healing, departure and snapshot continuation run through the real session. */
static void test_stock_pits(void) {
  const char *paths=getenv("FZERO_TEST_PIT_WORLDS");if(!paths)return;
  char *list=malloc(strlen(paths)+1);CHECK(list);strcpy(list,paths);unsigned courses=0;
  for(char *path=strtok(list,";");path;path=strtok(NULL,";")) {
    static uint8_t world[0x20000];FILE *f=fopen(path,"rb");CHECK(f);
    CHECK(fread(world,sizeof(world),1,f)==1 && fgetc(f)==EOF);CHECK(!fclose(f));
    unsigned px=0,py=0,rx=0,ry=0,pdx=0,pdy=0,rdx=0,rdy=0;
    for(unsigned y=8;y<4000 && !py;y+=8)for(unsigned x=8;x<8096;x+=8) {
      if(FzeroActorSampleSurface(world,x,y)!=0xb9)continue;
      if(FzeroActorSampleSurface(world,x+64,y)==0xb9) {px=x;py=y;pdx=64;break;}
      if(FzeroActorSampleSurface(world,x,y+64)==0xb9) {px=x;py=y;pdy=64;break;}
    }
    for(unsigned y=8;y<4000 && !ry;y+=8)for(unsigned x=8;x<8096;x+=8) {
      if(FzeroActorSampleSurface(world,x,y)!=0x9a)continue;
      if(FzeroActorSampleSurface(world,x+64,y)==0x9a) {rx=x;ry=y;rdx=64;break;}
      if(FzeroActorSampleSurface(world,x,y+64)==0x9a) {rx=x;ry=y;rdy=64;break;}
    }
    CHECK(py && ry);
    for(unsigned machine=0;machine<4;++machine) {
      FzeroVersusConfig config={0,{(uint8_t)machine,(uint8_t)((machine+1)%4)}};
      FzeroActor spawn[2]={{.x=(uint16_t)px,.y=(uint16_t)py},{.x=(uint16_t)(px+pdx),.y=(uint16_t)(py+pdy)}};
      FzeroVersus *s=FzeroVersusCreate(config,world,spawn,(FzeroActorTables){rom_read,NULL});CHECK(s);
      FzeroVersusReady(s,0,true);FzeroVersusReady(s,1,true);
      const uint16_t held[2]={0,0};for(unsigned n=0;n<FZERO_VERSUS_COUNTDOWN;++n)CHECK(FzeroVersusTick(s,held));
      FzeroVersusSnapshot *v=(FzeroVersusSnapshot *)(void *)FzeroVersusCompleted(s);
      for(unsigned p=0;p<2;++p)v->racers[p].actor.health=0x400;
      unsigned healed[2]={0,0};
      for(unsigned tick=0;tick<650;++tick) {
        for(unsigned p=0;p<2;++p) {
          FzeroActor *a=&v->racers[p].actor;
          a->x=(uint16_t)(px+p*pdx);a->y=(uint16_t)(py+p*pdy);a->state=0;a->recovery_active=0;
          a->speed=a->speed_fraction=a->vx=a->vy=a->vx_fraction=a->vy_fraction=0;
        }
        CHECK(FzeroVersusTick(s,held));
        for(unsigned p=0;p<2;++p) {
          const FzeroActor *a=&v->racers[p].actor;
          if(a->health>0x400 && !healed[p])healed[p]=tick+1;
          if(a->surface_id!=0xb9 || !(a->surface_flags&0x10) || a->health<0x400) {
            fprintf(stderr,"pit integration %s machine=%u seat=%u tick=%u pos=%u,%u tile=%02x flags=%02x phase=%u health=%04x recovery=%u\n",path,a->machine,p,tick,a->x,a->y,a->surface_id,a->surface_flags,a->pit_phase,a->health,a->recovery_active);CHECK(false);
          }
        }
      }
      CHECK(healed[0] && healed[1] && v->racers[0].actor.health==0x800 && v->racers[1].actor.health==0x800);
      FzeroVersus *copy=FzeroVersusCreate(config,world,spawn,(FzeroActorTables){rom_read,NULL});CHECK(copy);
      session_roundtrip(s,copy);
      CHECK(FzeroVersusTick(s,held) && FzeroVersusTick(copy,held));
      CHECK(FzeroVersusHash(s)==FzeroVersusHash(copy));
      for(unsigned p=0;p<2;++p) {
        CHECK(FzeroVersusCompleted(copy)->racers[p].actor.pit_pose==v->racers[p].actor.pit_pose);
        v->racers[p].actor.health=0x600;
      }
      for(unsigned tick=0;tick<80;++tick) {
        for(unsigned p=0;p<2;++p) {
          FzeroActor *a=&v->racers[p].actor;a->x=(uint16_t)(rx+p*rdx);a->y=(uint16_t)(ry+p*rdy);
          a->state=0;a->recovery_active=0;
          a->speed=a->speed_fraction=a->vx=a->vy=a->vx_fraction=a->vy_fraction=0;
        }
        CHECK(FzeroVersusTick(s,held));
        for(unsigned p=0;p<2;++p)CHECK(v->racers[p].actor.surface_id==0x9a && v->racers[p].actor.health==0x600);
        if(tick==8 || tick==35) {
          session_roundtrip(s,copy);
          CHECK(FzeroVersusTick(s,held) && FzeroVersusTick(copy,held));
          CHECK(FzeroVersusHash(s)==FzeroVersusHash(copy));
        }
      }
      CHECK(!v->racers[0].actor.pit_phase && !v->racers[1].actor.pit_phase);
      CHECK(!v->racers[0].actor.pit_pose && !v->racers[1].actor.pit_pose);
      FzeroVersusDestroy(copy);
      printf("stock pit %s machines=%u/%u first-heal=%u/%u health=%04x/%04x\n",path,config.machine[0],config.machine[1],healed[0],healed[1],v->racers[0].actor.health,v->racers[1].actor.health);
      FzeroVersusDestroy(s);
    }
    ++courses;
  }
  free(list);printf("stock repair integration: %u course captures, all four machines and both seats passed\n",courses);
}
static void test_native_effect_art(void) {
  static FzeroNativeUi ui;CHECK(FzeroNativeUiInit(&ui,(FzeroActorTables){rom_read,NULL}));
  static uint32_t actual[256*224],expected[256*224];
  FzeroUiCanvas canvas={actual,256,224,1};
  memset(ram,0,sizeof(ram));ram[0x4e]=3;reference(0xd107);
  CHECK(ui.crash_length==11 && ram_word(0x4b)==80);
  for(unsigned slot=0;slot<20;++slot)paint_native_obj(expected,&ui,slot,8,-4);
  FzeroUiCrashOut(&ui,canvas,192);CHECK(!memcmp(expected,actual,sizeof(actual)));
  for(unsigned machine=0;machine<4;++machine)for(unsigned side=0;side<2;++side)for(unsigned phase=1;phase<=10;++phase) {
    FzeroActor a={.machine=(uint8_t)machine,.speed=0x800,.animation_transition=(uint8_t)(phase|(side?0x80:0))};
    memset(ram,0,sizeof(ram));write_fields(&a,actor_fields,COUNT(actor_fields));ram[0xc70]=128;ram[0xc80]=180;
    reference(0xbed7);FzeroActorSpark(&a);
    memset(actual,0,sizeof(actual));memset(expected,0,sizeof(expected));
    paint_native_obj(expected,&ui,47,ram[0x40b]&0x80?16:8,0);
    FzeroUiSpark(&ui,canvas,128,180,0,a.animation_transition);
    CHECK(!memcmp(expected,actual,sizeof(actual)));
  }
  for(unsigned machine=0;machine<4;++machine)for(unsigned effect=0;effect<3;++effect)for(unsigned overlap=0;overlap<3;++overlap) {
    FzeroActor a={.machine=(uint8_t)machine,.effect_timer=effect==0?0:effect==1?0x45:0x8b,
        .black_flash=overlap==0?0:overlap==1?0x85:9};
    memset(ram,0,sizeof(ram));ram[0xb00]=0x88;ram[0xc41]=(8+machine*2);
    for(unsigned i=0;i<256;++i) {
      ram[0x600+i]=ram[0x700+i]=rom_read(NULL,0x0fcd00+i);
      ram[0x800+i]=rom_read(NULL,0x0aee80+i);
    }
    for(unsigned tick=0;tick<16;++tick) {
      write_fields(&a,actor_fields,COUNT(actor_fields));ram[0xc70]=128;ram[0xc80]=180;
      reference(0xbf83);reference(0xc0ac);FzeroActorTransientEffects(&a);
      compare_fields(&a,actor_fields,COUNT(actor_fields),0xbf83,tick);
      unsigned base=0x80+machine*32;
      CHECK(!memcmp(ram+0x600+base,ram+(a.body_flash?0x800:0x700)+base,32));
      memset(actual,0,sizeof(actual));memset(expected,0,sizeof(expected));
      for(int slot=114;slot>=112;--slot)paint_native_obj(expected,&ui,(unsigned)slot,8,0);
      FzeroUiContact(&ui,canvas,128,180,0,a.effect_timer);
      if(memcmp(expected,actual,sizeof(actual))) {
        fprintf(stderr,"native electricity art machine=%u effect=%u overlap=%u tick=%u phase=%02x\n",machine,effect,overlap,tick,a.effect_timer);CHECK(false);
      }
    }
  }
  puts("native effects: all four machines, all20 spark selections, complete contact/landing sequences and overlapping palette latches match native CPU OAM/CGRAM");
}

static void test_all_exhaust_art(void) {
  static FzeroNativeUi ui;static uint32_t actual[256*224],expected[256*224];
  FzeroActorTables tables={rom_read,NULL};CHECK(FzeroNativeUiInit(&ui,tables));CHECK(FzeroNativeUiLoadMachines(&ui,tables));
  for(unsigned id=0;id<FZERO_MACHINE_COUNT;++id)for(unsigned pose=0;pose<13;++pose)for(unsigned tilt=0;tilt<2;++tilt) {
    unsigned group=FzeroMachineGroup(id),slot=FzeroMachineSlot(id);
    cgp_native=group?cgp_images[group-1]:NULL;
    FzeroActor a={.machine=(uint8_t)id,.speed=0x800,.exhaust=0x80,
      .steering_history=(uint8_t)(pose<11?pose:128+pose-11),.animation_scale=(uint8_t)(tilt?128:0)};
    memset(ram,0,sizeof(ram));ram[0x52]=(uint8_t)slot;ram[0xd1]=(uint8_t)(slot*2);
    set_word(0xe6,slot?rom_read(NULL,0xbe98+(slot-1)*2)|(unsigned)rom_read(NULL,0xbe99+(slot-1)*2)<<8:0xc68c);
    for(unsigned tick=0;tick<256;++tick) {
      FzeroActor native=a;native.machine=(uint8_t)slot;
      write_fields(&native,actor_fields,COUNT(actor_fields));ram[0xc70]=128;ram[0xc80]=180;
      reference(0xc507);
      cgp_native=NULL;FzeroMachineReader reader;FzeroActorExhaust(&a,FzeroMachineTables(&reader,tables,id));
      memset(actual,0,sizeof(actual));memset(expected,0,sizeof(expected));
      FzeroUiExhaust(&ui,(FzeroUiCanvas){actual,256,224,1},128,180,&a);
      cgp_native=group?cgp_images[group-1]:NULL;
      for(int pipe=3;pipe>=0;--pipe) {
        unsigned at=0x2f0+(unsigned)pipe*4,attr=ram_word(at+2),high=ram[0x40f]>>(pipe*2);
        int x=ram[at]-(high&1?256:0),y=ram[at+1];
        for(unsigned row=0;row<8;++row)for(unsigned col=0;col<8;++col) {
          int dx=x+(int)col,dy=(y+(int)row)&255;
          unsigned color=ui.objects[attr&255][row*8+col];
          if(!color || dx<0 || dx>=256 || dy>=224)continue;
          unsigned value=color==12?ram_word(0x638):rom_read(NULL,0x0fcd20+color*2)|(unsigned)rom_read(NULL,0x0fcd21+color*2)<<8;
          unsigned r=value&31,g=(value>>5)&31,b=(value>>10)&31;
          expected[dy*256+dx]=0xff000000u|(((r<<3)|(r>>2))<<16)|(((g<<3)|(g>>2))<<8)|(b<<3)|(b>>2);
        }
      }
      if(memcmp(expected,actual,sizeof(actual))) {
        fprintf(stderr,"native exhaust id=%u pose=%u tilt=%u tick=%u E1=%02x frame=%u timer=%u color=%u offset=%u nativeColor=%04x\n",id,pose,tilt,tick,a.exhaust,a.exhaust_frame,a.exhaust_timer,a.exhaust_color,a.exhaust_offset,ram_word(0x638));
        for(unsigned n=0;n<COUNT(actual);++n)if(actual[n]!=expected[n]) {fprintf(stderr,"pixel%u,%u native=%08x actual=%08x\n",n%256,n/256,expected[n],actual[n]);break;}
        CHECK(false);
      }
    }
    cgp_native=NULL;
  }
  puts("native exhaust: all12 identities,13 steering/shoulder poses, both hover offsets,256-tick ignition/decay/full-color sequences match CPU OAM/CGRAM PASS");
}
static void test_cgp_mixed_sessions(void) {
  FzeroActorTables tables={rom_read,NULL};
  for(unsigned ma=0;ma<FZERO_MACHINE_COUNT;++ma)for(unsigned mb=0;mb<FZERO_MACHINE_COUNT;++mb) {
    for(unsigned n=0;n<100;++n) {
      FzeroActor a,b;FzeroActorScratch scratch;randomize(&a,&scratch,FzeroMachineSlot(ma));randomize(&b,&scratch,FzeroMachineSlot(mb));
      FzeroActor na=a,nb=b;pair_fields(&na,&nb);ram[0x11]=0;ram[0x12]=2;
      a.machine=(uint8_t)ma;b.machine=(uint8_t)mb;FzeroActorContactSpeed(&a,&b);
      unsigned group=FzeroMachineGroup(ma);if(!group)group=FzeroMachineGroup(mb);
      cgp_native=group?cgp_images[group-1]:NULL;reference(0xbd51);
      a.machine=na.machine;compare_fields(&a,actor_fields,COUNT(actor_fields),0xbd51,ma*1200+mb*100+n);
      if(b.speed!=ram_word(0xb22) || b.momentum!=ram_word(0xbe2) || b.no_control!=ram_word(0xd22))
        fprintf(stderr,"mixed speed pair=%u,%u sample=%u group=%u initialSpeed=%04x/%04x facing=%04x/%04x resultB speed=%04x/%04x momentum=%04x/%04x control=%04x/%04x\n",ma,mb,n,group,na.speed,nb.speed,na.facing,nb.facing,b.speed,ram_word(0xb22),b.momentum,ram_word(0xbe2),b.no_control,ram_word(0xd22));
      CHECK(b.speed==ram_word(0xb22) && b.momentum==ram_word(0xbe2) && b.no_control==ram_word(0xd22));cgp_native=NULL;
    }
    for(unsigned side=0;side<2;++side)for(unsigned direct=0;direct<2;++direct)for(unsigned n=0;n<100;++n) {
      unsigned own=side?mb:ma,other=side?ma:mb,group=FzeroMachineGroup(own);
      FzeroActor a,b;FzeroActorScratch scratch;
      randomize(&a,&scratch,FzeroMachineSlot(own));randomize(&b,&scratch,FzeroMachineSlot(other));
      a.machine=(uint8_t)own;b.machine=(uint8_t)other;
      FzeroActor native_a=a,native_b=b;native_a.machine=(uint8_t)FzeroMachineSlot(own);
      native_b.machine=(uint8_t)FzeroMachineSlot(other);pair_fields(&native_a,&native_b);
      FzeroMachineReader reader;
      FzeroActorDamageContact(&a,&b,FzeroMachineTables(&reader,tables,own),direct!=0);
      cgp_native=group?cgp_images[group-1]:NULL;
      unsigned entry=direct?0x87a3:0x8808;reference(entry);
      a.machine=native_a.machine;compare_fields(&a,actor_fields,COUNT(actor_fields),entry,ma*1200+mb*100+n);
      cgp_native=NULL;
    }
    flat_course();FzeroActor spawns[2]={{.x=1000,.y=1000,.facing=0x3000},{.x=1008,.y=1000,.facing=0x3000}};
    FzeroVersusConfig config={0,{(uint8_t)ma,(uint8_t)mb}};
    FzeroVersus *s=FzeroVersusCreate(config,ram,spawns,tables),*copy=FzeroVersusCreate(config,ram,spawns,tables);
    CHECK(s && copy);session_roundtrip(s,copy);
    FzeroVersusReady(s,0,true);FzeroVersusReady(s,1,true);session_roundtrip(s,copy);
    for(unsigned tick=0;tick<900;++tick) {
      uint16_t held[2]={(uint16_t)(0x8000|(tick%97<20?0x110:0)),(uint16_t)(0x8000|(tick%79<15?0x220:0))};
      CHECK(FzeroVersusTick(s,held)==FzeroVersusTick(copy,held));
      if(tick%150==0) {CHECK(FzeroVersusHash(s)==FzeroVersusHash(copy));session_roundtrip(s,copy);}
    }
    CHECK(FzeroVersusCompleted(s)->racers[0].actor.machine==ma && FzeroVersusCompleted(s)->racers[1].actor.machine==mb);
    FzeroVersusEndRace(s);CHECK(FzeroVersusRematch(s));session_roundtrip(s,copy);
    CHECK(FzeroVersusCompleted(s)->config.machine[0]==ma && FzeroVersusCompleted(s)->config.machine[1]==mb);
    FzeroVersusDestroy(s);FzeroVersusDestroy(copy);
  }
  puts("twelve identities: all144 ordered pairs,57600 native own-profile collision damage and14400 native speed-response fixtures, shared ticks, atomic saves/replay and rematch PASS");
}
static void test_cgp_machine_profiles(void) {
  const char *folder=getenv("FZERO_TEST_CGP_PROFILE_DIR");if(!folder)return;
  for(unsigned group=0;group<3;++group) {
    char path[2048];snprintf(path,sizeof(path),"%s/profile%u.sfc",folder,group+1);
    FILE *f=fopen(path,"rb");CHECK(f);cgp_images[group]=malloc(0x400000);CHECK(cgp_images[group]);
    CHECK(fread(cgp_images[group],0x400000,1,f)==1 && fgetc(f)==EOF);fclose(f);
  }
  const unsigned routines[]={0x91bc,0x9264,0x938d,0x96f2,0xb7cb,0xb881,0x98a4,0xfa1c,0xba70,0xc507};
  for(unsigned id=4;id<12;++id) {
    unsigned slot=FzeroMachineSlot(id),group=FzeroMachineGroup(id);cgp_native=cgp_images[group-1];
    CHECK(FzeroMachineAvailable((FzeroActorTables){rom_read,NULL},id));
    for(unsigned r=0;r<COUNT(routines);++r)for(unsigned sample=0;sample<2000;++sample) {
      FzeroActor a;FzeroActorScratch scratch;randomize(&a,&scratch,slot);
      a.machine=(uint8_t)id;a.speed=(uint16_t)(a.speed%0xe01);scratch.speed=a.speed;
      a.acceleration_index=rom_read(NULL,0xfa79+slot);a.turning_index=rom_read(NULL,0xfa7d+slot);
      a.race_state=sample%2;a.steering_history=sample%13;
      if(routines[r]==0xc507) {
        static const uint8_t phases[]={0,1,2,3,4,0x80,0x81,0x82,0x83};
        a.exhaust=phases[sample%COUNT(phases)];a.exhaust_frame=sample%4;
        a.exhaust_timer=(sample/4)%6;a.exhaust_color=sample%95;
      }
      initial_actor=a;initial_scratch=scratch;
      memset(ram,0,sizeof(ram));FzeroActor native=a;native.machine=(uint8_t)slot;
      write_fields(&native,actor_fields,COUNT(actor_fields));write_fields(&scratch,scratch_fields,COUNT(scratch_fields));
      ram[0xd1]=(uint8_t)(slot*2);ram[0x51]=(uint8_t)sample;ram[0xdc8]=1;ram[0xb00]=0x88;ram[0xc60]=0x80;
      if(routines[r]==0xc507) {ram[0xe6]=0x8c;ram[0xe7]=0xc6;}
      call_adapter(routines[r],&a,&scratch,a.race_state);reference(routines[r]);
      a.machine=(uint8_t)slot;compare_fields(&a,actor_fields,COUNT(actor_fields),routines[r],id*2000+sample);
      if(routines[r]<0xb000)compare_fields(&scratch,scratch_fields,COUNT(scratch_fields),routines[r],id*2000+sample);
    }
    unsigned wrapper=cgp_native[0x3f]|(unsigned)cgp_native[0x40]<<8|(unsigned)cgp_native[0x41]<<16;
    CHECK(rom_read(NULL,wrapper)==0x20);
    cgp_boost_entry=(wrapper&0xff0000)|rom_read(NULL,wrapper+1)|(unsigned)rom_read(NULL,wrapper+2)<<8;
    for(unsigned sample=0;sample<3000;++sample) {
      static const uint16_t health[]={0,1,17,19,20,21,22,23,24,0x1ff,0x200,0x800,0xffff};
      static const uint8_t timer[]={0,1,8,16,150,255};
      FzeroActor a={.machine=(uint8_t)id,.health=health[sample%COUNT(health)],.lap=(uint8_t)(1+sample%6),
        .state=(uint8_t)((sample%4)*8),.boosts=(uint8_t)(sample%4),.boost_award=(uint8_t)(sample%2),
        .boost_timer=timer[sample%COUNT(timer)],.recovery_active=(uint8_t)(sample%11==0?255:0),
        .held=(uint16_t)(sample%3==0?0x8000:0),.pressed=(uint16_t)(sample%7==0?0x80:0)};
      FzeroActor expected=a;expected.machine=(uint8_t)slot;
      memset(ram,0,sizeof(ram));write_fields(&expected,actor_fields,COUNT(actor_fields));
      ram[0x54]=2;ram[0x55]=3;ram[0xd1]=(uint8_t)(slot*2);ram[0xf53]=a.lap>1;ram[0xd40]=a.lap-1;
      FzeroActorEnergyBoost(&a);FzeroMachineReader reader;
      FzeroActorInputAction(&a,FzeroMachineTables(&reader,(FzeroActorTables){rom_read,NULL},id));
      reference(cgp_boost_entry);reference(0xb7cb);
      /* The native helper's lap numbering is zero-based, like $F53; VS's
       * authored five-lap progression exposes lap1..5 to its HUD instead. */
      ram[0xd40]=a.lap;a.machine=(uint8_t)slot;
      compare_fields(&a,actor_fields,COUNT(actor_fields),cgp_boost_entry,id*3000+sample);
    }
    cgp_boost_entry=0;
    printf("CGP identity%u profile%u/slot%u:20000 movement/handling/boost/exhaust and3000 energy/refill/input fixtures PASS\n",id,group,slot);fflush(stdout);
  }
  cgp_native=NULL;
  test_all_exhaust_art();
  test_cgp_mixed_sessions();
  for(unsigned group=0;group<3;++group) {
    cgp_native=cgp_images[group];
    test_player_display();test_menu_spin();test_native_effect_art();
    printf("CGP profile%u native human poses, full spin and spark/electricity art PASS\n",group+1);fflush(stdout);
  }
  cgp_native=NULL;
  const char *dump=getenv("FZERO_TEST_EXPANDED_SELECTOR_DUMP");
  if(dump) {
    static FzeroNativeUi ui;static uint32_t pixels[256*224];
    CHECK(FzeroNativeUiInit(&ui,(FzeroActorTables){rom_read,NULL}));
    CHECK(FzeroNativeUiLoadMachines(&ui,(FzeroActorTables){rom_read,NULL}) && ui.machines_available==4095);
    for(unsigned id=0;id<FZERO_MACHINE_COUNT;++id) {
      memset(pixels,0,sizeof(pixels));FzeroUiMachineSelect(&ui,(FzeroUiCanvas){pixels,256,224,1},id,0,0,true,false);
      char path[2048];snprintf(path,sizeof(path),"%s/expanded%u.ppm",dump,id);
      FILE *f=fopen(path,"wb");CHECK(f);fprintf(f,"P6\n256 224\n255\n");
      for(unsigned n=0;n<256*224;++n) {
        uint8_t rgb[3]={(uint8_t)(pixels[n]>>16),(uint8_t)(pixels[n]>>8),(uint8_t)pixels[n]};CHECK(fwrite(rgb,3,1,f)==1);
      }
      CHECK(!fclose(f));
    }
  }
  for(unsigned i=0;i<3;++i) {free(cgp_images[i]);cgp_images[i]=NULL;}
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
  const unsigned routines[] = {0x9377,0x91bc,0x9264,0x930d,0x938d,0x953a,0x9503,0x9589,0x95e7,0x9672,0x96f2,0x9753,0x981c,0xb7cb,0xb94e,0xb881,0x9b4f,0x98a4,0x8e36,0x9d9e,0xfa1c,0x8d12,0xb5ec,0xba13,0xba70,0xb6cc,0xb61b,0xd9c8,0xc507,0xbed7};
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
      if (routines[r]==0xc507) {
        static const uint8_t phases[]={0,1,2,3,4,0x80,0x81,0x82,0x83};
        a.exhaust=phases[sample%COUNT(phases)];a.exhaust_frame=sample%4;
        a.exhaust_timer=(sample/4)%6;a.exhaust_color=sample%95;a.exhaust_offset=sample%5;
        a.steering_history=sample%13;
        if(a.steering_history>10)a.steering_history+=0x75;
        a.animation_scale=sample%7;
      }
      initial_actor=a; initial_scratch=s;
      memset(ram,0,sizeof(ram));
      write_fields(&a,actor_fields,COUNT(actor_fields)); write_fields(&s,scratch_fields,COUNT(scratch_fields));
      ram[0xd1] = machine * 2;
      ram[0x51] = (uint8_t)random_word(); ram[0xdc8] = 1;
      ram[0xb00]=0x88;ram[0xc60]=0x80;
      /* C2ED normally selects this presentation-only exhaust layout jump.
       * Exercise a valid native target; no handler may feed actor state. */
      if(routines[r]==0xc507) {ram[0xe6]=0x8c;ram[0xe7]=0xc6;}
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
  {
    flat_course();ram[FZERO_WORLD_RESOURCE]=1;
    ram[FZERO_WORLD_TERRAIN+0x300+0xd0]=0x80;
    FzeroActor a={.state=0x80,.height=0x210,.height_velocity=0xff00,.surface_id=0xd0,.lap=2};
    FzeroActorScratch scratch={.gravity=0x60};
    CHECK(!a.ground_flags);
    FzeroActorCourseAltitude(&a,&scratch,(FzeroActorTables){rom_read,NULL},0,ram);
    CHECK((a.race_state&0x40) && a.death_animation==6 && a.death_lap==2);
    a=(FzeroActor){.state=0x80,.height=0x210,.height_velocity=0xff00,.surface_id=0};
    FzeroActorCourseAltitude(&a,&scratch,(FzeroActorTables){rom_read,NULL},0,ram);
    CHECK(!a.race_state);
  }
  test_session();
  test_session_finishes();
  test_session_events();
  test_session_contact_dnf();
  test_camera();
  test_display();
  test_player_display();
  test_menu_spin();
  test_player_captures();
  test_sound();
  test_transient_effects();
  test_native_hud();
  test_native_flash_colors();
  test_stock_pits();
  test_native_pit_art();
  test_native_course_animation();
  test_finish_presentation();
  test_deluxe_course_animation();
  test_native_effect_art();
  test_cgp_machine_profiles();
  return 0;
}
