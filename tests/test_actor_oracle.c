/* Private-ROM differential fixtures for the portable human-actor adapters.
 * The reference executes the original ROM routine, with real M/X widths and
 * hardware multiply/divide semantics, on a separate callback bus. It cannot
 * modify a live guest, play sound or write cartridge-derived artifacts. */
#include "fzero_actor.h"
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
static uint16_t reference(unsigned pc) {
  Interp816 cpu = {0};
  cpu.mem = NULL; cpu.read = read_bus; cpu.write = write_bus;
  cpu.sp = 0x1fd; cpu.pc = (uint16_t)pc; cpu.k = (uint8_t)(pc >> 16);
  cpu.mf = cpu.xf = true; cpu.c = false;
  if (pc==0xfa1c) cpu.a=ram[0x66];
  ram[0x1fe] = ram[0x1ff] = 255;
  unsigned steps = 0;
  do { CHECK(++steps < 10000); interp816_runOpcode(&cpu); } while (cpu.sp != 0x1ff);
  CHECK(cpu.pc == 0);
  return cpu.a;
}
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
  s->top_speed = random_word() % 0x1000; s->turn = random_word() % 0x1000;
  s->facing = random_word() % 0xc000; s->momentum = random_word() % 0xc000;
  s->speed = random_word() % 0x1000; s->grip = random_word() % 0x1000;
  s->spin_decay = random_word() & 31;
  s->vx = random_word(); s->vy = random_word();
  s->vx_fraction = random_word(); s->vy_fraction = random_word();
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
  const unsigned routines[] = {0x9377,0x91bc,0x9264,0x930d,0x938d,0x953a,0x9503,0x9589,0x95e7,0x9672,0x96f2,0x9753,0x981c,0xb7cb,0xb94e,0xb881,0x9b4f,0x98a4,0x8e36,0x9d9e,0xfa1c,0x8d12,0xb5ec,0xba13,0xba70,0xb6cc,0xb61b};
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
  return 0;
}
