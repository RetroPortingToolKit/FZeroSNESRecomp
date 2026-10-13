#include "fzero_capture.h"
#include "fzero_build.h"
#include "fzero_runtime.h"
#include "fzero_tracks.h"
#include "fzero_course_runtime.h"
#include "common_rtl.h"
#include "crc32.h"
#include "cpu_state.h"
#include "snes/saveload.h"
#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

/* Last three 30-second segments, plus a checkpoint whenever the timeline
 * changes. Input is flushed BEFORE execution so a mid-frame crash retains
 * its triggering input. Disk work and snapshots stay outside fault handlers. */
static FILE *inputs, *segments, *replay;
static char root[1400];
/* A fault may originate on an audio/render worker while the main thread
 * updates its summary. Publish complete versions without a fault-path lock.
 * Atomic bytes keep even an overlapping two-buffer wrap free of data races. */
static _Atomic unsigned char reports[2][4096];
static atomic_uint_fast64_t report_sequence;
static uint64_t frame, completed, generation, segment_start;
static unsigned segment;
static bool recording, pending;
static uint32_t last_input;
static uint64_t replay_sequence;
static bool replay_has_sequence;
extern bool FzeroCaptureRecords(const char *directory, bool erase);

static void quoted(FILE *f, const char *s) {
  fputc('"', f);
  for (; *s; ++s) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
    else if (c < 32) fprintf(f, "\\u%04x", c);
    else fputc(c, f);
  }
  fputc('"', f);
}
static bool directory(const char *path) {
#ifdef _WIN32
  return !_mkdir(path) || errno == EEXIST;
#else
  return !mkdir(path, 0755) || errno == EEXIST;
#endif
}
static void name(char *out, size_t cap, unsigned id, const char *ext) {
  snprintf(out, cap, "%s/segment-%06u.%s", root, id, ext);
}
static void update_report(const char *status) {
  /* root is escaped once below at Start; per-frame fields are numeric only. */
  char path[2800]; unsigned n = 0;
  for (const char *s=root; *s && n+7<sizeof(path); ++s) {
    if (*s=='"' || *s=='\\') path[n++]='\\';
    if ((unsigned char)*s<32) n+=(unsigned)snprintf(path+n,sizeof(path)-n,"\\u%04x",(unsigned char)*s);
    else path[n++]=*s;
  }
  path[n]=0;
  char report[4096];
  snprintf(report,sizeof(report),"\"fzero_replay\":{\"enabled\":true,\"status\":\"%s\","
           "\"directory\":\"%s\",\"segment\":%u,\"next_frame\":%" PRIu64 ","
           "\"completed_frames\":%" PRIu64 ",\"pending_input\":%s,\"last_input\":%u,"
           "\"before_fault\":{\"resume_pc\":%u,\"master_cycles\":%" PRIu64 ","
           "\"scene\":[%u,%u,%u],\"vehicle_slot\":%u}},\n",
           status,path,segment,frame,completed,pending?"true":"false",last_input,
           FzeroResumePc(),g_cpu.master_cycles,g_ram[0x54],g_ram[0x55],g_ram[0x56],g_ram[0x52]);
  uint_fast64_t next=atomic_load_explicit(&report_sequence,memory_order_relaxed)+1;
  size_t length=strlen(report);
  for (size_t i=0;i<=length;++i)
    atomic_store_explicit(&reports[next&1][i],(unsigned char)report[i],memory_order_relaxed);
  atomic_store_explicit(&report_sequence,next,memory_order_release);
}
static void disable(void) {
  if (inputs) fclose(inputs);
  if (segments) fclose(segments);
  inputs=segments=NULL; recording=false;
  update_report("write_failed");
  fprintf(stderr,"[fzero-replay] Capture failed; gameplay continues.\n");
}

