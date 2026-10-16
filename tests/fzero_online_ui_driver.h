#pragma once
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
