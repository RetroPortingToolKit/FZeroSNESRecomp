#include <filesystem>
#include <string>
extern "C" {
#include "fzero_course_runtime.h"
}
namespace fs=std::filesystem;
static bool key(const std::string &name) {
  if (name.size()!=32) return false;
  for (char c:name) if (!(c>='0' && c<='9') && !(c>='a' && c<='f')) return false;
  return true;
}
/* Copy only the records files the course browser may read after a checkpoint.
 * Never copy ROMs, user quick/save states or arbitrary save-directory files.
 * This runs on the main thread at checkpoint time, never on a fault path. */
extern "C" bool FzeroCaptureRecords(const char *destination, bool erase) {
  try {
    fs::path target(destination);
    fs::path source=erase?target:fs::path(FzeroTracksSaveBaseRoot())/"courses";
    if (!erase) fs::create_directory(target);
    if (!fs::exists(source)) return true;
    unsigned count=0;
    for (const auto &entry:fs::directory_iterator(source)) {
      auto leaf=entry.path().filename().string();
      if (!key(leaf) || !entry.is_directory() || entry.is_symlink()) continue;
      auto file=entry.path()/"records.bin";
      if (erase) { fs::remove(file); fs::remove(entry.path()); continue; }
      if (!fs::exists(file)) continue;
      if (fs::is_symlink(file) || !fs::is_regular_file(file) ||
          fs::file_size(file)>32+0x8000+0x200 || ++count>4096) return false;
      fs::create_directory(target/leaf);
      fs::copy_file(file,target/leaf/"records.bin");
    }
    if (erase) fs::remove(target);
    return true;
  } catch (...) { return false; }
}
