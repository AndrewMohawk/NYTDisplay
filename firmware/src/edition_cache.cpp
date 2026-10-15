#include "edition_cache.h"
#include <FS.h>
#include <LittleFS.h>

namespace edition_cache {
namespace {
constexpr size_t FRAME_BYTES = 1200u * 1600u / 2;
constexpr char PREFIX[] = "/edition-";

bool validEdition(const String& edition) {
  if (edition.length() != 10 || edition[4] != '-' || edition[7] != '-') return false;
  for (int i = 0; i < 10; ++i)
    if (i != 4 && i != 7 && !isDigit(edition[i])) return false;
  return true;
}

String pathFor(const String& edition) { return String(PREFIX) + edition + ".bin"; }

size_t list(String* editions, size_t capacity) {
  size_t count = 0;
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) return 0;
  for (File file = root.openNextFile(); file; file = root.openNextFile()) {
    String name = file.name();
    if (!name.startsWith(PREFIX)) name = "/" + name;
    const bool candidate = name.length() == 23 && name.startsWith(PREFIX) &&
                           name.endsWith(".bin") && file.size() == FRAME_BYTES;
    if (candidate && count < capacity) {
      String edition = name.substring(9, 19);
      if (validEdition(edition)) editions[count++] = edition;
    }
    file.close();
  }
  for (size_t i = 1; i < count; ++i) {
    String value = editions[i];
    size_t j = i;
    while (j && editions[j - 1] > value) {
      editions[j] = editions[j - 1];
      --j;
    }
    editions[j] = value;
  }
  return count;
}
}  // namespace

bool begin() {
  if (!LittleFS.begin(false)) {
    Serial0.println("[cache] Filesystem unavailable; formatting edition cache.");
    if (!LittleFS.begin(true)) return false;
  }
  return true;
}

bool save(const String& edition, const uint8_t* frame, size_t length) {
  if (!validEdition(edition) || !frame || length != FRAME_BYTES) return false;
  const String finalPath = pathFor(edition);
  const String temporaryPath = finalPath + ".tmp";
  LittleFS.remove(temporaryPath);
  File output = LittleFS.open(temporaryPath, FILE_WRITE);
  if (!output) return false;
  const bool complete = output.write(frame, length) == length;
  output.close();
  if (!complete) { LittleFS.remove(temporaryPath); return false; }
  LittleFS.remove(finalPath);
  if (!LittleFS.rename(temporaryPath, finalPath)) {
    LittleFS.remove(temporaryPath);
    return false;
  }

  String editions[MAX_EDITIONS + 1];
  size_t count = list(editions, MAX_EDITIONS + 1);
  while (count > MAX_EDITIONS) {
    LittleFS.remove(pathFor(editions[0]));
    for (size_t i = 1; i < count; ++i) editions[i - 1] = editions[i];
    --count;
  }
  Serial0.printf("[cache] Saved %s; %u edition(s) retained.\n",
                 edition.c_str(), static_cast<unsigned>(count));
  return true;
}

bool load(const String& edition, uint8_t* frame, size_t length) {
  if (!validEdition(edition) || !frame || length != FRAME_BYTES) return false;
  File input = LittleFS.open(pathFor(edition), FILE_READ);
  if (!input || input.size() != length) { if (input) input.close(); return false; }
  const bool complete = input.read(frame, length) == length;
  input.close();
  return complete;
}

bool contains(const String& edition) {
  if (!validEdition(edition)) return false;
  File input = LittleFS.open(pathFor(edition), FILE_READ);
  const bool valid = input && input.size() == FRAME_BYTES;
  if (input) input.close();
  return valid;
}

bool latest(String& edition) {
  String editions[MAX_EDITIONS];
  const size_t count = list(editions, MAX_EDITIONS);
  if (!count) return false;
  edition = editions[count - 1];
  return true;
}

bool adjacent(const String& current, int direction, String& edition) {
  if (direction != -1 && direction != 1) return false;
  String editions[MAX_EDITIONS];
  const size_t count = list(editions, MAX_EDITIONS);
  if (!count) return false;
  int selected = -1;
  for (size_t i = 0; i < count; ++i)
    if (editions[i] == current) { selected = static_cast<int>(i); break; }
  if (selected < 0) selected = static_cast<int>(count) - 1;
  if (count < 2) return false;
  const int target = (selected + direction + static_cast<int>(count)) %
                     static_cast<int>(count);
  edition = editions[target];
  return true;
}
}  // namespace edition_cache
