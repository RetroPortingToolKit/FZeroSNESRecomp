/* The Deluxe loader only reaches the runtime to install a program and to pick
 * a save root. Neither belongs to what test_deluxe.c checks - which payload is
 * accepted, and that a refusal is a return value rather than an exit - so they
 * stand in here and the test links without the recompiled program. */
#include "cpu_state.h"
#include "common_rtl.h"
#include "program_module.h"
#include "snes/interp_bridge.h"

SnesProgramModule deluxe_g_program_module;

void snes_program_module_select(const SnesProgramModule *module) { (void)module; }
void interp_bridge_set_scheduler_aot_policy(int enabled) { (void)enabled; }
const char *RtlSaveRoot(void) { return "."; }
void RtlEnsureSaveDir(void) {}
void RtlSetSaveRoot(const char *root) { (void)root; }
void RtlSetQuickSaveRoot(const char *root) { (void)root; }
