#include "fzero_tracks.h"
#include "fzero_packs.h"
#include "fzero_title.h"
#include "fzero_menu_music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif
static CpCatalog catalog;
static char root_path[CP_PATH], error_text[256], title_style[CP_ID];
static bool has_deluxe, loader_enabled, title_enabled;
static char diagnostics[CP_PACKS][256];
static unsigned diagnostic_count;
const CpCatalog *FzeroTracksCatalog(void) { return &catalog; }
const char *FzeroTracksRoot(void) { return root_path; }
const char *FzeroTracksError(void) { return error_text; }
bool FzeroTrackLoaderEnabled(void) { return loader_enabled; }
void FzeroTrackLoaderEnable(bool enabled) { loader_enabled = enabled; }
static bool fail(const char *s) {
  snprintf(error_text, sizeof(error_text), "%s", s);
  return false;
}
void FzeroTracksReport(const char *s) {
  if (diagnostic_count < CP_PACKS)
    snprintf(diagnostics[diagnostic_count++], 256, "%s", s);
  fprintf(stderr, "[pack-loader] %s\n", s);
}
unsigned FzeroTracksDiagnosticCount(void) { return diagnostic_count; }
const char *FzeroTracksDiagnostic(unsigned i) {
  return i < diagnostic_count ? diagnostics[i] : "";
}
bool FzeroTracksHidden(const CpPack *p) {
  (void)p;
  return false;
}
bool FzeroTracksBundled(const CpPack *p) {
  return p && FzeroPacksContains(p->id);
}
const char *FzeroTracksPatch(const CpPack *p) {
  (void)p;
  return "";
}
bool FzeroTracksEnabled(const CpPack *p) {
  return p && (!strcmp(p->adapter, "retail") ||
               !strcmp(p->adapter, "bs-deluxe") || loader_enabled);
}
bool FzeroTracksAvailable(const CpPack *p) {
  if (!p)
    return false;
  if (!strcmp(p->adapter, "retail"))
    return true;
  if (!strcmp(p->adapter, "bs-deluxe"))
    return has_deluxe;
  return loader_enabled && FzeroPacksContains(p->id);
}
bool FzeroTracksEnable(const CpPack *p, bool enabled) {
  if (!p || !FzeroPacksContains(p->id))
    return false;
  loader_enabled = enabled;
  return true;
}
bool FzeroTracksSetPatch(const CpPack *p, const char *path) {
  (void)p;
  (void)path;
  return fail("Install a pack folder or ZIP in mods/packs");
}
bool FzeroTracksReadMusicSources(FzeroMusicSources *out) {
  return FzeroPacksMusicSources(out);
}
void FzeroTracksDiscover(const uint8_t *stock, size_t size) {
  (void)stock;
  (void)size;
}
bool FzeroTracksTitleEnabled(void) { return title_enabled; }
void FzeroTracksEnableTitle(bool enabled) { title_enabled = enabled; }
const char *FzeroTracksTitleStyle(void) { return title_style; }
bool FzeroTracksSetTitleStyle(const char *s) {
  if (!s || strlen(s) >= sizeof(title_style) || !FzeroTitleFind(s))
    return false;
  strcpy(title_style, s);
  return true;
}
static void add_builtin(const char *id, const char *name, const char *adapter,
                        const char *const *cups, unsigned count,
                        const char *const *tracks) {
  CpPack *p = calloc(1, sizeof(*p));
  if (!p) {
    fail("Out of memory");
    return;
  }
  snprintf(p->id, sizeof(p->id), "%s", id);
  snprintf(p->name, sizeof(p->name), "%s", name);
  snprintf(p->adapter, sizeof(p->adapter), "%s", adapter);
  snprintf(p->author, sizeof(p->author), "%s",
           !strcmp(id, "retail") ? "Nintendo"
                                 : "GuyPerfect, PowerPanda, Porthor, Catador");
  p->cup_count = count;
  p->track_count = count * 5;
  static const char *const labels[] = {"Knight League", "Queen League",
                                       "King League", "BS-1 League",
                                       "BS-2 League"};
  for (unsigned i = 0; i < count; ++i) {
    snprintf(p->cups[i].id, CP_ID, "%s", cups[i]);
    snprintf(p->cups[i].name, CP_NAME, "%s", labels[i]);
    p->cups[i].slot = i;
    for (unsigned j = 0; j < 5; ++j) {
      CpTrack *t = &p->tracks[i * 5 + j];
      snprintf(t->id, CP_ID, "%s-%u", cups[i], j + 1);
      snprintf(t->name, CP_NAME, "%s", tracks[i * 5 + j]);
      snprintf(t->cup, CP_ID, "%s", cups[i]);
      t->slot = j;
    }
  }
  cp_catalog_add(&catalog, p, error_text, sizeof(error_text));
  free(p);
}
static bool directory_exists(const char *path) {
#ifdef _WIN32
  DWORD attributes = GetFileAttributesA(path);
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat info;
  return !stat(path, &info) && S_ISDIR(info.st_mode);
#endif
}
static bool ensure_directory(const char *path) {
  if (directory_exists(path))
    return true;
  char parent[CP_PATH];
  size_t length = strlen(path);
  if (!length || length >= sizeof(parent))
    return false;
  memcpy(parent, path, length + 1);
  while (length > 1 &&
         (parent[length - 1] == '/' || parent[length - 1] == '\\'))
    parent[--length] = 0;
  char *last = NULL;
  for (char *p = parent; *p; ++p)
    if (*p == '/' || *p == '\\')
      last = p;
  if (last && last > parent && last[-1] != ':') {
    *last = 0;
    if (!ensure_directory(parent))
      return false;
  }
#ifdef _WIN32
  if (!_mkdir(path))
    return true;
#else
  if (!mkdir(path, 0755))
    return true;
#endif
  return directory_exists(path);
}

