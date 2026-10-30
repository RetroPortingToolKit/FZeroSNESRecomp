#include "fzero_msu.h"
#include "fzero_gameplay.h"
#include "fzero_music_sources.h"
#include "fzero_tracks.h"
#include "fzero_packs.h"
#include "fzero_menu_music.h"
#include "fzero_course_runtime.h"
#include "fzero_local.h"
#include "fzero_online.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "sha256.h"
#include "snes/interp_bridge.h"
#include "snes/msu1.h"
#include "snes/snes.h"
#include "snes/apu.h"
#include "snes/dsp.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static bool active;
static bool versus_music, versus_pcm, versus_loop;
static bool versus_restored;
static bool versus_seek_pending;
static uint64_t versus_seek;
static unsigned versus_command=~0u, versus_course=~0u;
static uint64_t versus_master;
static uint64_t versus_generation;
extern Snes *g_snes;
static char legacy_pack[1024], source_rom[1024];
static char error[256];
static FzeroMusicSources music_sources;
static char selected_source[96], resolved_directory[1024];
static bool selected_supported = true, selected_race, directory_loaded, directory_valid;
typedef struct MsuRoute {
  char source[96],pack[96];unsigned index;
  bool supported,race,course_music;
} MsuRoute;
static MsuRoute loaded_route;
static const MsuRoute *restoring_route;
static bool versus_voices(uint8_t voices);

void FzeroMsuSelectTrackSource(const char *source, bool race) {
  selected_supported = source != NULL;
  selected_race = race;
  snprintf(selected_source, sizeof(selected_source), "%s", source ? source : "");
}
static bool resolve_track(void *context, const char *base, uint16_t track, char *path, size_t cap) {
  (void)context;
  /* Course filenames identify installed race music without soundtrack IDs,
   * prefixes or a numeric song map. Non-course events use the pack defaults
   * and individual selections exposed in Mods. */
  if(restoring_route) {
    loaded_route=*restoring_route;
    if(loaded_route.course_music)
      return FzeroPacksCourseMusic(loaded_route.pack,loaded_route.index,path,cap);
  } else {
    memset(&loaded_route,0,sizeof(loaded_route));
    snprintf(loaded_route.source,sizeof(loaded_route.source),"%s",selected_source);
    loaded_route.supported=selected_supported;loaded_route.race=selected_race;
    if(selected_race && FzeroTracksCurrentMusic(path,cap)) {
      loaded_route.course_music=FzeroTracksCurrentMusicKey(loaded_route.pack,sizeof(loaded_route.pack),&loaded_route.index);
      return true;
    }
  }
  if (!selected_race) return FzeroMenuMusicResolve(track, path, cap);
  const char *leaf = base;
  for (const char *p = base; *p; ++p) if (*p == '/' || *p == '\\') leaf = p + 1;
  size_t length = (size_t)(leaf - base);
  if (length >= sizeof(resolved_directory)) return false;
  if (!directory_loaded || strlen(resolved_directory) != length || strncmp(resolved_directory, base, length)) {
    memcpy(resolved_directory, base, length); resolved_directory[length] = 0;
    directory_loaded = true;
    memset(&music_sources, 0, sizeof(music_sources));
    directory_valid = FzeroTracksReadMusicSources(&music_sources);
    if (!directory_valid)
      fprintf(stderr, "[fzero-msu1] Invalid soundtrack metadata; using SNES audio.\n");
  }
  if (!directory_valid || !selected_supported) return false;
  if (FzeroMusicResolve(&music_sources, base, selected_source, selected_race, track, path, cap)) {
    FILE *file = fopen(path, "rb");
    if (file) { fclose(file); return true; }
  }
  return FzeroPacksResolveMusic(selected_source, track, path, cap);
}
/* Conn/Cubear v11, from the patch-only archive linked by its authors at
 * https://www.zeldix.net/t2768-bs-f-zero-deluxe-msu-1 . No patch is bundled. */
static const uint8_t patch_hash[32] = {
  0x90,0x19,0x01,0x3f,0x08,0x5f,0xf1,0x6f,0x55,0x01,0xc4,0x51,0x65,0x31,0xa0,0x44,
  0xbc,0x5f,0x36,0x70,0x3a,0xad,0xb5,0x88,0x44,0xe6,0x7c,0x54,0x56,0x41,0x35,0x32};
/* A non-NULL empty program prevents calls into stock/Deluxe AOT routines
 * whose baked-in instructions would bypass the imported patch. */
static const DispatchEntry patched_program[1] = {{0}};

