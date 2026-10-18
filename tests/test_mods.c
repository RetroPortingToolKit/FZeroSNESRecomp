#include "fzero_mods.h"
#include "fzero_netplay_mods.h"
#include "fzero_tracks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "%d: %s\n", __LINE__, #e); exit(1); } } while (0)
int main(void) {
  remove("test-mod-tracks/loader.cfg");
  CHECK(FzeroTracksInit("test-mod-tracks", true));
  FzeroVideoSettings s, loaded; FzeroVideoStock(&s); /* start from nothing enabled to test each toggle */
  const RecompLauncherCModProvider *p = FzeroModsProvider(&s, "test-mods.ini", true);
  RecompLauncherCModFeature w, f;
  RecompLauncherCModOption option;
  const CpCatalog *catalog = FzeroTracksCatalog();
  CHECK(p->package_count(NULL) == 6 + FZERO_RULE_COUNT && p->feature_count(NULL) == p->package_count(NULL));
  CHECK(!p->feature_enable(NULL, "track-library", "cups", 1));
  const CpPack *max = cp_catalog_find(catalog,"max-league");
  if (max && FzeroTracksHidden(max)) {
    CHECK(!FzeroTracksEnabled(max) && !FzeroTracksEnable(max,true));
    CHECK(!p->feature_enable(NULL,"max-league","tracks",1));
    CHECK(p->feature_resource_count(NULL,"max-league","tracks") == 0);
  }
  for (int i = 2 + FZERO_RULE_COUNT; i < p->feature_count(NULL); ++i) {
    RecompLauncherCModFeature pack;
    RecompLauncherCModPackage package;
    CHECK(p->feature_get(NULL, i, &pack) && p->package_get(NULL, i, &package));
    if(!strcmp(pack.id,"local-2p")) {
      CHECK(!strcmp(pack.package_id,"fzero-local-2p") && !strcmp(pack.group,"Game Modes"));
      CHECK(p->feature_enable(NULL,pack.package_id,pack.id,1));
      CHECK(p->commit(NULL,NULL) && FzeroVideoLoad(&loaded,"test-mods.ini") && loaded.local_two_player);
      CHECK(!s.gameplay.enabled && !s.bs_deluxe && !s.bs_tracks);
      CHECK(p->feature_enable(NULL,pack.package_id,pack.id,0));
      continue;
    }
    if (!strcmp(pack.id, "crash-replay")) {
      CHECK(!strcmp(pack.package_id, "fzero-crash-replay") && !pack.option_count);
      CHECK(p->feature_enable(NULL, pack.package_id, pack.id, 1) && s.crash_replay);
      CHECK(p->feature_get(NULL, i, &pack) && pack.enabled);
      CHECK(p->commit(NULL, NULL) && FzeroVideoLoad(&loaded, "test-mods.ini") && loaded.crash_replay);
      CHECK(p->feature_enable(NULL, pack.package_id, pack.id, 0) && !s.crash_replay);
      CHECK(p->commit(NULL, NULL) && FzeroVideoLoad(&loaded, "test-mods.ini") && !loaded.crash_replay);
      continue;
    }
    if (!strcmp(pack.id, "menu-music")) {
      CHECK(!strcmp(pack.package_id, package.id) && pack.option_count == 1 && pack.enabled);
      CHECK(p->feature_resource_count(NULL, pack.package_id, pack.id) == 6);
      static const char *cues[] = {"countdown", "ready", "lost-life", "title", "select", "ending"};
      for (int cue = 0; cue < 6; ++cue) {
        RecompLauncherCModResource music;
        CHECK(p->feature_resource_get(NULL, pack.package_id, pack.id, cue, &music));
        CHECK(!strcmp(music.id, cues[cue]) && !strcmp(music.file_patterns, "*.pcm"));
        CHECK(!p->feature_resource_set_path(NULL, pack.package_id, pack.id, music.id, "missing.pcm"));
        CHECK(p->feature_resource_set_path(NULL, pack.package_id, pack.id, music.id, ""));
      }
      CHECK(p->feature_option_get(NULL, pack.package_id, pack.id, 0, &option));
      CHECK(option.type == RECOMP_MOD_OPTION_CHOICE && option.choice_count >= 1);
      CHECK(p->feature_enable(NULL, pack.package_id, pack.id, 0));
      CHECK(p->feature_get(NULL, i, &pack) && !pack.enabled);
      CHECK(p->feature_enable(NULL, pack.package_id, pack.id, 1));
      continue;
    }
    CHECK(strcmp(pack.package_id, "track-library") && !strcmp(pack.id, "tracks"));
    CHECK(!strcmp(pack.package_id, package.id) && pack.option_count == 0);
    RecompLauncherCModResource resource;
    CHECK(p->feature_resource_count(NULL,pack.package_id,pack.id)==0);
    CHECK(!p->feature_resource_get(NULL,pack.package_id,pack.id,0,&resource));
    CHECK(!p->feature_resource_set_path(NULL,pack.package_id,pack.id,"patch",""));
    CHECK(!p->feature_option_get(NULL, pack.package_id, pack.id, 0, &option));
    CHECK(p->feature_enable(NULL, pack.package_id, pack.id, 0));
    CHECK(p->feature_get(NULL, i, &pack) && !pack.enabled && !strcmp(pack.status, "Disabled"));
    CHECK(p->feature_enable(NULL, pack.package_id, pack.id, 1));
    CHECK(p->feature_get(NULL, i, &pack) && pack.enabled);
    CHECK(!s.enhanced && !s.bs_deluxe && !s.hd_mode7 && !s.fps_enabled);
  }
  unsigned seen_rules = 0;
  for (int index=6;index<p->feature_count(NULL);++index) {
    RecompLauncherCModFeature rule;
    CHECK(p->feature_get(NULL,index,&rule));
    if (strcmp(rule.id,"rules")) continue;
    CHECK(!rule.enabled);
    unsigned i=0; while(i<FZERO_RULE_COUNT && strcmp(rule.package_id,fzero_rules[i].id)) ++i;
    CHECK(i<FZERO_RULE_COUNT && ((1u<<i)&FZERO_RULE_SELECTABLE_MASK));
    seen_rules |= 1u<<i;
    CHECK(p->feature_resource_count(NULL,rule.package_id,rule.id)==0);
    CHECK(p->feature_enable(NULL,rule.package_id,rule.id,1));
    CHECK((s.gameplay.enabled & (1u<<i))!=0);
    CHECK(p->feature_enable(NULL,rule.package_id,rule.id,0));
    CHECK(!rule.option_count);
  }
  CHECK(seen_rules == (FZERO_RULE_SELECTABLE_MASK & ~(1u << FZERO_RULE_MSU)));
  CHECK(!p->feature_enable(NULL,"cgp-dmag","rules",1));
  CHECK(!p->feature_enable(NULL,"cgp-up-magnet","rules",1));
  CHECK(!p->feature_enable(NULL,"cgp-rainbow","rules",1));
  CHECK(!s.gameplay.enabled);
  CHECK(p->feature_enable(NULL,"fzero-always-records","always-records",1));
  CHECK(s.gameplay.always_records);
  CHECK(p->commit(NULL,NULL) && FzeroVideoLoad(&loaded,"test-mods.ini") && loaded.gameplay.always_records);
  CHECK(p->feature_enable(NULL,"fzero-always-records","always-records",0));
  CHECK(p->commit(NULL,NULL) && FzeroVideoLoad(&loaded,"test-mods.ini") && !loaded.gameplay.always_records);
  CHECK(!p->feature_enable(NULL,"cgp-credits","rules",1));
  CHECK(!p->feature_enable(NULL,"cgp-tuning","rules",1));
  CHECK(!p->feature_enable(NULL,"cgp-boost","rules",1));
  CHECK(!p->feature_enable(NULL,"cgp-exhaust","rules",1));
  const char *old_car_packs[]={"cgp-cars-p1","cgp-cars-p2","cgp-cars-p3"};
  for(unsigned i=0;i<3;++i)
    CHECK(!p->feature_enable(NULL,old_car_packs[i],"vehicles",1));
  unsigned cgp_entries=0, rebalance_entries=0;
  int cgp_car_index=-1;
  for(int i=0;i<p->feature_count(NULL);++i) {
    CHECK(p->feature_get(NULL,i,&f));
    if(!strcmp(f.group,"Vehicle Rebalances")) {
      CHECK(!strcmp(f.package_id,"cgp-stock-rebalance") && !f.option_count && !f.enabled);
      ++rebalance_entries;
    }
    if(!strcmp(f.group,"Vehicle Packs") && strcmp(f.package_id,"bs-cars")) {
      CHECK(!strcmp(f.package_id,"cgp-cars") && !strcmp(f.name,"CGP vehicles"));
      ++cgp_entries;cgp_car_index=i;
    }
  }
  CHECK(cgp_entries==1 && rebalance_entries==1);
  for(unsigned enabled=0;enabled<2;++enabled) {
    CHECK(p->feature_enable(NULL,"cgp-cars","vehicles",enabled));
    CHECK(s.gameplay.vehicle_packs==(enabled?7u:0u) && !s.bs_deluxe);
    CHECK(p->feature_get(NULL,cgp_car_index,&f) && f.enabled==(int)enabled);
    CHECK(p->commit(NULL,NULL) && FzeroVideoLoad(&loaded,"test-mods.ini"));
    CHECK(loaded.gameplay.vehicle_packs==s.gameplay.vehicle_packs && !loaded.bs_deluxe);
  }
  CHECK(p->feature_enable(NULL,"bs-cars","vehicles",1));
  CHECK(s.bs_deluxe && !s.gameplay.vehicle_packs);
  CHECK(p->feature_enable(NULL,"cgp-cars","vehicles",1));
  CHECK(!s.bs_deluxe && s.gameplay.vehicle_packs==7);
  /* The unified roster does not select or clear the original-car rebalances. */
  CHECK(!s.gameplay.stock_rebalance);
  CHECK(p->feature_enable(NULL,"cgp-stock-rebalance","vehicles",1));
  CHECK(p->feature_enable(NULL,"cgp-cars","vehicles",0));
  CHECK(!s.gameplay.vehicle_packs && s.gameplay.stock_rebalance==15);
  CHECK(p->feature_enable(NULL,"cgp-cars","vehicles",1));
  CHECK(s.gameplay.vehicle_packs==7 && s.gameplay.stock_rebalance==15);
  CHECK(p->feature_enable(NULL,"cgp-stock-rebalance","vehicles",1));
  CHECK(s.gameplay.stock_rebalance==15 && !s.bs_deluxe);
  CHECK(p->feature_enable(NULL,"bs-cars","vehicles",1));
  CHECK(s.bs_deluxe && !s.gameplay.vehicle_packs && !s.gameplay.stock_rebalance);
  CHECK(p->feature_enable(NULL,"bs-cars","vehicles",0));
  for(unsigned enabled=0; enabled<2; ++enabled) {
    CHECK(p->feature_enable(NULL,"cgp-stock-rebalance","vehicles",enabled));
    CHECK(s.gameplay.stock_rebalance==(enabled?15u:0u) && !s.gameplay.vehicle_packs);
    CHECK(p->commit(NULL,NULL) && FzeroVideoLoad(&loaded,"test-mods.ini"));
    CHECK(loaded.gameplay.stock_rebalance==s.gameplay.stock_rebalance);
  }
  CHECK(p->feature_enable(NULL,"cgp-stock-rebalance","vehicles",0));
  const CpPack *cgp=cp_catalog_find(catalog,"cgp");
  CHECK(p->feature_enable(NULL,"bs-tracks","tracks",1) && s.bs_tracks);
  CHECK(!s.bs_deluxe);
  if(cgp) {
    CHECK(!FzeroTracksEnabled(cgp));
    CHECK(p->feature_enable(NULL,"track-pack-loader","tracks",1) && !s.bs_tracks);
    CHECK(!s.bs_deluxe);
  }
  CHECK(p->feature_enable(NULL,"bs-tracks","tracks",0));
  RecompLauncherCModFeature diag;
  CHECK(p->feature_get(NULL, 4, &diag) && !diag.enabled && diag.option_count == 0);
  CHECK(!p->feature_option_get(NULL, diag.package_id, diag.id, 0, &option));
  CHECK(p->feature_enable(NULL, diag.package_id, diag.id, 1) && s.diagnostics);
  CHECK(!s.enhanced && !s.hd_mode7 && !s.fps_enabled);
  RecompLauncherCModFeature hd;
  CHECK(p->feature_get(NULL, 3, &hd) && !hd.enabled && hd.option_count == 1);
  CHECK(p->feature_enable(NULL, hd.package_id, hd.id, 1));
  CHECK(s.hd_mode7 && !s.enhanced && !s.fps_enabled);
  CHECK(p->feature_set_option(NULL, hd.package_id, hd.id, "scale", "4"));
  CHECK(s.hd_scale == 4);
  CHECK(p->feature_get(NULL, 3, &hd) && !strstr(hd.description, "Warning:"));
  for (unsigned scale = 5; scale <= 10; ++scale) {
    char text[8]; snprintf(text, sizeof(text), "%u", scale);
    CHECK(p->feature_set_option(NULL, hd.package_id, hd.id, "scale", text));
    CHECK(s.hd_scale == scale);
    CHECK(p->feature_get(NULL, 3, &hd));
    CHECK(strstr(hd.description, "Warning:") && strstr(hd.description, "own risk"));
    CHECK(!hd.has_error); /* high-cost values remain usable */
  }
  CHECK(!p->feature_set_option(NULL, hd.package_id, hd.id, "scale", "11"));
  CHECK(!p->feature_set_option(NULL, hd.package_id, hd.id, "scale", "6.5"));
  CHECK(s.hd_scale == 10 && strstr(p->last_error(NULL), "2 to 10"));
  CHECK(p->feature_option_get(NULL, hd.package_id, hd.id, 0, &option));
  CHECK(!strcmp(option.value, "10") && !strcmp(option.default_value, "2"));
  CHECK(option.type == RECOMP_MOD_OPTION_INTEGER && option.min_value == 2 && option.max_value == 10);
  RecompLauncherCModFeature deluxe;
  CHECK(p->feature_get(NULL, 2, &deluxe) && deluxe.option_count == 0);
  CHECK(p->feature_enable(NULL, deluxe.package_id, deluxe.id, 1));
  CHECK(s.bs_deluxe && !s.fps_enabled && !s.enhanced);
  CHECK(!p->feature_option_get(NULL, deluxe.package_id, deluxe.id, 0, &option));
  CHECK(p->feature_get(NULL, 0, &w) && p->feature_get(NULL, 1, &f));
  CHECK(strcmp(w.package_id, f.package_id) && w.option_count == 1 && f.option_count == 1);
  CHECK(p->feature_enable(NULL, f.package_id, f.id, 1));
  CHECK(s.fps_enabled && !s.enhanced);
  CHECK(p->feature_set_option(NULL, f.package_id, f.id, "fps", "144"));
  CHECK(!p->feature_set_option(NULL, w.package_id, w.id, "fps", "60"));
  CHECK(p->feature_enable(NULL, w.package_id, w.id, 1));
  CHECK(p->feature_enable(NULL, f.package_id, f.id, 0));
  CHECK(s.enhanced && !s.fps_enabled && s.fps == 144);
  CHECK(p->feature_option_get(NULL, w.package_id, w.id, 0, &option) && !strcmp(option.id, "aspect"));
  CHECK(p->feature_option_get(NULL, f.package_id, f.id, 0, &option) && !strcmp(option.id, "fps"));
  CHECK(p->commit(NULL, NULL) && FzeroVideoLoad(&loaded, "test-mods.ini"));
  CHECK(loaded.enhanced && !loaded.fps_enabled && loaded.fps == 144 && loaded.bs_deluxe);
  CHECK(loaded.hd_mode7 && loaded.hd_scale == 10 && loaded.diagnostics);
  CHECK(p->feature_enable(NULL, diag.package_id, diag.id, 0) && !s.diagnostics);
  CHECK(p->commit(NULL, NULL) && FzeroVideoLoad(&loaded, "test-mods.ini") && !loaded.diagnostics);
  CHECK(p->feature_enable(NULL, hd.package_id, hd.id, 0));
  CHECK(!s.hd_mode7 && s.hd_scale == 10 && s.enhanced);
  CHECK(p->feature_enable(NULL, deluxe.package_id, deluxe.id, 0));
  CHECK(!s.bs_deluxe && s.enhanced && !s.fps_enabled);
  /* One global title feature; no per-pack dropdowns or file pickers. */
  RecompLauncherCModFeature title;
  CHECK(p->feature_get(NULL, FZERO_RULE_COUNT, &title));
  CHECK(!strcmp(title.package_id,"fzero-title") && !strcmp(title.group,"Presentation"));
  CHECK(title.option_count==1 && !title.enabled);
  CHECK(p->feature_resource_count(NULL,"fzero-title","title-screen")==0);
  CHECK(p->feature_option_get(NULL,"fzero-title","title-screen",0,&option));
  CHECK(!strcmp(option.value,"original") && !strcmp(option.default_value,"original"));
  CHECK(option.choice_count==3);
  const char *screens[]={"original","cgp","max-league"};
  for (int i=0;i<3;++i) {
    RecompLauncherCModChoice choice;
    CHECK(p->feature_choice_get(NULL,"fzero-title","title-screen","screen",i,&choice));
    CHECK(!strcmp(choice.value,screens[i]));
    CHECK(p->feature_set_option(NULL,"fzero-title","title-screen","screen",screens[i]));
  }
  CHECK(!p->feature_set_option(NULL,"fzero-title","title-screen","screen","fzero-55"));
  CHECK(!p->feature_set_option(NULL,"fzero-title","title-screen","screen","unknown"));
  CHECK(p->feature_enable(NULL,"fzero-title","title-screen",1));
  CHECK(p->feature_enable(NULL,"track-pack-loader","tracks",0));
  CHECK(p->commit(NULL,NULL) && FzeroTracksInit("test-mod-tracks",true));
  CHECK(FzeroTracksTitleEnabled() && !strcmp(FzeroTracksTitleStyle(),"max-league"));
  CHECK(!FzeroTracksEnabled(cp_catalog_find(FzeroTracksCatalog(),"cgp")));
  CHECK(p->feature_enable(NULL,"fzero-title","title-screen",0));
  CHECK(p->commit(NULL,NULL) && FzeroTracksInit("test-mod-tracks",true));
  CHECK(!FzeroTracksTitleEnabled() && !strcmp(FzeroTracksTitleStyle(),"max-league"));
  /* Presets select the loader as a unit and retain unrelated display choices. */
  const CpPack *bower=cp_catalog_find(FzeroTracksCatalog(),"bower-league");
  max=cp_catalog_find(FzeroTracksCatalog(),"max-league");
  cgp=cp_catalog_find(FzeroTracksCatalog(),"cgp");
  CHECK(p->preset_count(NULL)==3);
  for (unsigned extra=0; extra<4; ++extra) {
    s.gameplay.always_records=(extra&1)!=0;
    if (max) CHECK(FzeroTracksEnable(max,(extra&1)!=0));
    if (bower) CHECK(FzeroTracksEnable(bower,(extra&2)!=0));
    s.enhanced=true;s.fps_enabled=true;s.fps=144;s.hd_mode7=true;s.hd_scale=3;s.diagnostics=true;
    for (int pick=0; pick<3; ++pick) {
      RecompLauncherCSettings io={0};io.volume=37;io.rewind_enabled=1;io.fullscreen=2;
      snprintf(io.msu1_dir,sizeof(io.msu1_dir),"my custom soundtrack");
      RecompLauncherCModPreset recipe;
      CHECK(p->preset_get(NULL,pick,&recipe));
      CHECK(p->preset_apply(NULL,recipe.id,&io));
      CHECK(!strcmp(p->preset_current(NULL,&io),recipe.id));
      if(max)CHECK(FzeroTracksEnabled(max)==(pick==2));
      if(bower)CHECK(FzeroTracksEnabled(bower)==(pick==2));
      CHECK(io.volume==37 && io.rewind_enabled==1 && io.fullscreen==2);
      CHECK(!strcmp(io.msu1_dir,"my custom soundtrack"));
      CHECK(s.enhanced && s.fps_enabled && s.fps==144 && s.hd_mode7 && s.hd_scale==3 && s.diagnostics);
      CHECK(s.bs_tracks==(pick==1) && s.bs_deluxe==(pick==1));
      CHECK(s.gameplay.vehicle_packs==(pick==2?7u:0u));
      CHECK(s.gameplay.stock_rebalance==(pick==2?15u:0u));
      CHECK(s.gameplay.enabled==(pick==2?FZERO_RULE_SELECTABLE_MASK:0u));
      CHECK(s.gameplay.always_records==((extra&1)!=0));
      CHECK(io.msu1_enabled==(pick==2));
      CHECK(FzeroTracksEnabled(cgp)==(pick==2));
      CHECK(FzeroTracksTitleEnabled()==(pick==2));
      CHECK(p->commit(NULL,NULL) && FzeroVideoLoad(&loaded,"test-mods.ini"));
      CHECK(!memcmp(&loaded.gameplay,&s.gameplay,sizeof(s.gameplay)));
      CHECK(!p->preset_apply(NULL,"invalid",&io));
      CHECK(!strcmp(p->preset_current(NULL,&io),recipe.id));
      if(pick==2) {
        CHECK(p->feature_enable(NULL,"cgp-legend","rules",0));
        CHECK(!*p->preset_current(NULL,&io));
        CHECK(p->preset_apply(NULL,"cgp",&io));
        io.msu1_enabled=0;CHECK(!*p->preset_current(NULL,&io));
      }
    }
  }
  /* The same executable also ships without PCM files. CGP keeps SNES audio
   * when recordings are missing; adding them later needs no source selection. */
  p = FzeroModsProvider(&s, "test-mods.ini", false);
  for (unsigned custom=0; custom<2; ++custom) {
    RecompLauncherCSettings io={0};
    if (custom) snprintf(io.msu1_dir,sizeof(io.msu1_dir),"my external CGP music");
    CHECK(p->preset_apply(NULL,"cgp",&io));
    CHECK(!strcmp(p->preset_current(NULL,&io),"cgp"));
    CHECK(io.msu1_enabled && !io.msu1_pack[0]);
    CHECK(!strcmp(io.msu1_dir,custom ? "my external CGP music" : ""));
    CHECK(s.gameplay.vehicle_packs==7 && s.gameplay.stock_rebalance==15);
    CHECK(s.gameplay.enabled & (1u<<FZERO_RULE_LEGEND));
    CHECK(p->preset_apply(NULL,"vanilla",&io) && !io.msu1_enabled);
    CHECK(!strcmp(io.msu1_dir,custom ? "my external CGP music" : ""));
  }
  /* Fresh defaults must survive provider normalization and reach the actual
   * launcher toggles; the formerly default-on BS pack cleared rebalances. */
  FzeroVideoSettings fresh;FzeroVideoDefaults(&fresh);
  p=FzeroModsProvider(&fresh,"test-mods.ini",false);
  unsigned defaults_seen=0;
  for(int i=0;i<p->feature_count(NULL);++i) {
    CHECK(p->feature_get(NULL,i,&f));
    if(!strcmp(f.package_id,"cgp-stock-rebalance")) {CHECK(f.enabled);defaults_seen|=1;}
    if(!strcmp(f.package_id,"fzero-local-2p")) {CHECK(f.enabled);defaults_seen|=2;}
  }
  CHECK(defaults_seen==3 && !fresh.bs_deluxe && fresh.gameplay.stock_rebalance==15);
  CHECK(p->commit(NULL,NULL) && FzeroVideoLoad(&loaded,"test-mods.ini"));
  CHECK(loaded.local_two_player && loaded.gameplay.stock_rebalance==15 && !loaded.bs_deluxe);
  CHECK(p->feature_enable(NULL,"fzero-local-2p","local-2p",0));
  CHECK(p->feature_enable(NULL,"cgp-stock-rebalance","vehicles",0));
  CHECK(p->commit(NULL,NULL) && FzeroVideoLoad(&loaded,"test-mods.ini"));
  CHECK(!loaded.local_two_player && !loaded.gameplay.stock_rebalance);
  const RecompLauncherCModProvider *net=FzeroNetplayMods(p);
  RecompLauncherCModPackage pkg;
  FzeroNetplayModsMode(true);
  CHECK(net->feature_count(NULL)==3 && net->package_count(NULL)==3 && !net->preset_count(NULL));
  for(int i=0;i<3;++i) {
    CHECK(net->feature_get(NULL,i,&f) && *f.name && !f.hidden && !f.option_count);
    CHECK(net->package_get(NULL,i,&pkg) && !strcmp(pkg.id,f.package_id));
    CHECK(!net->feature_enable(NULL,f.package_id,f.id,!f.enabled));
    CHECK(*net->last_error(NULL));
  }
  CHECK(net->commit_netplay(NULL,NULL));
  CHECK(!fresh.local_two_player && !fresh.gameplay.stock_rebalance);
  FzeroNetplayModsMode(false);
  CHECK(net->feature_count(NULL)==p->feature_count(NULL));
  CHECK(net->feature_enable(NULL,"fzero-local-2p","local-2p",1));
  remove("test-mods.ini");
  puts("Independent widescreen and presentation FPS plugins passed");
  return 0;
}
