#include "fzero_fault.h"
#include "fzero_capture.h"
#include "post_mortem.h"
#include <signal.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static int finished;
static void exit_report(void) {
  if (!finished) recomp_post_mortem_dump("atexit",NULL);
}
static void signal_report(int sig) {
  recomp_post_mortem_dump("signal",NULL);
  _Exit(128+sig);
}
#ifdef _WIN32
static LONG WINAPI exception_report(EXCEPTION_POINTERS *info) {
  /* Shared writer emits the minidump first, then host/guest rings and our
   * cached replay location. No snapshotting or guest calls on this path. */
  recomp_post_mortem_dump("seh",info);
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif
void FzeroFaultInstall(void) {
  recomp_post_mortem_set_game_section(FzeroCaptureReport);
#ifndef _WIN32
  signal(SIGSEGV,signal_report);
#else
  /* The CRT SIGSEGV shim would discard the original SEH fault context. */
  SetUnhandledExceptionFilter(exception_report);
  SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
#endif
  signal(SIGABRT,signal_report);
  atexit(exit_report);
}
void FzeroFaultFinish(void) {
  FzeroCaptureStop();
  recomp_post_mortem_dump("normal_exit",NULL);
  finished=1;
}
