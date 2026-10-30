#include "fzero_video.h"
#include "fzero_replay.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Do not use assert: Release builds must execute all validation. */
#define CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static void viewport_tests(void) {
  FzeroVideoSettings settings;
  FzeroVideoDefaults(&settings); /* Fit, Auto rate, CGP rebalances, local VS and Legend. */
  CHECK(settings.enhanced && settings.aspect == FZERO_ASPECT_FIT && settings.fps == 0 &&
        settings.fps_enabled && !settings.bs_deluxe);
  CHECK(settings.local_two_player && settings.gameplay.stock_rebalance==15);
  CHECK(settings.hd_mode7 && settings.hd_scale == 8 && !settings.diagnostics);
  CHECK(settings.crash_replay);
  CHECK(settings.gameplay.enabled == FZERO_RULE_DEFAULT_MASK);
  FzeroVideoStock(&settings);
  CHECK(!settings.enhanced && settings.aspect == FZERO_ASPECT_STOCK && !settings.fps_enabled && !settings.bs_deluxe);
  CHECK(!settings.local_two_player && !settings.gameplay.stock_rebalance);
  FzeroViewport v = FzeroCalculateViewport(&settings, 5120, 1440);
  CHECK(!v.enhanced && v.width == 256 && v.extra == 0);
  settings.enhanced = true;
  const int expected[] = {256, 342, 448, 682};
  for (int i = 0; i < 4; ++i) {
    settings.aspect = (FzeroAspect)i;
    v = FzeroCalculateViewport(&settings, 800, 600);
    CHECK(v.width == expected[i]);
    CHECK(v.width == 256 + 2 * v.extra);
    CHECK(FzeroHudAnchorX(v, 8, -1) == 8);
    CHECK(FzeroHudAnchorX(v, 248, 1) == v.width - 8);
    CHECK(FzeroHudAnchorX(v, 128, 0) == v.width / 2);
  }
  settings.aspect = FZERO_ASPECT_FIT;
  v = FzeroCalculateViewport(&settings, 5120, 1440);
  CHECK(v.width == 682);
  CHECK(FzeroCalculateViewport(&settings, 10000, 100).width == 682);
  CHECK(FzeroCalculateViewport(&settings, 600, 900).width == 256);
  CHECK(!FzeroCalculateViewport(&settings, 800, 600).enhanced);
  CHECK(FzeroCalculateViewport(&settings, 0, 0).width == 256);
  CHECK(FzeroCalculateViewport(&settings, 1920, 1080).width == 342);
  FzeroRect r = FzeroDestination(v, 1920, 1080);
  CHECK(r.w == 1920 && r.h == 540 && r.y == 270);
  r = FzeroDestination(v, 0, 0);
  CHECK(r.w == 0 && r.h == 0);
  settings.enhanced = false;
  r = FzeroDestination(FzeroCalculateViewport(&settings, 1920, 1080), 1920, 1080);
  CHECK(r.w == 1440 && r.h == 1080 && r.x == 240);
}

static void clock_tests(void) {
  const unsigned rates[] = {30, 60, 90, 120, 144, 165, 240, 360};
  uint64_t expected_frames = (uint64_t)ceil(600 * FZERO_SIMULATION_HZ);
  for (unsigned i = 0; i < sizeof(rates) / sizeof(*rates); ++i) {
    FzeroClock c;
    FzeroClockReset(&c, 0, rates[i]);
    /* Ten minutes of independent simulation/display events, including
     * display rates below the simulation rate and non-integer ratios. */
    double now;
    while ((now = FzeroClockNextDeadline(&c)) < 600) {
      if (FzeroClockSimulationDue(&c, now)) FzeroClockSimulationDone(&c);
      if (FzeroClockPresentationDue(&c, now)) {
        CHECK(FzeroClockAlpha(&c, now) >= 0 && FzeroClockAlpha(&c, now) <= 1);
        FzeroClockPresentationDone(&c, now);
      }
    }
    CHECK(c.simulation_frames == expected_frames);
    CHECK(llabs((long long)c.presentations - 600LL * rates[i]) <= 1);
    CHECK(c.missed_presentations == 0);
  }
  FzeroClock c;
  FzeroClockReset(&c, 10, 240);
  FzeroClockSimulationDone(&c);
  double simulation_deadline = c.next_simulation;
  FzeroClockPresentationDone(&c, 10.05);
  CHECK(c.missed_presentations >= 11);
  CHECK(c.next_presentation > 10.05);
  CHECK(c.next_simulation == simulation_deadline);
  CHECK(FzeroClockSimulationDue(&c, 10.05));
  FzeroClockReset(&c, 900, 120); /* resume/load discards paused wall time */
  FzeroClockSimulationDone(&c);
  CHECK(!FzeroClockSimulationDue(&c, 900));
  CHECK(fabs(FzeroClockAlpha(&c, 900 + 0.5 / FZERO_SIMULATION_HZ) - 0.5) < 1e-8);
  CHECK(FzeroClockAlpha(&c, 901) == 1);
  CHECK(FzeroClockAlpha(&c, 899) == 0);
  CHECK(FzeroPresentationHz(0, 59.94) == 59.94);
  CHECK(FzeroPresentationHz(0, 500) == 360);
  CHECK(FzeroPresentationHz(0, NAN) == 60);
  CHECK(FzeroPresentationHz(0, 0) == 60);
  CHECK(FzeroPresentationHz(144, 60) == 144);
}

