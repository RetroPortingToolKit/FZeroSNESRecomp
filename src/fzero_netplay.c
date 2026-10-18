#include "fzero_netplay.h"
#include "fzero_capture.h"
#include "fzero_build.h"
#include "fzero_runtime.h"
#include "fzero_tracks.h"
#include "common_rtl.h"
#include "host_paths.h"
#include "netplay/snes_netplay.h"
#include "netplay/snes_netplay_rb.h"
#include "netplay/snes_state_digest.h"
#include "netplay/snes_host_app.h"
#include "netplay/snes_host_lobby.h"
#include "netplay/snes_netplay_identity.h"
#include "recomp_netplay_host.h"
#include "crc32.h"
#include "diagnostics/trace_writer.h"
#include "desktop/sdl_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool initialized,from_lobby,resume,menu_mode;
static unsigned aspect=FZERO_ASPECT_16_9;
static char identity[512],base_identity[400],lobby_version[64];
static const RecompLauncherCNetplayCallbacks *original;
static RecompLauncherCNetplayCallbacks callbacks;
static const RecompLauncherCModProvider *original_mods;
static RecompLauncherCModProvider mods;
static SnesTraceWriter *trace;
static bool exit_ready;
static struct {uint32_t tick,confirmed,episodes,desync;uint64_t resim;} last_stats;
static void remember_stats(void) {
  last_stats.tick=snes_netplay_sim_tick();last_stats.confirmed=snes_netplay_rb_confirmed_through();
  last_stats.episodes=snes_netplay_rb_episode_count();last_stats.desync=snes_netplay_rb_desync_count();
  last_stats.resim=snes_netplay_rb_resim_ticks();
}
static int path(void *ctx,const char *leaf,char *out,size_t n) {(void)ctx;return snesrecomp_exe_dir_path(leaf,out,n);}
static int store_name(void *ctx,const char *name) {(void)ctx;return snes_netplay_identity_store(name);}
static int load_name(void *ctx,char *out,size_t n) {(void)ctx;return snes_netplay_identity_load(out,n);}
static int no(void *ctx) {(void)ctx;return 0;}
static const char *caption(void *ctx) {(void)ctx;return "Viewport";}
static int feature_count(void *ctx) {return menu_mode?0:original_mods->feature_count(ctx);}
static int package_count(void *ctx) {return menu_mode?0:original_mods->package_count(ctx);}
static int preset_count(void *ctx) {return menu_mode?0:original_mods->preset_count(ctx);}
static int net_commit(void *ctx,const char *rom) {(void)ctx;(void)rom;return 1;}
static void mode(int active) {menu_mode=active!=0;}
static int create(void *ctx,const char *name,char *endpoint,const char *password,
                  const RecompLauncherCSettings *settings,int lan,int seats) {
  (void)seats;
  if(original->allow_spectators_set)original->allow_spectators_set(ctx,0);
  return original->create(ctx,name,endpoint,password,settings,lan,2);
}
static int fill_launch(void *ctx,RecompLauncherCNetplayLaunch *out) {
  if(!original->fill_launch(ctx,out))return 0;
  if(out->is_spectator || out->host_spectates || out->player_count!=2 || out->max_slots!=2 ||
      out->session_variant<0 || out->session_variant>2 || !out->rollback) {
    recomp_netplay_host_set_runtime_error("F-Zero requires two players and rollback.");return 0;
  }
  return 1;
}
static void fill_caps(void *ctx,const RecompLauncherCSettings *s,RNetLobbyMatchCaps *caps) {
  (void)ctx;(void)s;caps->rollback=1;
}
static int fork(void *ctx,uint32_t *tick,const char **part,uint32_t *mine,uint32_t *theirs) {
  (void)ctx;if(!snes_netplay_rb_last_fork(tick,part))return 0;
  snes_netplay_rb_fork_digests(mine,theirs);return 1;
}
void FzeroNetplayLauncher(RecompLauncherCGameInfo *info) {
  if(!initialized) {
    snes_lobby_configure();
    static const RecompNetplaySessionVariant variants[]={{0,"4:3"},{1,"16:9"},{2,"21:9"}};
    /* LAN room versions have a 32-byte wire field. Keep this discovery tag
     * within it; the full build/catalog identity is checked before frame 0. */
    snprintf(lobby_version,sizeof(lobby_version),"fzero-vs7-%.20s",FZERO_REPLAY_BUILD_ID);
    RecompNetplayHostHooks h={0};h.game_name="F-Zero Forever";h.game_version=lobby_version;
    h.platform="snes";h.legacy_env_prefix="SNES_NET_";h.default_lobby_name="F-Zero Forever VS";
    h.lan_registry_path="fzero-netplay-lan.txt";h.max_players=2;h.netplay_max_players=2;
    h.slot_policy=RECOMP_NETPLAY_SLOTS_HOST_FIRST;h.input_player=RECOMP_NETPLAY_INPUT_AUTO;
    h.exe_dir_path=path;h.name_store=store_name;h.name_load=load_name;
    h.caps_codec=snes_lobby_caps_codec();h.fill_match_caps=fill_caps;h.last_fork=fork;
    h.session_variants=variants;h.session_variant_count=3;h.default_session_variant=1;
    if(recomp_netplay_host_init(&h)!=0)return;
    original=recomp_netplay_host_callbacks();callbacks=*original;
    /* The getter can return a persisted name before the lobby client has a
     * live identity. Install it before creating/joining a room too. */
    const char *name=original->player_name(original->ctx);
    if(name && *name)original->set_player_name(original->ctx,name);
    callbacks.create=create;callbacks.fill_launch=fill_launch;callbacks.host_can_spectate=no;
    callbacks.allow_spectators_get=NULL;callbacks.allow_spectators_set=NULL;
    callbacks.automatch_available=no;callbacks.session_variant_caption=caption;
    initialized=true;
  }
  info->netplay_supported=1;info->netplay=&callbacks;info->netplay_mode_changed=mode;
  if(info->mods && info->mods!=&mods) {
    original_mods=info->mods;mods=*original_mods;
    mods.feature_count=feature_count;mods.package_count=package_count;mods.preset_count=preset_count;
    mods.commit_netplay=net_commit;info->mods=&mods;
  }
  if(resume) {
    info->resume_netplay_room=1;info->resume_netplay_endpoint=recomp_netplay_host_resume_endpoint();resume=false;
  }
}
bool FzeroNetplayResume(void) {return resume;}
bool FzeroNetplayWanted(const RecompLauncherCSettings *s) {
  const char *direct=getenv("SNES_NET_ENABLE");
  return s->netplay_launch.enabled || (direct && !strcmp(direct,"1"));
}
static uint32_t hash(const void *p,size_t n) {return crc32_final(crc32_update(CRC32_INIT,p,n));}
static int check_set(const char *want,char *reason,uint32_t capacity) {
  if(want && !strcmp(want,identity))return 0;
  snprintf(reason,capacity,"F-Zero build, course catalog, viewport or execution profile differs. Match the host's build and enabled course packs.");
  return 1;
}
static void profile_settled(uint32_t profile,uint32_t transcript) {
  snprintf(identity,sizeof(identity),"%s;startup=%u:%08x",base_identity,(unsigned)profile,(unsigned)transcript);
  snes_netplay_rb_set_identity(hash(FZERO_REPLAY_BUILD_ID,64),hash(identity,strlen(identity)));
  snes_netplay_rb_set_modset(identity,check_set,NULL);
}
static uint32_t game_digest(void) {
  static uint8_t *image;static size_t capacity;
  size_t n=FzeroLocalSaveSize();if(capacity<n) {free(image);image=malloc(n);capacity=image?n:0;}
  uint8_t online[FZERO_ONLINE_STATE_SIZE];FzeroOnlineSave(online);
  uint32_t crc=crc32_update(CRC32_INIT,online,sizeof(online));
  if(image && FzeroLocalSave(image,n))crc=crc32_update(crc,image,n);
  else abort(); /* Never silently exclude authoritative state on allocation failure. */
  return crc32_final(crc);
}
static void trace_frame(void) {
  if(!trace)return;
  const FzeroOnlineState *s=FzeroOnlineCompleted();
  const FzeroVersusSnapshot *r=FzeroVersusCompleted(FzeroLocalSession());char row[384];
  int n=snprintf(row,sizeof(row),"%llu,%08x,%u,%u,%u,%u,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
      (unsigned long long)s->tick,(unsigned)game_digest(),s->phase,s->config.course,
      s->config.machine[0],s->config.machine[1],(unsigned long long)(r?r->race_tick:0),
      r?r->racers[0].actor.x:0,r?r->racers[0].actor.y:0,r?r->racers[1].actor.x:0,r?r->racers[1].actor.y:0,
      r?r->phase:0,r?r->racers[0].completed_laps:0,r?r->racers[1].completed_laps:0,
      r?r->racers[0].status:0,r?r->racers[1].status:0,r?r->winner:0,r?r->pause:0);
  if(n>0 && n<(int)sizeof(row))SnesTraceWriterWrite(trace,row,(size_t)n);
}
static void replay(uint32_t input,uint32_t tick) {
  (void)tick;FzeroCaptureBefore(input,FzeroCaptureViewport());
  (void)RtlRunFrame(input);FzeroDrawPpuFrame();FzeroCaptureAfter();trace_frame();
}
bool FzeroNetplayStart(const RecompLauncherCSettings *settings) {
  SnesNetplayConfig cfg;
  exit_ready=false;memset(&last_stats,0,sizeof(last_stats));
  from_lobby=settings->netplay_launch.enabled!=0;
  if(from_lobby) {
    SnesHostLaunchResult launch;snes_host_app_apply_launch(&settings->netplay_launch,&launch);cfg=launch.net_cfg;
    aspect=(unsigned)settings->netplay_launch.session_variant;
  } else {
    snes_netplay_config_defaults(&cfg);snes_netplay_apply_env(&cfg);cfg.enabled=1;
    aspect=1;const char *a=getenv("FZERO_NET_ASPECT");if(a)aspect=(unsigned)strtoul(a,NULL,10);
  }
  if(aspect>2 || cfg.local_slot<0 || cfg.local_slot>1 || cfg.spectator || !cfg.rollback)return false;
  FzeroOnlineBegin(aspect,(unsigned)cfg.local_slot);
  uint8_t content[32]={0};if(FzeroTracksStateActive())memcpy(content,FzeroTracksActiveHash(),32);
  char hex[65];for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",content[i]);
  snprintf(base_identity,sizeof(base_identity),"fzero-vs7;build=%s;catalog=%s;portable-scalar-v1;aspect=%u",
      FZERO_REPLAY_BUILD_ID,hex,aspect);
  const char *fixture=getenv("FZERO_TEST_ONLINE_FIXTURE");
  if(fixture && *fixture) {
    if(strcmp(fixture,"teleport") && strcmp(fixture,"crash"))return false;
    size_t end=strlen(base_identity);
    snprintf(base_identity+end,sizeof(base_identity)-end,";qa=%s",fixture);
  }
  snprintf(identity,sizeof(identity),"%s;startup=pending",base_identity);
  uint32_t cpu=(SDL_HasSSE2()?1u:0)|(SDL_HasSSE41()?2u:0)|(SDL_HasAVX2()?4u:0)|(SDL_HasNEON()?8u:0);
  unsigned architecture=0;
#if defined(__x86_64__) || defined(_M_X64)
  architecture=1;
#elif defined(__aarch64__) || defined(_M_ARM64)
  architecture=2;
#elif defined(__i386__) || defined(_M_IX86)
  architecture=3;
#endif
  /* No direct assembly/SIMD race backend is enabled. Renderer capabilities
   * are presentation-only; CPU features above are actual runtime queries. */
  snes_netplay_set_profile_caps(0xf2060001u,cpu,1,1,architecture,profile_settled);
  snes_state_digest_set_extra(game_digest);snes_netplay_rb_set_replay_frame(replay);
  if(snes_netplay_start(&cfg)!=0) {FzeroOnlineEnd();snes_state_digest_set_extra(NULL);return false;}
  const char *stem=getenv("FZERO_TEST_NET_TRACE");
  if(stem && *stem)trace=SnesTraceWriterOpen(stem,"tick,hash,phase,course,p1,p2,race,x1,y1,x2,y2,stage,lap1,lap2,status1,status2,winner,pause\n",16*1024*1024,2);
  /* RtlEnsureSaveDir creates one level; install the isolated namespace's
   * parents on a fresh extraction before any shared save operation. */
  RtlSetSaveRoot("saves");RtlEnsureSaveDir();
  RtlSetSaveRoot("saves/netplay");RtlEnsureSaveDir();
  RtlSetSaveRoot("saves/netplay/fzero");RtlSetQuickSaveRoot("saves/netplay/fzero");RtlEnsureSaveDir();
  return true;
}
FzeroViewport FzeroNetplayViewport(const FzeroVideoSettings *s,int width,int height) {
  if(!FzeroOnlineEnabled())return FzeroCalculateViewport(s,width,height);
  FzeroVideoSettings fixed=*s;fixed.enhanced=true;fixed.aspect=(FzeroAspect)aspect;
  return FzeroCalculateViewport(&fixed,width,height);
}
typedef struct PadSample {uint16_t buttons;} PadSample;
static uint16_t sample(void *ctx) {return ((PadSample *)ctx)->buttons;}
bool FzeroNetplayAdmit(uint16_t pad,int *running) {
  if(exit_ready || (FzeroOnlineCompleted()->phase==FZERO_ONLINE_EXIT && snes_netplay_peer_disconnected(10000))) {
    *running=0;return false;
  }
  PadSample data={pad};SnesHostBarrierHooks hooks={0};hooks.capture_local_pad=sample;hooks.ctx=&data;
  hooks.connect_timeout_ms=30000;
  hooks.peer_timeout_ms=10000; /* Bounded native course bootstrap on slower hosts. */
  return snes_host_barrier_admit(from_lobby,running,&hooks)!=0;
}
void FzeroNetplayFinish(void) {
  trace_frame();
  snes_netplay_finish_frame();
  remember_stats();
  const FzeroOnlineState *s=FzeroOnlineCompleted();
  /* Relative age also works after synchronized loads start a new wire epoch. */
  if(s->phase==FZERO_ONLINE_EXIT && s->tick-s->exit_tick>snes_netplay_rb_confirmed_remaining()) {
    exit_ready=true;
  }
}
void FzeroNetplayStop(void) {
  if(!FzeroOnlineEnabled())return;
  bool keep=from_lobby;
  if(snes_netplay_active())remember_stats();
  fprintf(stderr,"[fzero-netplay] final tick=%u confirmed=%u episodes=%u resim=%llu desync=%u\n",
      last_stats.tick,last_stats.confirmed,last_stats.episodes,
      (unsigned long long)last_stats.resim,last_stats.desync);
  const char *writer_error=SnesTraceWriterClose(trace);trace=NULL;
  if(writer_error)fprintf(stderr,"[fzero-netplay] trace incomplete: %s\n",writer_error);
  snes_netplay_shutdown();snes_state_digest_set_extra(NULL);
  snes_netplay_set_profile_caps(0,0,0,0,0,NULL);FzeroOnlineEnd();
  if(keep) {recomp_netplay_host_prepare_rematch();resume=true;}
}
