#include "fzero_course_file.h"
#include "fzero_packs.h"
#include "fzero_tracks.h"
#include "fzero_menu_music.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define CHDIR(p) _chdir(p)
#define RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#define CHDIR(p) chdir(p)
#define RMDIR(p) rmdir(p)
#endif
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "%d: %s (%s)\n", __LINE__, #x, FzeroTracksError());      \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)
static void write_file(const char *path, const void *bytes, size_t n) {
  FILE *f = fopen(path, "wb");
  CHECK(f);
  CHECK(fwrite(bytes, 1, n, f) == n);
  CHECK(!fclose(f));
}
static void write_pack(const char *folder, const char *index, const char *id, const char *title) {
  char path[256], hash[65], envelope[1024];
  uint8_t digest[32];
  sha256_compute((const uint8_t *)index,strlen(index),digest);
  for(unsigned i=0;i<32;++i)snprintf(hash+2*i,3,"%02x",digest[i]);
  snprintf(path,sizeof(path),"%s/courses.json",folder);
  write_file(path,index,strlen(index));
  snprintf(envelope,sizeof(envelope),
    "{\"format\":\"snesrecomp.data-pack\",\"version\":1,\"game\":\"f-zero\","
    "\"id\":\"%s\",\"title\":\"%s\",\"base_rom_sha256\":"
    "\"bf16c3c867c58e2ab061c70de9295b6930d63f29f81cc986f5ecae03e0ad18d2\","
    "\"payload\":{\"format\":\"fzero.course-index\",\"file\":\"courses.json\",\"sha256\":\"%s\"}}",
    id,title,hash);
  snprintf(path,sizeof(path),"%s/pack.json",folder);
  write_file(path,envelope,strlen(envelope));
}
static void set_packs(const char *path) {
#ifdef _WIN32
  _putenv_s("FZERO_PACKS_DIR", path);
#else
  setenv("FZERO_PACKS_DIR", path, 1);
#endif
}
int main(void) {
  const char *root = "test-pack-discovery";
  MKDIR(root);
  MKDIR("test-pack-discovery/a");
  MKDIR("test-pack-discovery/b");
  remove("test-pack-discovery/settings/loader.cfg");
  remove("test-pack-discovery/b/pack.json");
  remove("test-pack-discovery/a/music/course.pcm");
  remove("test-pack-discovery/b/music/course.pcm");
  remove("test-pack-discovery/a/music/menu.pcm");
  set_packs(root);
  FzeroCourse c = {0};
  c.block_size = 544;
  c.grid_size = 36;
  c.shortcuts[1] = 0x80; /* Empty, terminated native shortcut table. */
  for (unsigned i = 0; i < 16; ++i)
    c.blocks[512 + i * 2 + 1] = 0x70;
  char error[256];
  CHECK(FzeroCourseFileWrite("test-pack-discovery/a/course.fzc", &c, error,
                             sizeof(error)));
  const char *manifest =
      "{\"format\":1,\"id\":\"one\",\"name\":\"One\",\"author\":\"Test\","
      "\"menu_music\":{\"select\":\"music/menu.pcm\"},"
      "\"cups\":[{\"id\":\"solo\",\"name\":\"Solo\",\"courses\":[\"course\"]}],"
      "\"courses\":[{\"id\":\"course\",\"name\":\"Course\",\"source\":\"course."
      "fzc\"}]}";
  write_pack("test-pack-discovery/a", manifest, "one", "One");
  CHECK(FzeroTracksInit("test-pack-discovery/settings", true));
  const CpPack *one = cp_catalog_find(FzeroTracksCatalog(), "one");
  CHECK(one);
  CHECK(FzeroTrackLoaderEnabled() && FzeroTracksAvailable(one));
  FzeroTrackLoaderEnable(false);
  CHECK(!FzeroTracksAvailable(one));
  CHECK(FzeroTracksSave());
  CHECK(FzeroTracksInit("test-pack-discovery/settings", true));
  one = cp_catalog_find(FzeroTracksCatalog(), "one");
  CHECK(!FzeroTrackLoaderEnabled() && !FzeroTracksAvailable(one));
  FzeroTrackLoaderEnable(true);
  CHECK(FzeroTracksAvailable(one));
  CHECK(FzeroTracksSave());
  CHECK(FzeroTracksInit("test-pack-discovery/settings", true));
  CHECK(FzeroTrackLoaderEnabled());
  FzeroCourse copy;
  CHECK(FzeroPacksLoadCourse("one", 0, &copy, error, sizeof(error)));
  CHECK(FzeroCourseValidate(&copy, error, sizeof(error)));
  /* Music follows the source filename, without any soundtrack declaration. */
  char music_a[CP_PATH], music_b[CP_PATH];
  uint8_t record_hash[32];
  memcpy(record_hash, copy.hash, sizeof(record_hash));
  CHECK(!FzeroPacksHasMusic());
  CHECK(!FzeroPacksCourseMusic("one",0,music_a,sizeof(music_a)));
  MKDIR("test-pack-discovery/a/music");
  write_file("test-pack-discovery/a/music/course.pcm","MSU1\0\0\0\0",8);
  CHECK(FzeroPacksHasMusic());
  CHECK(FzeroPacksCourseMusic("one",0,music_a,sizeof(music_a)));
  CHECK(!FzeroPacksCourseMusic("one",1,music_b,sizeof(music_b)));
  CHECK(FzeroPacksLoadCourse("one",0,&copy,error,sizeof(error)));
  CHECK(!memcmp(record_hash,copy.hash,sizeof(record_hash)));
  /* Prefilled missing defaults, event-only overrides, persistence and removal. */
  CHECK(FzeroMenuMusicPath(4,music_b,sizeof(music_b)));
  CHECK(strstr(music_b,"menu.pcm"));
  CHECK(!FzeroMenuMusicResolve(5,music_b,sizeof(music_b)));
  const unsigned char pcm[] = {'M','S','U','1',0,0,0,0,0,0,0,0};
  write_file("test-pack-discovery/a/music/menu.pcm",pcm,sizeof(pcm));
  CHECK(FzeroMenuMusicResolve(5,music_b,sizeof(music_b)));
  CHECK(!FzeroMenuMusicResolve(6,music_b,sizeof(music_b)));
  CHECK(!FzeroMenuMusicSetPath(4,"test-pack-discovery/a/music/course.pcm"));
  CHECK(FzeroMenuMusicSetPath(0,"test-pack-discovery/a/music/menu.pcm"));
  CHECK(FzeroMenuMusicSetPack("one"));
  FzeroMenuMusicEnable(false);
  CHECK(!FzeroMenuMusicResolve(1,music_b,sizeof(music_b)));
  CHECK(FzeroPacksCourseMusic("one",0,music_a,sizeof(music_a)));
  CHECK(FzeroTracksSave());
  CHECK(FzeroTracksInit("test-pack-discovery/settings",true));
  CHECK(!FzeroMenuMusicEnabled() && FzeroMenuMusicCustom(0));
  CHECK(!strcmp(FzeroMenuMusicPack(),"one"));
  FzeroMenuMusicEnable(true);
  CHECK(FzeroMenuMusicResolve(1,music_b,sizeof(music_b)));
  CHECK(FzeroMenuMusicSetPath(0,""));
  CHECK(!FzeroMenuMusicResolve(1,music_b,sizeof(music_b)));
  CHECK(!remove("test-pack-discovery/a/music/menu.pcm"));
  CHECK(!FzeroMenuMusicResolve(5,music_b,sizeof(music_b)));
  /* A pack-wide module and a course-only module compose without making an
   * undeclared course in another pack inherit those terrain semantics. */
  const char *scoped =
    "{\"format\":1,\"id\":\"scoped\",\"name\":\"Scoped\",\"author\":\"Test\","
    "\"mechanics\":[\"grip.json\"],\"cups\":[{\"id\":\"cup\",\"name\":\"Cup\",\"courses\":[\"a\",\"b\"]}],"
    "\"courses\":[{\"id\":\"a\",\"name\":\"A\",\"source\":\"course.fzc\",\"mechanics\":[\"rainbow.json\"]},"
    "{\"id\":\"b\",\"name\":\"B\",\"source\":\"course.fzc\"}]}";
  const char *grip = "{\"format\":1,\"id\":\"grip\",\"engine\":\"fzero-course-v1\",\"requires\":[\"grip-magnets\",\"up-magnets\"]}";
  const char *rainbow = "{\"format\":1,\"id\":\"rainbow\",\"engine\":\"fzero-course-v1\",\"requires\":[\"rainbow-road\",\"fall-down\"]}";
  write_file("test-pack-discovery/b/grip.json",grip,strlen(grip));
  write_file("test-pack-discovery/b/rainbow.json",rainbow,strlen(rainbow));
  CHECK(FzeroCourseFileWrite("test-pack-discovery/b/course.fzc", &c,error,sizeof(error)));
  write_pack("test-pack-discovery/b", scoped, "scoped", "Scoped");
  CHECK(FzeroTracksInit("test-pack-discovery/settings",true));
  CHECK(FzeroPacksLoadCourse("scoped",0,&copy,error,sizeof(error)) && copy.required==23);
  CHECK(FzeroPacksLoadCourse("scoped",1,&copy,error,sizeof(error)) && copy.required==3);
  CHECK(FzeroPacksLoadCourse("one",0,&copy,error,sizeof(error)) && copy.required==0);
  CHECK(!FzeroPacksCourseMusic("scoped",0,music_b,sizeof(music_b)));
  MKDIR("test-pack-discovery/b/music");
  write_file("test-pack-discovery/b/music/course.pcm","MSU1\0\0\0\0",8);
  CHECK(FzeroPacksCourseMusic("scoped",0,music_b,sizeof(music_b)));
  CHECK(strcmp(music_a,music_b)); /* Same name stays scoped to its pack. */
  CHECK(!remove("test-pack-discovery/b/music/course.pcm"));
  CHECK(!FzeroPacksCourseMusic("scoped",0,music_b,sizeof(music_b)));
  const char *bad_module="{\"format\":2,\"id\":\"grip\",\"engine\":\"fzero-course-v1\",\"requires\":[]}";
  write_file("test-pack-discovery/b/grip.json",bad_module,strlen(bad_module));
  CHECK(FzeroTracksInit("test-pack-discovery/settings",true));
  CHECK(!cp_catalog_find(FzeroTracksCatalog(),"scoped") && FzeroTracksDiagnosticCount());
  CHECK(FzeroCourseFileWrite("test-pack-discovery/b/course.fzc", &c, error,
                             sizeof(error)));
  write_pack("test-pack-discovery/b", manifest, "one", "One");
  CHECK(FzeroTracksInit("test-pack-discovery/settings", false));
  CHECK(!cp_catalog_find(FzeroTracksCatalog(), "one"));
  CHECK(FzeroTracksDiagnosticCount() == 2);
  CHECK(FzeroTracksAvailable(cp_catalog_find(FzeroTracksCatalog(), "retail")));
  CHECK(!FzeroTracksAvailable(
      cp_catalog_find(FzeroTracksCatalog(), "bs-deluxe")));
  CHECK(!remove("test-pack-discovery/b/pack.json"));
  const char *invalid = "{\"format\":1,\"id\":\"bad\",\"name\":\"Bad\","
                        "\"author\":\"Test\",\"courses\":[]}";
  write_pack("test-pack-discovery/b", invalid, "bad", "Bad");
  CHECK(FzeroTracksInit("test-pack-discovery/settings", true));
  CHECK(cp_catalog_find(FzeroTracksCatalog(), "one"));
  CHECK(!cp_catalog_find(FzeroTracksCatalog(), "bad"));
  CHECK(FzeroTracksDiagnosticCount());
  puts("Pack discovery, atomic rejection, duplicate quarantine and loader "
       "persistence passed");
  return 0;
}