bool FzeroMsuActive(void) { return active; }
const char *FzeroMsuError(void) { return error; }

bool FzeroMsuHasInstalledMusic(void) { return FzeroPacksHasMusic(); }

static void set_pack(const char *value) {
#ifdef _WIN32
  _putenv_s("SNESRECOMP_MSU1", value);
#else
  setenv("SNESRECOMP_MSU1", value, 1);
#endif
}

static void trim_msu_suffix(char *base) {
  size_t n = strlen(base);
  if (n >= 2 && base[0] == '"' && base[n-1] == '"') {
    memmove(base, base+1, n-2); base[n-2]=0; n-=2;
  }
  if (n >= 4 && base[n-4] == '.' &&
      tolower((unsigned char)base[n-3]) == 'm' &&
      tolower((unsigned char)base[n-2]) == 's' &&
      tolower((unsigned char)base[n-1]) == 'u') base[n-4]=0;
}
bool FzeroMsuHasLegacyPatch(const char *pack) {
  if (!pack || !*pack) return false;
  const char *override = getenv("FZERO_MSU1_PATCH");
  if (override && *override) return true;
  char directory[1024], path[1200];
  if (strlen(pack) >= sizeof(directory)) return false;
  snprintf(directory,sizeof(directory),"%s",pack);
  trim_msu_suffix(directory);
  /* Preserve the legacy command-line shortcut for music beside the ROM. */
  if (!strcmp(directory,"auto") || !strcmp(directory,"on") || !strcmp(directory,"1"))
    return true;
  struct stat st;
  if (stat(directory,&st) != 0 || !(st.st_mode & S_IFDIR)) {
    char *slash=strrchr(directory,'/'), *back=strrchr(directory,'\\');
    if (!slash || (back && back>slash)) slash=back;
    if (slash) *slash=0; else snprintf(directory,sizeof(directory),".");
  }
  snprintf(path,sizeof(path),"%s/f-zero_msu1.ips",directory);
  return stat(path,&st)==0;
}
bool FzeroMsuConfigure(const char *pack, bool cgp, const char *rom_path) {
  /* A cold return to single-player must restore all native voices too. */
  (void)versus_voices(0xff);
  versus_music=versus_pcm=versus_restored=versus_seek_pending=false;versus_command=versus_course=~0u;versus_master=0;
  memset(&loaded_route,0,sizeof(loaded_route));restoring_route=NULL;
  active=false; legacy_pack[0]=0; error[0]=0;
  FzeroMsuSelectTrackSource("", false);
  directory_loaded = false;
  msu1_set_track_resolver(cgp ? resolve_track : NULL, NULL);
  char requested[1024];
  if (pack && strlen(pack)>=sizeof(requested)) { snprintf(error,sizeof(error),"Music path is too long");set_pack("");return false; }
  snprintf(requested,sizeof(requested),"%s",pack?pack:"");
  trim_msu_suffix(requested);
  if (cgp || !*requested || !strcmp(requested,"off") || !strcmp(requested,"0")) {
    set_pack(requested); return true;
  }
  /* Validate the user patch before any game image is prepared. */
  uint8_t *probe=calloc(1,0x80000);size_t length=0x80000;
  if (!probe) { snprintf(error,sizeof(error),"Cannot validate music adapter");return false; }
  bool ok=FzeroMsuPrepare(&probe,&length,requested,rom_path);free(probe);
  if (!ok) return false;
  snprintf(legacy_pack,sizeof(legacy_pack),"%s",requested);
  snprintf(source_rom,sizeof(source_rom),"%s",rom_path?rom_path:"");
  return true;
}
bool FzeroMsuConfigureVersus(const char *pack, bool cgp, const char *rom_path) {
  /* Arm the existing PCM core/resolver without importing any guest patch.
   * A peer's music choice or missing files must not alter simulation state. */
  if(!FzeroMsuConfigure(pack,true,rom_path))return false;
  if(!cgp)msu1_set_track_resolver(NULL,NULL);
  const char *configured=getenv("SNESRECOMP_MSU1");
  char base[1024];snprintf(base,sizeof(base),"%s",configured?configured:"");
  if(!strcmp(base,"off") || !strcmp(base,"0"))base[0]=0;
  if(!strcmp(base,"auto") || !strcmp(base,"on") || !strcmp(base,"1")) {
    if(snprintf(base,sizeof(base),"%s",rom_path?rom_path:"fzero.sfc")>=(int)sizeof(base)) {
      snprintf(error,sizeof(error),"Music path is too long");set_pack("");return false;
    }
    char *dot=strrchr(base,'.'),*slash=strrchr(base,'/'),*back=strrchr(base,'\\');
    if(!slash || (back && back>slash))slash=back;
    if(dot && (!slash || dot>slash))*dot=0;
  }
  set_pack(base);versus_music=*base!=0;
  return true;
}
static bool versus_voices(uint8_t voices) {
  if(!g_snes || !g_snes->apu)return false;
  RtlApuLock();
  bool ok=dsp_set_output_voices(g_snes->apu->dsp,voices);
  RtlApuUnlock();return ok;
}
void FzeroMsuUpdateVersus(void) {
  if(!versus_music || !FzeroLocalEnabled() || !g_snes)return;
  /* The load callback precedes the engine's generation increment. Acknowledge
   * that one timeline change without reselecting the just-restored track. */
  if(versus_restored) {versus_generation=RtlStateGeneration();versus_restored=false;}
  const FzeroVersusSnapshot *race=FzeroVersusCompleted(FzeroLocalSession());
  unsigned command=FzeroLocalMusicCommand();
  const FzeroOnlineState *online=FzeroOnlineCompleted();
  if(FzeroOnlineEnabled() && online->phase>=FZERO_ONLINE_MACHINES && online->phase<=FZERO_ONLINE_COURSE)
    command=5;
  unsigned course=FzeroLocalConfig().course;
  unsigned song=command&7;
  bool changed=versus_command==~0u || song!=(versus_command&7) ||
      (song==6 && course!=versus_course) ||
      (!FzeroOnlineEnabled() && g_cpu.master_cycles<versus_master) ||
      versus_generation!=RtlStateGeneration();
  versus_master=g_cpu.master_cycles;
  versus_generation=RtlStateGeneration();
  if(changed) {
    versus_command=song;versus_course=course;versus_pcm=false;
    msu1_write(0x2007,0);(void)versus_voices(0xff);
    if(song && msu1_enabled()) {
      unsigned track=FzeroGameplayMusicTrack(song);
      msu1_write(0x2004,(uint8_t)track);msu1_write(0x2005,(uint8_t)(track>>8));
      if(!(msu1_read(0x2000)&8) && versus_voices(0xc0)) {
        versus_pcm=true;versus_loop=song>3;
        msu1_write(0x2006,255);msu1_write(0x2007,versus_loop?3:1);
      }
      fprintf(stderr,"[versus-msu] command=%u course=%u track=%u output=%s\n",song,course,track,versus_pcm?"PCM":"SPC");
    }
    if(versus_seek_pending) {
      if(versus_pcm && !msu1_seek_playback(versus_seek)) {
        versus_pcm=false;(void)versus_voices(0xff);
      }
      versus_seek_pending=false;
    }
  }
  if(!versus_pcm)return;
  /* A broken/empty looping file must restore native audio, not a silent race.
   * One-shot event songs are allowed to end normally. */
  if(versus_loop && !(msu1_read(0x2000)&0x10)) {
    versus_pcm=false;msu1_write(0x2007,0);(void)versus_voices(0xff);
    fprintf(stderr,"[versus-msu] Playback stopped unexpectedly; using SPC\n");return;
  }
  unsigned volume=255;
  if(race && (command&0x80)) {
    /* Follow the native SPC's result fade without changing its master or
     * stopping native engine, collision, drift and explosion channels. */
    RtlApuLock();int master=g_snes->apu->dsp->masterVolumeL;RtlApuUnlock();
    volume=master<=0?0:master>=0x54?255:(unsigned)master*255/0x54;
  }
  msu1_write(0x2006,(uint8_t)volume);
}
bool FzeroMsuApplyConfigured(uint8_t **rom, size_t *size) {
  return !*legacy_pack || FzeroMsuPrepare(rom,size,legacy_pack,source_rom);
}

