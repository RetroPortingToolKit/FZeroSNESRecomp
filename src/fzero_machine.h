#pragma once
#include "fzero_actor.h"

enum {FZERO_MACHINE_COUNT=12,FZERO_MACHINE_COLUMNS=3};
/* Stable roster identities are independent of the four native artwork/stat
 * slots. High ROM-address bits select immutable authored profile resources;
 * the caller owns the reader and may use its own ROM/cache implementation. */
unsigned FzeroMachineSlot(unsigned identity);
unsigned FzeroMachineGroup(unsigned identity);
const char *FzeroMachineName(unsigned identity);
unsigned FzeroMachineAt(unsigned index);
unsigned FzeroMachineIndex(unsigned identity);
unsigned FzeroMachineMove(unsigned identity,uint16_t pressed,int *slide);
bool FzeroMachineAvailable(FzeroActorTables tables,unsigned identity);
typedef struct FzeroMachineReader {FzeroActorTables base;unsigned group;} FzeroMachineReader;
FzeroActorTables FzeroMachineTables(FzeroMachineReader *reader,FzeroActorTables base,unsigned identity);