static void config_tests(void) {
  FzeroVideoSettings a, b;
  FzeroVideoDefaults(&a);
  a.enhanced = true; a.aspect = FZERO_ASPECT_FIT; a.fps = 165;
  CHECK(FzeroVideoSave(&a, "test-video.ini"));
  CHECK(FzeroVideoLoad(&b, "test-video.ini"));
  CHECK(b.enhanced && b.aspect == FZERO_ASPECT_FIT && b.fps == 165);
  a.fps = 0; a.aspect = FZERO_ASPECT_21_9;
  CHECK(FzeroVideoSave(&a, "test-video.ini")); /* atomic replacement */
  CHECK(FzeroVideoLoad(&b, "test-video.ini"));
  CHECK(b.aspect == FZERO_ASPECT_21_9 && b.fps == 0);
  for (unsigned scale = 1; scale <= 64; ++scale) {
    char text[8]; snprintf(text, sizeof(text), "%u", scale);
    CHECK(FzeroParseHdScale(text, &a.hd_scale) && a.hd_scale == scale);
    CHECK(FzeroVideoSave(&a, "test-video.ini"));
    CHECK(FzeroVideoLoad(&b, "test-video.ini") && b.hd_scale == scale);
  }
  const char *invalid[] = {"", "0", "65", "-2", "+4", "4x", "6.5", "10junk", "4294967298", "999999999999999999999999"};
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    unsigned scale = 4;
    CHECK(!FzeroParseHdScale(invalid[i], &scale) && scale == 4);
    FILE *bad = fopen("test-video.ini", "w"); CHECK(bad);
    fprintf(bad, "Mode7Scale=%s\n", invalid[i]); fclose(bad);
    CHECK(!FzeroVideoLoad(&b, "test-video.ini") && b.hd_scale == 8);
  }
  FILE *f = fopen("test-video.ini", "w");
  CHECK(f);
  fputs("EnhancedRenderer=perhaps\nAspect=99:1\nPresentationFPS=120bad\nMode7Enabled=bad\nMode7Scale=9999\n", f);
  fclose(f);
  CHECK(!FzeroVideoLoad(&b, "test-video.ini"));
  CHECK(b.enhanced && b.aspect == FZERO_ASPECT_FIT && b.fps == 0); /* invalid fields fall back to shipped defaults */
  CHECK(b.hd_mode7 && b.hd_scale == 8);
  f = fopen("test-video.ini", "w"); CHECK(f);
  fputs("HDMode7=0\nHDMode7Scale=2\nAspect=21:9\nPresentationFPS=144\n", f); fclose(f);
  CHECK(FzeroVideoLoad(&b, "test-video.ini"));
  CHECK(b.hd_mode7 && b.hd_scale == 8 && b.aspect == FZERO_ASPECT_21_9 && b.fps == 144);
  /* Only absent or invalid fields take a default: a setting the file turned
   * off stays off however corrupt its neighbours are. */
  f = fopen("test-video.ini", "w");
  CHECK(f);
  fputs("EnhancedRenderer=0\nBSDeluxe=0\nAspect=99:1\nPresentationFPS=nope\n", f);
  fclose(f);
  CHECK(!FzeroVideoLoad(&b, "test-video.ini"));
  CHECK(!b.enhanced && !b.bs_deluxe);
  CHECK(b.aspect == FZERO_ASPECT_FIT && b.fps == 0 && !b.fps_enabled);
  CHECK(!b.bs_tracks && b.gameplay.enabled==FZERO_RULE_DEFAULT_MASK);
  f = fopen("test-video.ini", "w"); CHECK(f);
  fputs("BSDeluxe=1\n", f); fclose(f);
  CHECK(FzeroVideoLoad(&b, "test-video.ini") && b.bs_deluxe && b.bs_tracks);
  CHECK(b.gameplay.always_records); /* Older config without the testing key. */
  f = fopen("test-video.ini", "w"); CHECK(f);
  fputs("BSVehicles=0\nBSTracks=1\nBSDeluxe=1\n", f); fclose(f);
  CHECK(FzeroVideoLoad(&b, "test-video.ini") && !b.bs_deluxe && b.bs_tracks);
    b.gameplay.enabled=((1u<<FZERO_RULE_COUNT)-1)&~7u;
  b.gameplay.tuning=0; b.gameplay.boost=1; b.gameplay.exhaust=2;
  CHECK(FzeroVideoSave(&b,"test-video.ini") && FzeroVideoLoad(&a,"test-video.ini"));
  CHECK(!a.bs_deluxe && a.bs_tracks && a.gameplay.enabled==(b.gameplay.enabled & FZERO_RULE_SELECTABLE_MASK));
    CHECK(a.gameplay.tuning==0 && a.gameplay.boost==1 && a.gameplay.exhaust==2);
    b.gameplay.vehicle_packs=7;b.gameplay.stock_rebalance=15;
    CHECK(FzeroVideoSave(&b,"test-video.ini") && FzeroVideoLoad(&a,"test-video.ini"));
    CHECK(a.gameplay.vehicle_packs==7 && a.gameplay.stock_rebalance==15);
    b.bs_deluxe=true;
    CHECK(FzeroVideoSave(&b,"test-video.ini") && FzeroVideoLoad(&a,"test-video.ini"));
    CHECK(a.bs_deluxe && !a.gameplay.vehicle_packs && !a.gameplay.stock_rebalance);
    f=fopen("test-video.ini","w");CHECK(f);fputs("BSVehicles=0\nCGPRules=7\n",f);fclose(f);
    CHECK(FzeroVideoLoad(&a,"test-video.ini") && !a.gameplay.enabled && !a.gameplay.vehicle_packs && a.gameplay.stock_rebalance==15);
  /* A former smoothing opt-in is discarded without shifting MSU or any
   * unrelated option. Saving also clears stale bits from caller settings. */
  CHECK(FZERO_RULE_MSU==17 && FZERO_RULE_CREDITS==18);
  unsigned retained=(1u<<FZERO_RULE_LEGEND)|(1u<<FZERO_RULE_MSU);
  f=fopen("test-video.ini","w");CHECK(f);
  fprintf(f,"BSVehicles=0\nCGPRules=%u\nMode7Enabled=1\nMode7Scale=4\n",retained|(1u<<FZERO_RULE_RETIRED_FOG));
  fclose(f);
  CHECK(FzeroVideoLoad(&a,"test-video.ini") && a.gameplay.enabled==retained);
  CHECK(a.hd_mode7 && a.hd_scale==4);
  a.gameplay.enabled|=1u<<FZERO_RULE_RETIRED_FOG;
  CHECK(FzeroVideoSave(&a,"test-video.ini"));
  f=fopen("test-video.ini","r");CHECK(f);
  char setting[256];unsigned saved_rules=0;bool found_rules=false;
  while(fgets(setting,sizeof(setting),f))if(sscanf(setting,"CGPRules=%u",&saved_rules)==1)found_rules=true;
  fclose(f);CHECK(found_rules && saved_rules==retained);
  CHECK(FzeroVideoLoad(&b,"test-video.ini") && b.gameplay.enabled==retained);
  /* Migrate saved partial car packs to the complete roster, including when
   * skipping the launcher; an explicit BS vehicle choice still wins. */
  for(unsigned bs=0;bs<2;++bs) for(unsigned mask=0;mask<8;++mask) for(unsigned rebalance=0;rebalance<16;++rebalance) {
    f=fopen("test-video.ini","w");CHECK(f);
    fprintf(f,"BSVehicles=%u\nCGPCars=%u\nCGPStockRebalance=%u\n",bs,mask,rebalance);
    fclose(f);
    CHECK(FzeroVideoLoad(&a,"test-video.ini"));
    CHECK(a.gameplay.vehicle_packs==(!bs && mask?7u:0u));
    CHECK(a.gameplay.stock_rebalance==(!bs && rebalance?15u:0u));
    CHECK(FzeroVideoSave(&a,"test-video.ini") && FzeroVideoLoad(&b,"test-video.ini"));
    CHECK(b.gameplay.vehicle_packs==a.gameplay.vehicle_packs);
    CHECK(b.gameplay.stock_rebalance==a.gameplay.stock_rebalance);
  }
  remove("test-video.ini");
  CHECK(FzeroVideoLoad(&b, "test-video.ini"));
  CHECK(b.enhanced && b.fps_enabled && !b.bs_deluxe && b.aspect == FZERO_ASPECT_FIT);
  CHECK(b.local_two_player && b.gameplay.stock_rebalance==15);
  /* Explicit opt-outs survive saving, reloading and unrelated missing keys. */
  f=fopen("test-video.ini","w");CHECK(f);
  fputs("LocalTwoPlayer=0\nCGPStockRebalance=0\n",f);fclose(f);
  CHECK(FzeroVideoLoad(&a,"test-video.ini") && !a.local_two_player && !a.gameplay.stock_rebalance);
  CHECK(FzeroVideoSave(&a,"test-video.ini") && FzeroVideoLoad(&a,"test-video.ini"));
  CHECK(!a.local_two_player && !a.gameplay.stock_rebalance && !a.bs_deluxe);
  CHECK(b.gameplay.always_records); /* Missing key defaults on, including old configs. */
  CHECK(b.crash_replay); /* Old performance Diagnostics=0 never disables crash capture. */
  b.crash_replay=false;
  CHECK(FzeroVideoSave(&b,"test-video.ini") && FzeroVideoLoad(&a,"test-video.ini") && !a.crash_replay);
  b.gameplay.always_records=false;
  CHECK(FzeroVideoSave(&b,"test-video.ini") && FzeroVideoLoad(&a,"test-video.ini") && !a.gameplay.always_records);
  remove("test-video.ini");
  CHECK(!b.bs_tracks && b.gameplay.enabled==FZERO_RULE_DEFAULT_MASK); /* Legend is offered by default; native BS tracks stay off. */
  CHECK(!FzeroValidFps(61));
  FzeroAspect aspect;
  CHECK(FzeroParseAspect("32:9", &aspect) && aspect == FZERO_ASPECT_32_9);
  CHECK(!FzeroParseAspect("Garbage", &aspect));
}