bool FzeroTracksInit(const char *root, bool deluxe_available) {
  cp_catalog_free(&catalog);
  error_text[0] = 0;
  diagnostic_count = 0;
  has_deluxe = deluxe_available;
  loader_enabled = true;
  title_enabled = false;
  FzeroTitleCatalogInit();
  FzeroMenuMusicInit();
  strcpy(title_style, "original");
  if (!root || !*root || strlen(root) >= sizeof(root_path) - 32)
    return fail("Pack settings path too long");
  strcpy(root_path, root);
  static const char *const cups[] = {"knight", "queen", "king", "bs-1", "bs-2"};
  static const char *const tracks[] = {
      "Mute City I",  "Big Blue",      "Sand Ocean",    "Death Wind I",
      "Silence",      "Mute City II",  "Port Town I",   "Red Canyon I",
      "White Land I", "White Land II", "Mute City III", "Death Wind II",
      "Port Town II", "Red Canyon II", "Fire Field",    "Forest I",
      "Big Blue II",  "Sand Storm I",  "Forest II",     "Silence II",
      "Mute City IV", "Forest III",    "Sand Storm II", "Metal Fort I",
      "Metal Fort II"};
  add_builtin("retail", "F-Zero", "retail", cups, 3, tracks);
  add_builtin("bs-deluxe", "BS Deluxe", "bs-deluxe", cups, 5, tracks);

  const char *directory = getenv("FZERO_PACKS_DIR");
  FzeroPacksDiscover(&catalog,
                     directory && *directory ? directory : "mods/packs");
  char path[CP_PATH + 32], line[1200];
  snprintf(path, sizeof(path), "%s/loader.cfg", root_path);
  FILE *f = fopen(path, "rb");
  if (f) {
    while (fgets(line, sizeof(line), f)) {
      line[strcspn(line, "\r\n")] = 0;
      FzeroMenuMusicRead(line);
      if (!strcmp(line, "enabled=1"))
        loader_enabled = true;
      else if (!strcmp(line, "enabled=0"))
        loader_enabled = false;
      if (!strncmp(line, "title=", 6))
        FzeroTracksSetTitleStyle(line + 6);
      if (!strcmp(line, "title_enabled=1"))
        title_enabled = true;
    }
    fclose(f);
  }
  const char *enabled = getenv("FZERO_PACK_LOADER");
  if (enabled)
    loader_enabled = !strcmp(enabled, "1");
  return true;
}
bool FzeroTracksWriteSettings(FILE *f) {
  return fprintf(f, "enabled=%u\ntitle=%s\ntitle_enabled=%u\n",
                 loader_enabled, title_style, title_enabled) > 0 &&
         FzeroMenuMusicWrite(f);
}
bool FzeroTracksSave(void) {
  if (!ensure_directory(root_path))
    return fail("Cannot create pack settings directory");
  char path[CP_PATH + 32], tmp[CP_PATH + 32];
  snprintf(path, sizeof(path), "%s/loader.cfg", root_path);
  snprintf(tmp, sizeof(tmp), "%s/loader.cfg.tmp", root_path);
  FILE *f = fopen(tmp, "wb");
  if (!f)
    return fail("Cannot write loader settings");
  bool ok = FzeroTracksWriteSettings(f);
  if (fclose(f))
    ok = false;
#ifdef _WIN32
  if (ok)
    ok = MoveFileExA(tmp, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  if (ok)
    ok = rename(tmp, path) == 0;
#endif
  return ok ? true : fail("Cannot save loader settings");
}
