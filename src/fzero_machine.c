#include "fzero_machine.h"

static const uint8_t slots[FZERO_MACHINE_COUNT]={0,1,2,3,0,1,2,3,1,3,0,2};
static const uint8_t groups[FZERO_MACHINE_COUNT]={0,0,0,0,1,1,1,1,2,2,3,3};
static const uint8_t roster[FZERO_MACHINE_COUNT]={4,6,5,7,0,2,8,9,10,11,1,3};
static const char *names[FZERO_MACHINE_COUNT]={"BLUE FALCON","WILD GOOSE","GOLDEN FOX","FIRE STINGRAY",
  "MOON SHADOW","DRAGON BIRD","GREAT STAR","DEATH ANCHOR","P. EMERALD","BLACK BULL","WHITE CAT","RED GAZELLE"};
unsigned FzeroMachineSlot(unsigned id) {return id<FZERO_MACHINE_COUNT?slots[id]:0;}
unsigned FzeroMachineGroup(unsigned id) {return id<FZERO_MACHINE_COUNT?groups[id]:0;}
const char *FzeroMachineName(unsigned id) {return id<FZERO_MACHINE_COUNT?names[id]:"";}
unsigned FzeroMachineAt(unsigned index) {return roster[index%FZERO_MACHINE_COUNT];}
unsigned FzeroMachineIndex(unsigned id) {
  for(unsigned i=0;i<FZERO_MACHINE_COUNT;++i)if(roster[i]==id)return i;
  return 4;
}
unsigned FzeroMachineMove(unsigned id,uint16_t pressed,int *slide) {
  unsigned n=FzeroMachineIndex(id),row=n%4,column=n/4;*slide=0;
  if(pressed&4) {n=(n+1)%FZERO_MACHINE_COUNT;if(n/4!=column)*slide=1;}
  else if(pressed&0xc0) {
    *slide=pressed&0x40?-1:1;column=(column+FZERO_MACHINE_COLUMNS+*slide)%FZERO_MACHINE_COLUMNS;n=column*4+row;
  } else if(pressed&0x30)n=column*4+(row+((pressed&0x10)?3:1))%4;
  return roster[n];
}
static uint8_t read_machine(void *context,uint32_t address) {
  const FzeroMachineReader *r=context;
  return r->base.read(r->base.context,(address&0xffffff)|(r->group<<24));
}
FzeroActorTables FzeroMachineTables(FzeroMachineReader *r,FzeroActorTables base,unsigned id) {
  unsigned group=FzeroMachineGroup(id);
  if(base.read==read_machine) {
    const FzeroMachineReader *old=base.context;
    if(old->group==group)return base;
    base=old->base;
  }
  if(!group)return base;
  *r=(FzeroMachineReader){base,group};return (FzeroActorTables){read_machine,r};
}
bool FzeroMachineAvailable(FzeroActorTables t,unsigned id) {
  if(id>=FZERO_MACHINE_COUNT || !t.read)return false;
  if(!FzeroMachineGroup(id))return true;
  FzeroMachineReader r;t=FzeroMachineTables(&r,t,id);
  return t.read(t.context,0xfa7a)==29 && t.read(t.context,0xfa7b)==58 && t.read(t.context,0xfa7c)==87;
}