typedef struct Stream { SaveLoadInfo base; FILE *file; bool loading, ok; } Stream;
static void stream(SaveLoadInfo *base, void *data, size_t size) {
  Stream *s=(Stream *)base;
  if (s->loading) {
    if (fread(data,1,size,s->file)!=size) { memset(data,0,size); s->ok=false; }
  } else if (fwrite(data,1,size,s->file)!=size) s->ok=false;
}
static bool execution(const char *path, bool loading) {
  FILE *f=fopen(path,loading?"rb":"wb"); if (!f) return false;
  Stream s={{stream,NULL},f,loading,true};
  /* The framework's pointer-free execution extension includes the master,
   * refresh, APU-port and HDMA clocks ordinary one-off states re-anchor. */
  RtlApuLock();
  if (loading) s.ok=RtlLoadExecutionState(&s.base) && s.ok;
  else RtlSaveExecutionState(&s.base);
  RtlApuUnlock();
  if (loading && fgetc(f)!=EOF) s.ok=false;
  if (fclose(f)) s.ok=false;
  if (loading && s.ok) RtlApplyExecutionState();
  return s.ok;
}
static bool checkpoint(void) {
  if (inputs) { fclose(inputs); inputs=NULL; ++segment; }
  char path[1600];
  name(path,sizeof(path),segment,"sav");
  if (!RtlSaveSnapshot(path)) return false;
  name(path,sizeof(path),segment,"execution");
  if (!execution(path,false)) return false;
  name(path,sizeof(path),segment,"records");
  if (!FzeroCaptureRecords(path,false)) return false;
  name(path,sizeof(path),segment,"inputs");
  inputs=fopen(path,"wb"); if (!inputs) return false;
  segment_start=frame; generation=RtlStateGeneration();
  if (fprintf(segments,"{\"segment\":%u,\"start_frame\":%" PRIu64 "}\n",segment,frame)<0 || fflush(segments)) return false;
  /* Delete only files generated by this recorder in this new session. */
  if (segment>=3) {
    const char *exts[]={"sav","execution","inputs"};
    for (unsigned i=0;i<3;++i) { name(path,sizeof(path),segment-3,exts[i]); remove(path); }
    name(path,sizeof(path),segment-3,"records"); FzeroCaptureRecords(path,true);
  }
  return true;
}
bool FzeroCaptureStart(bool enabled, const char *directory_path, const FzeroVideoSettings *s) {
  FzeroCaptureStop();
  if (!enabled) return true;
  if (!directory_path || strlen(directory_path)>1100 || !s || !directory(directory_path)) return false;
  time_t now=time(NULL); struct tm t;
#ifdef _WIN32
  if (gmtime_s(&t,&now)) return false;
  int pid=_getpid();
#else
  if (!gmtime_r(&now,&t)) return false;
  int pid=(int)getpid();
#endif
  static unsigned sequence;
  char stamp[40], path[1600]; strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%SZ",&t);
  snprintf(root,sizeof(root),"%s/replay-%s-%d-%u",directory_path,stamp,pid,sequence++);
  if (!directory(root)) return false;
  frame=completed=segment_start=0; segment=0; pending=false; last_input=0;
  generation=UINT64_MAX;
  snprintf(path,sizeof(path),"%s/video.ini",root);
  if (!FzeroVideoSave(s,path)) return false;
  snprintf(path,sizeof(path),"%s/session.json",root);
  FILE *f=fopen(path,"w"); if (!f) return false;
  fputs("{\"format\":1,\"game\":\"fzero-usa\",\"revision\":",f); quoted(f,FZERO_SOURCE_REVISION);
  fputs(",\"framework\":",f); quoted(f,FZERO_FRAMEWORK_REVISION);
  fputs(",\"ui\":",f); quoted(f,FZERO_UI_REVISION);
  fputs(",\"version\":",f); quoted(f,SNESRECOMP_BUILD_VERSION);
  fputs(",\"build\":",f); quoted(f,FZERO_REPLAY_BUILD_ID);
  fputs(",\"save_namespace\":",f); quoted(f,FzeroTracksSaveNamespace());
  fputs(",\"content_signature\":\"",f);
  const uint8_t *hash=FzeroTracksActiveHash();
  for (unsigned i=0;i<32;++i) fprintf(f,"%02x",hash[i]);
  fputs("\",\"packs\":[",f);
  const CpCatalog *catalog=FzeroTracksCatalog();
  for (unsigned i=0;i<catalog->count;++i) { if(i) fputc(',',f); quoted(f,catalog->packs[i]->id); }
  fputs("]}\n",f);
  bool ok=!ferror(f); if(fclose(f)) ok=false; if(!ok) return false;
  /* Effective loader/title/music choices include this launch's overrides. */
  snprintf(path,sizeof(path),"%s/loader.cfg",root); f=fopen(path,"wb");
  if (!f) return false;
  ok=FzeroTracksWriteSettings(f); if(fclose(f)) ok=false; if(!ok) return false;
  snprintf(path,sizeof(path),"%s/segments.jsonl",root);
  segments=fopen(path,"wb"); if (!segments) return false;
  recording=true; update_report("recording");
  fprintf(stderr,"[fzero-replay] Recording %s\n",root);
  return true;
}
void FzeroCaptureBefore(uint32_t input, FzeroViewport v) {
  if (!recording) return;
  last_input=input;
  if ((!inputs || frame-segment_start>=1800 || generation!=RtlStateGeneration()) && !checkpoint()) { disable(); return; }
  if (fprintf(inputs,"%" PRIu64 " %u %" PRIu64 " %u %u %d %d %.17g %u\n",
      frame,input,g_cpu.master_cycles,FzeroResumePc(),crc32_compute(g_ram,0x2000),
      v.width,v.extra,v.aspect,v.enhanced?1:0)<0 || fflush(inputs)) { disable(); return; }
  pending=true; update_report("recording");
}
void FzeroCaptureAfter(void) {
  if (!recording) return;
  ++frame; completed=frame; pending=false; update_report("recording");
}
void FzeroCaptureStop(void) {
  if (recording) { pending=false; update_report("finished"); }
  if(inputs) fclose(inputs);
  if(segments) fclose(segments);
  inputs=segments=NULL; recording=false;
}
void FzeroCaptureReport(FILE *f) {
  char report[4096];
  for (unsigned attempt=0;attempt<3;++attempt) {
    uint_fast64_t seq=atomic_load_explicit(&report_sequence,memory_order_acquire);
    if (!seq) { fputs("\"fzero_replay\":{\"enabled\":false},\n",f); return; }
    for (size_t i=0;i<sizeof(report);++i) {
      report[i]=(char)atomic_load_explicit(&reports[seq&1][i],memory_order_relaxed);
      if (!report[i]) break;
    }
    report[sizeof(report)-1]=0;
    if (seq==atomic_load_explicit(&report_sequence,memory_order_acquire)) { fputs(report,f); return; }
  }
  /* Never spin or emit torn JSON if the simulation still advances rapidly. */
  fputs("\"fzero_replay\":{\"enabled\":true,\"status\":\"summary_busy\"},\n",f);
}
const char *FzeroCaptureDirectory(void) { return root; }
void FzeroCaptureHost(const FzeroDiagnosticSession *s, int width, int height, int aspect) {
  if (!recording) return;
  char path[1600]; snprintf(path,sizeof(path),"%s/host.json",root);
  FILE *f=fopen(path,"w"); if (!f) return;
  fputs("{\"backend\":",f); quoted(f,s->backend?s->backend:"");
  fputs(",\"gpu\":",f); quoted(f,s->gpu?s->gpu:"");
  fputs(",\"vendor\":",f); quoted(f,s->gpu_vendor?s->gpu_vendor:"");
  fputs(",\"driver\":",f); quoted(f,s->driver?s->driver:"");
  fputs(",\"shader\":",f); quoted(f,s->shader?s->shader:"");
  fprintf(f,",\"linear_filter\":%u,\"audio_enabled\":%u,\"rewind_enabled\":%u,"
          "\"vsync\":%d,\"allocated_scale\":%u,\"output_width\":%d,\"output_height\":%d,"
          "\"aspect_index\":%d}\n",s->linear_filter,s->audio_enabled,s->rewind_enabled,
          s->vsync,s->allocated_scale,width,height,aspect);
  fclose(f);
}