int main(void) {
  FzeroVideoSettings replay;
  FzeroVideoDefaults(&replay);
  CHECK(FzeroReplayConfigure("0:8,10-20:1,15:256", "0:16:9,12:32:9,22:4:3"));
  CHECK(FzeroReplayHasInput());
  CHECK(FzeroReplayInput(0) == 8 && FzeroReplayInput(9) == 0);
  CHECK(FzeroReplayInput(15) == 257 && FzeroReplayInput(20) == 1);
  CHECK(FzeroReplayInput(21) == 0);
  CHECK(FzeroReplayViewport(0, &replay) && replay.enhanced);
  CHECK(!FzeroReplayViewport(1, &replay));
  CHECK(FzeroReplayViewport(12, &replay) && replay.aspect == FZERO_ASPECT_32_9);
  CHECK(FzeroReplayViewport(22, &replay) && !replay.enhanced);
  CHECK(!FzeroReplayConfigure("-1:1", NULL));
  CHECK(!FzeroReplayConfigure(" -1:1", NULL));
  CHECK(!FzeroReplayConfigure("20-10:1", NULL));
  CHECK(!FzeroReplayConfigure("0:4096", NULL));
  CHECK(!FzeroReplayConfigure("0:1,", NULL));
  CHECK(!FzeroReplayConfigure(NULL, "1:16:9,1:32:9"));
  CHECK(!FzeroReplayConfigure(NULL, "1:16:9,"));
  CHECK(FzeroReplayConfigure(NULL, NULL) && !FzeroReplayHasInput());
  int width = 0, height = 0;
  CHECK(FzeroReplayConfigure(NULL, "10:Fit@1280x720,20:Fit@5120x1440"));
  CHECK(!FzeroReplayWindow(9, &width, &height));
  CHECK(FzeroReplayWindow(10, &width, &height) && width == 1280 && height == 720);
  CHECK(FzeroReplayWindow(20, &width, &height) && width == 5120 && height == 1440);
  CHECK(!FzeroReplayConfigure(NULL, "0:Fit@0x720"));
  CHECK(!FzeroReplayConfigure(NULL, "0:Fit@1280x0"));
  viewport_tests();
  clock_tests();
  config_tests();
  puts("F-Zero video: viewport, anchors, clock, and config checks passed");
  return 0;
}
