#ifdef _WIN32
#include <windows.h>
#undef FORCEINLINE
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif
#include "fzero_course_runtime.h"
#include "fzero_records.h"
#include "common_rtl.h"
#include "snes/saveload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                   \
      exit(1);                                                                                     \
    }                                                                                              \
  } while (0)
uint8 g_ram[0x20000];
static uint8 sram[0x8000];
uint8 *g_sram = sram;
int g_sram_size = 0x800;
static char root[96] = "test-course-records";
static char quick_root[96];
const char *RtlSaveRoot(void) {
  return root;
}
void RtlSetSaveRoot(const char *s) {
  snprintf(root, sizeof(root), "%s", s);
}
void RtlSetQuickSaveRoot(const char *s) {
  snprintf(quick_root, sizeof(quick_root), "%s", s);
}
void RtlEnsureSaveDir(void) {
  MKDIR(root);
}
bool FzeroDeluxeActive(void) {
  return true;
}
void FzeroTracksReport(const char *s) {
  fprintf(stderr, "%s\n", s);
}
int main(void) {
  uint8_t unified[0x400], previous[0x400], merged[0x400];
  for (unsigned i = 0; i < sizeof(unified); ++i) unified[i] = previous[i] = 0;
  for (unsigned i = 0; i < 55; ++i) {
    memcpy(unified + 5 + i * 3, "\x09\x59\x99", 3);
    memcpy(previous + 5 + i * 3, "\x09\x59\x99", 3);
  }
  memcpy(unified + 5, "\x81\x30\x32", 3);
  memcpy(previous + 5, "\x81\x20\x15", 3);
  memcpy(previous + 8, "\x81\x30\x32", 3); /* Existing time is not duplicated. */
  memcpy(unified + 35, "\x80\x20\x52", 3);
  memcpy(previous + 35, "\x80\x19\x32", 3);
  FzeroRecordsMergeCup(unified, previous, 0);
  CHECK(!memcmp(unified + 5, "\x81\x20\x15\x81\x30\x32\x09\x59\x99", 9));
  CHECK(!memcmp(unified + 35, "\x80\x19\x32", 3));
  CHECK(!memcmp(unified + 0xac, previous + 0xac, 0x400 - 0xac));
  unsigned checksum = 0;
  for (unsigned i = 5; i < 0xaa; ++i) checksum += unified[i];
  CHECK(unified[0xaa] == (uint8_t)checksum && unified[0xab] == checksum >> 8);
  memcpy(merged, unified, sizeof(merged));
  FzeroRecordsMergeCup(unified, previous, 0);
  CHECK(!memcmp(merged, unified, sizeof(merged)));
  uint8_t a[32] = {1}, b[32] = {2}, original[0x800], mirror[0x200];
  MKDIR(root);
  FzeroTracksSavesInit();
  CHECK(!strcmp(quick_root, "test-course-records"));
  memset(sram, 0x42, sizeof(sram));
  memset(g_ram + 0x14800, 0x73, 0x200);
  memcpy(original, sram, sizeof(original));
  memcpy(mirror, g_ram + 0x14800, sizeof(mirror));
  remove("test-course-records/courses/01000000000000000000000000000000/records.bin");
  remove("test-course-records/courses/02000000000000000000000000000000/records.bin");
  CHECK(FzeroTracksRecordsSelect(a));
  CHECK(!memcmp(sram + 5, "\x09\x59\x99", 3) && sram[0xaa] == 0xed && sram[0xab] == 0x35);
  CHECK(sram[0x1fa] == 0x42);
  sram[5] = 0x81;
  g_ram[0x14805] = 0x81;
  CHECK(FzeroTracksRecordsSelect(b) && sram[5] == 9);
  sram[5] = 0x82;
  g_ram[0x14805] = 0x82;
  CHECK(FzeroTracksRecordsSelect(a) && sram[5] == 0x81 && g_ram[0x14805] == 0x81);
  CHECK(!strcmp(quick_root, "test-course-records"));
  FzeroTracksSavesFinish();
  CHECK(!memcmp(original, sram, sizeof(original)) &&
        !memcmp(mirror, g_ram + 0x14800, sizeof(mirror)));
  CHECK(!strcmp(root, "test-course-records"));
  CHECK(FzeroTracksRecordsSelect(b) && sram[5] == 0x82);
  uint8_t readback[0x400], missing[32] = {3};
  CHECK(FzeroRecordsViewBegin() && FzeroRecordsViewState());
  CHECK(!strcmp(root, "test-course-records"));
  CHECK(FzeroRecordsRead(a, readback) && readback[5] == 0x81);
  CHECK(FzeroRecordsRead(b, readback) && readback[5] == 0x82);
  CHECK(FzeroRecordsRead(NULL, readback) && !memcmp(readback, original, sizeof(readback)));
  CHECK(!FzeroRecordsRead(missing, readback) && readback[5] == 9);
  memset(sram, 0xee, sizeof(sram)); /* Synthetic three-cup view is disposable. */
  memset(g_ram + 0x14800, 0xdd, 0x200);
  FzeroTracksFlush();
  CHECK(FzeroTracksRecordsSelect(a) && !FzeroRecordsViewState() && sram[5] == 0x81);
  CHECK(FzeroRecordsViewBegin());
  FzeroTracksSavesFinish();
  CHECK(!memcmp(original, sram, sizeof(original)) && !memcmp(mirror, g_ram + 0x14800, sizeof(mirror)));
  const char *path = "test-course-records/courses/01000000000000000000000000000000/records.bin";
  FILE *f = fopen(path, "wb");
  CHECK(f);
  fputs("damaged", f);
  fclose(f);
  CHECK(FzeroTracksRecordsSelect(a) && sram[5] == 9);
  CHECK(FzeroRecordsViewBegin() && !FzeroRecordsRead(a, readback) && readback[5] == 9);
  FzeroTracksSavesFinish();
  f = fopen(path, "rb");
  CHECK(f);
  char saved[8] = {0};
  CHECK(fread(saved, 1, 8, f) == 7);
  fclose(f);
  CHECK(!strcmp(saved, "damaged") && !memcmp(original, sram, sizeof(original)));
  puts("Independent cup records, clean defaults, base restoration and corrupt-file preservation "
       "passed");
  return 0;
}
