#pragma once
/* Both processes receive identical virtual-pad presses, as they do from one
 * shared physical controller. The keyboard-only process must ignore them. */
static void online_source_test(long frame) {
  static bool started,checked;
  if(!FzeroOnlineEnabled() || !getenv("FZERO_TEST_ONLINE_SOURCE"))return;
  const FzeroOnlineState *s=FzeroOnlineCompleted();
  if(!started && s->phase==FZERO_ONLINE_MACHINES) {
    if(!FzeroReplayConfigure(NULL,getenv("FZERO_VIEWPORT_SCRIPT")))Die("Cannot stop source-test boot script");
    started=true;
  }
  if(!started)return;
  selftest_release_all();
  if(frame>=430 && frame<=432)selftest_set(SDL_CONTROLLER_BUTTON_DPAD_DOWN,1);
  if(frame>=480 && frame<=482)selftest_set(SDL_CONTROLLER_BUTTON_START,1);
  if(frame>=600 && !checked) {
    bool ok=s->phase==FZERO_ONLINE_MACHINES && s->ready==1 &&
        s->config.machine[0]==2 && s->config.machine[1]==1;
    fprintf(stderr,"online-source-test: one controller moves/confirms only its configured client %s (phase=%u ready=%u cars=%u,%u)\n",
        ok?"PASS":"FAIL",s->phase,s->ready,s->config.machine[0],s->config.machine[1]);
    if(!ok)g_selftest_failed=1;
    checked=true;
  }
}

/* Explicit actual-SDL online browser fixture. Runs in the host event pump,
 * including while the shared menu barrier has stopped simulation. Keys use
 * the same handlers as a person. No authoritative state is injected here. */
static void online_menu_test(long frame) {
  static unsigned stage,pumps;
  if(!FzeroOnlineEnabled() || !getenv("FZERO_TEST_ONLINE_MENU"))return;
  if(stage==0 && frame>=600) {selftest_push_key(g_hotkey_menu.key);stage=1;}
  else if(stage==1 && snes_netplay_menu_idle()) {selftest_set(SDL_CONTROLLER_BUTTON_Y,1);stage=2;}
  else if(stage==2) {selftest_release_all();stage=3;pumps=0;}
  else if(stage==3 && ++pumps>30 && snes_netplay_menu_idle()) {selftest_push_key(SDLK_ESCAPE);stage=4;}
  else if(stage==4 && !snes_savestate_menu_is_open()) {
    fprintf(stderr,"online-menu-test: actual keyboard open/cancel and SDL pad save PASS\n");stage=5;
  }
}