bool FzeroMsuPrepare(uint8_t **rom, size_t *size, const char *pack, const char *rom_path) {
  active = false;
  error[0] = 0;
  if (!pack || !*pack || !strcmp(pack, "0") || !strcmp(pack, "off")) {
    set_pack("");
    return true;
  }
  char base[1024], path[1200];
  if (strlen(pack) >= sizeof(base)) goto invalid;
  snprintf(base, sizeof(base), "%s", pack);
  /* Windows batch files often include quotes in the environment VALUE. */
  size_t len = strlen(base);
  if (len >= 2 && base[0] == '"' && base[len-1] == '"') {
    memmove(base, base + 1, len - 2); base[len - 2] = 0;
  }
  if (!strcmp(base, "1") || !strcmp(base, "on") || !strcmp(base, "auto")) {
    if (snprintf(base, sizeof(base), "%s", rom_path ? rom_path : "fzero.sfc") >= (int)sizeof(base))
      goto invalid;
    char *dot = strrchr(base, '.');
    char *slash = strrchr(base, '/'), *back = strrchr(base, '\\');
    if (!slash || (back && back > slash)) slash = back;
    if (dot && (!slash || dot > slash)) *dot = 0;
  }
  trim_msu_suffix(base);
  const char *override = getenv("FZERO_MSU1_PATCH");
  if (override && *override) {
    if (snprintf(path, sizeof(path), "%s", override) >= (int)sizeof(path)) goto invalid;
  } else {
    struct stat st;
    char directory[1024];
    snprintf(directory, sizeof(directory), "%s", base);
    if (stat(directory, &st) != 0 || !(st.st_mode & S_IFDIR)) {
      char *slash = strrchr(directory, '/'), *back = strrchr(directory, '\\');
      if (!slash || (back && back > slash)) slash = back;
      if (slash) *slash = 0; else snprintf(directory, sizeof(directory), ".");
    }
    if (snprintf(path, sizeof(path), "%s/f-zero_msu1.ips", directory) >= (int)sizeof(path)) goto invalid;
  }
  FILE *file = fopen(path, "rb");
  if (!file) {
    snprintf(error, sizeof(error), "Put Conn/Cubear v11 f-zero_msu1.ips in the selected music folder (or set FZERO_MSU1_PATCH).");
    set_pack("");
    return false;
  }
  uint8_t patch[710], digest[32];
  size_t count = fread(patch, 1, sizeof(patch), file);
  bool read_ok = !ferror(file) && count == 709 && fgetc(file) == EOF;
  fclose(file);
  if (!read_ok) goto invalid;
  sha256_compute(patch, count, digest);
  if (memcmp(digest, patch_hash, 32)) goto invalid;
  if (!rom || !*rom || !size || (*size != 0x80000 && *size != 0x100000 && *size != 0x400000)) goto invalid;
  size_t mapped_size = *size > 0x100000 ? *size : 0x100000;
  uint8_t *mapped = malloc(mapped_size);
  if (!mapped) goto invalid;
  memset(mapped, 0xff, mapped_size);
  memcpy(mapped, *rom, *size);
  /* Digest-pinned IPS still gets checked bounds; never publish a partial image. */
  size_t pos = 5;
  if (memcmp(patch, "PATCH", 5)) { free(mapped); goto invalid; }
  while (pos + 3 <= count && memcmp(patch + pos, "EOF", 3)) {
    if (pos + 5 > count) { free(mapped); goto invalid; }
    size_t offset = (size_t)patch[pos] << 16 | (size_t)patch[pos+1] << 8 | patch[pos+2];
    size_t length = (size_t)patch[pos+3] << 8 | patch[pos+4];
    pos += 5;
    if (!length || pos + length > count || offset + length > 0x100000) {
      free(mapped); goto invalid;
    }
    memcpy(mapped + offset, patch + pos, length);
    pos += length;
  }
  if (pos + 3 != count || memcmp(patch + pos, "EOF", 3)) { free(mapped); goto invalid; }
  free(*rom); *rom = mapped; *size = mapped_size;
  cpu_select_program(patched_program, 0, NULL, 0);
  interp_bridge_set_scheduler_aot_policy(0);
  set_pack(base);
  active = true;
  fprintf(stderr, "[fzero-msu1] Conn/Cubear v11 active; patched cartridge runs through the interpreter\n");
  return true;
invalid:
  snprintf(error, sizeof(error), "Unsupported MSU-1 patch or path. Expected Conn/Cubear v11 (709-byte IPS); no ROM changes applied.");
  set_pack("");
  return false;
}