bool FzeroCaptureReplayActive(void) { return replay!=NULL; }
bool FzeroCaptureReplayLoad(void) {
  const char *input=getenv("FZERO_REPLAY_INPUT_FILE");
  if (!input || !*input) return true;
  const char *expected=getenv("FZERO_REPLAY_CONTENT_HASH");
  if (expected) {
    char actual[65]; const uint8_t *hash=FzeroTracksActiveHash();
    for (unsigned i=0;i<32;++i) snprintf(actual+i*2,3,"%02x",hash[i]);
    if (strcmp(expected,actual)) { fprintf(stderr,"[fzero-replay] Course/asset configuration mismatch.\n"); return false; }
  }
  const char *state=getenv("FZERO_REPLAY_STATE"), *exec=getenv("FZERO_REPLAY_EXECUTION");
  if (!state || !exec || !FzeroStateFileAcceptable(state) || !RtlLoadSnapshot(state) ||
      !execution(exec,true) || !(replay=fopen(input,"rb"))) {
    fprintf(stderr,"[fzero-replay] Incompatible or incomplete checkpoint.\n"); return false;
  }
  replay_has_sequence=false;
  return true;
}
int FzeroCaptureReplayNext(uint32_t *input, FzeroViewport *v) {
  if (!replay) return -1;
  char line[256], tail; uint64_t seq, master; unsigned pc,crc,enhanced;
  if (!fgets(line,sizeof(line),replay)) return ferror(replay)?-1:0;
  if (sscanf(line,"%" SCNu64 " %u %" SCNu64 " %u %u %d %d %lf %u %c",
      &seq,input,&master,&pc,&crc,&v->width,&v->extra,&v->aspect,&enhanced,&tail)!=9 ||
      v->width<256 || v->width>FZERO_MAX_WIDTH || v->extra!=(v->width-256)/2 ||
      enhanced>1 || !(v->aspect>0 && v->aspect<=4) ||
      (replay_has_sequence && seq!=replay_sequence+1)) {
    fprintf(stderr,"[fzero-replay] Malformed input row.\n"); return -1;
  }
  replay_sequence=seq;
  replay_has_sequence=true;
  if (master!=g_cpu.master_cycles || pc!=FzeroResumePc() || crc!=crc32_compute(g_ram,0x2000)) {
    fprintf(stderr,"[fzero-replay] DIVERGENCE frame=%" PRIu64 " master=%" PRIu64 "/%" PRIu64
        " pc=%u/%u crc=%u/%u\n",seq,g_cpu.master_cycles,master,FzeroResumePc(),pc,crc32_compute(g_ram,0x2000));
    return -1;
  }
  v->enhanced=enhanced!=0;
  return 1;
}