bool FzeroMsuSelectSaveRoot(void) {
  if (!active) return true;
  char root[96];
  if (snprintf(root, sizeof(root), "%s/msu1", RtlSaveRoot()) >= (int)sizeof(root)) {
    snprintf(error, sizeof(error), "MSU-1 save directory is too long; choose a shorter save root.");
    return false;
  }
  RtlEnsureSaveDir(); RtlSetSaveRoot(root); RtlEnsureSaveDir();
  return true;
}

static bool neutral_audio(const uint8_t *state) {
  static const uint8_t empty[FZERO_MSU_AUDIO_STATE_SIZE];
  return !memcmp(state,empty,sizeof(empty));
}
void FzeroMsuSaveAudio(uint8_t out[FZERO_MSU_AUDIO_STATE_SIZE]) {
  memset(out,0,FZERO_MSU_AUDIO_STATE_SIZE);
  if(RtlIsRollbackSave())return;
  msu1_capture_playback(out);
  snprintf((char *)out+32,96,"%s",selected_source);
  snprintf((char *)out+128,96,"%s",loaded_route.source);
  snprintf((char *)out+224,96,"%s",loaded_route.pack);
  out[320]=(uint8_t)loaded_route.index;out[321]=(uint8_t)(loaded_route.index>>8);
  out[322]=(uint8_t)(selected_supported | selected_race<<1 | loaded_route.supported<<2 |
      loaded_route.race<<3 | loaded_route.course_music<<4);
}
bool FzeroMsuAudioStateValid(const uint8_t state[FZERO_MSU_AUDIO_STATE_SIZE]) {
  if(neutral_audio(state))return true;
  if(!msu1_playback_state_valid(state) || (state[322]&~31) ||
      !memchr(state+32,0,96) || !memchr(state+128,0,96) || !memchr(state+224,0,96))return false;
  for(unsigned i=323;i<FZERO_MSU_AUDIO_STATE_SIZE;++i)if(state[i])return false;
  return !(state[322]&16) || ((state[322]&8) && state[224]);
}
void FzeroMsuRestoreAudio(uint8_t *ram,const uint8_t state[FZERO_MSU_AUDIO_STATE_SIZE]) {
  if(RtlIsRollbackLoad())return; /* speculative replay never rewinds live audio */
  if(neutral_audio(state))return;
  MsuRoute route={0};
  snprintf(route.source,sizeof(route.source),"%s",(const char *)state+128);
  snprintf(route.pack,sizeof(route.pack),"%s",(const char *)state+224);
  route.index=state[320]|(unsigned)state[321]<<8;
  route.supported=(state[322]&4)!=0;route.race=(state[322]&8)!=0;route.course_music=(state[322]&16)!=0;
  /* Reopen the actual saved course/namespace, including snapshots between
   * music selection and the guest's ready check. Restore the pending selector
   * afterwards. Only catalog identities travel in states, never audio paths. */
  FzeroMsuSelectTrackSource(route.supported?route.source:NULL,route.race);
  restoring_route=&route;
  bool restored=msu1_restore_playback(state);
  restoring_route=NULL;loaded_route=route;
  FzeroMsuSelectTrackSource((state[322]&1)?(const char *)state+32:NULL,(state[322]&2)!=0);
  bool pcm=restored && !(msu1_read(0x2000)&8) && (state[5]&1);
  if(versus_music && FzeroLocalEnabled()) {
    unsigned command=FzeroLocalMusicCommand();
    const FzeroOnlineState *online=FzeroOnlineCompleted();
    if(FzeroOnlineEnabled() && online->phase>=FZERO_ONLINE_MACHINES && online->phase<=FZERO_ONLINE_COURSE)command=5;
    versus_command=command&7;versus_course=FzeroLocalConfig().course;
    versus_pcm=versus_command && pcm;versus_loop=(state[5]&4)!=0;
    versus_master=g_cpu.master_cycles;versus_restored=true;
    versus_seek_pending=false;
    if(versus_command && !(state[5]&1)) {
      /* A native-music host has no PCM cursor to share. Retain this peer's
       * MSU preference and align its own recording to the saved race clock. */
      const FzeroVersusSnapshot *race=FzeroVersusCompleted(FzeroLocalSession());
      uint64_t age=0;
      if(race)age=race->phase==FZERO_VS_COUNTDOWN?
          FZERO_VERSUS_COUNTDOWN-race->countdown:race->race_tick;
      double frames=(double)age*44100/FZERO_SIMULATION_HZ;
      versus_seek=frames>=(double)UINT64_MAX?UINT64_MAX:(uint64_t)frames;
      versus_command=~0u;versus_seek_pending=true;
    }
    (void)versus_voices(versus_pcm?0xc0:0xff);
  } else {
    (void)versus_voices(0xff);
    if((state[5]&1) && !pcm && (FzeroRuleEnabled(FZERO_RULE_MSU) || active)) {
      /* A recording may have been removed after saving. Let the existing
       * adapter take its native SPC fallback on the next guest music hook. */
      ram[0x182]=1;ram[0x181]=0;
      ram[0x46]=(ram[0x46]&0xf8)|(ram[0x180]&7);
    }
  }
}
