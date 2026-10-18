#include "dfr1154_dataset.h"
#include "dfr1154_camera.h"

#include <SD_MMC.h>
#include <FS.h>

static WebServer* g_srv;

// Max entries returned by /api/dataset/list. Each entry costs a FAT directory
// read while the single-threaded WebServer is blocked, so keep this small.
#define DFR1154_DS_LIST_MAX 100

// .jpg counts per dataset folder, kept in RAM. Scanning the SD card on every
// /api/dataset/counts request took 30+ s with a few hundred captures and froze
// the whole dashboard, so we scan once at boot and then track changes.
enum { DS_INBOX, DS_SQUIRREL, DS_NOT_SQUIRREL, DS_UNCERTAIN, DS_FOLDER_COUNT };
static const char* const kDsDirs[DS_FOLDER_COUNT] = {
    DFR1154_DS_INBOX, DFR1154_DS_SQUIRREL, DFR1154_DS_NOT_SQUIRREL, DFR1154_DS_UNCERTAIN};
static int s_counts[DS_FOLDER_COUNT];

static int folder_index(const char* dir) {
  if (!dir) return -1;
  for (int i = 0; i < DS_FOLDER_COUNT; i++) {
    if (strcmp(dir, kDsDirs[i]) == 0) return i;
  }
  return -1;
}

static void count_adjust(const char* dir, int delta) {
  int i = folder_index(dir);
  if (i < 0) return;
  s_counts[i] += delta;
  if (s_counts[i] < 0) s_counts[i] = 0;
}

// "Delete all" renames the folder to /dataset/trash_<ms> (one cheap directory
// op) and recreates it empty, so the dashboard is fast again at once. The
// trash folder is then emptied by a low-priority background task: a single
// delete in a folder of thousands of files can take seconds, and doing that in
// loop() stalled pan/spray and the dashboard.
#define DFR1154_DS_TRASH_PREFIX "trash_"
static bool s_purge_pending;
static File s_purge_dir;
static String s_purge_path;
static uint32_t s_purged;
static TaskHandle_t s_purge_task;

/** Count .jpg files by name only — getNextFileName() never opens the file. */
static int scan_jpg_count(const char* dir) {
  File root = SD_MMC.open(dir);
  if (!root || !root.isDirectory()) return 0;
  int n = 0;
  bool is_dir = false;
  String name = root.getNextFileName(&is_dir);
  while (name.length() > 0) {
    if (!is_dir && name.endsWith(".jpg")) n++;
    name = root.getNextFileName(&is_dir);
  }
  root.close();
  return n;
}

static bool safe_name(const String& name) {
  if (name.length() < 5 || name.length() > 48) return false;
  if (!name.endsWith(".jpg")) return false;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name.charAt(i);
  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) {
      return false;
    }
  }
  return true;
}

static const char* folder_for_label(const char* label) {
  if (!label) return nullptr;
  if (strcmp(label, "squirrel") == 0) return DFR1154_DS_SQUIRREL;
  if (strcmp(label, "not_squirrel") == 0) return DFR1154_DS_NOT_SQUIRREL;
  if (strcmp(label, "uncertain") == 0) return DFR1154_DS_UNCERTAIN;
  if (strcmp(label, "inbox") == 0) return DFR1154_DS_INBOX;
  return nullptr;
}

static const char* folder_path(const char* folder) {
  if (!folder) return nullptr;
  if (strcmp(folder, "inbox") == 0) return DFR1154_DS_INBOX;
  if (strcmp(folder, "squirrel") == 0) return DFR1154_DS_SQUIRREL;
  if (strcmp(folder, "not_squirrel") == 0) return DFR1154_DS_NOT_SQUIRREL;
  if (strcmp(folder, "uncertain") == 0) return DFR1154_DS_UNCERTAIN;
  return nullptr;
}

bool dfr1154_dataset_init() {
  bool ok = true;
  ok = SD_MMC.mkdir("/dataset") && ok;
  ok = SD_MMC.mkdir(DFR1154_DS_INBOX) && ok;
  ok = SD_MMC.mkdir(DFR1154_DS_SQUIRREL) && ok;
  ok = SD_MMC.mkdir(DFR1154_DS_NOT_SQUIRREL) && ok;
  ok = SD_MMC.mkdir(DFR1154_DS_UNCERTAIN) && ok;
  SD_MMC.mkdir("/images");
  SD_MMC.mkdir("/triggers");

  uint32_t t0 = millis();
  for (int i = 0; i < DS_FOLDER_COUNT; i++) {
    s_counts[i] = scan_jpg_count(kDsDirs[i]);
  }
  Serial.printf("Dataset counts: inbox=%d squirrel=%d not=%d uncertain=%d (%lu ms)\n",
                s_counts[DS_INBOX], s_counts[DS_SQUIRREL], s_counts[DS_NOT_SQUIRREL],
                s_counts[DS_UNCERTAIN], (unsigned long)(millis() - t0));
  s_purge_pending = true;  // finish any trash left from before a reboot
  return ok;
}

static String base_name(const String& path) {
  int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

/** Open the next /dataset/trash_* folder for purging. False when none are left. */
static bool open_next_trash() {
  File root = SD_MMC.open("/dataset");
  if (!root || !root.isDirectory()) return false;
  bool is_dir = false;
  String name = root.getNextFileName(&is_dir);
  while (name.length() > 0) {
    if (is_dir && base_name(name).startsWith(DFR1154_DS_TRASH_PREFIX)) {
      root.close();
      s_purge_path = name.startsWith("/") ? name : "/dataset/" + name;
      s_purge_dir = SD_MMC.open(s_purge_path);
      Serial.printf("Dataset purge: emptying %s\n", s_purge_path.c_str());
      return (bool)s_purge_dir;
    }
    name = root.getNextFileName(&is_dir);
  }
  root.close();
  return false;
}

/** One purge step: delete one file, or finish a trash folder. False when all done. */
static bool purge_step() {
  if (!s_purge_dir && !open_next_trash()) {
    s_purge_pending = false;
    return false;
  }
  bool is_dir = false;
  String name = s_purge_dir.getNextFileName(&is_dir);
  if (name.length() == 0) {
    s_purge_dir.close();
    SD_MMC.rmdir(s_purge_path);
    Serial.printf("Dataset purge: removed %s (%lu files so far)\n",
                  s_purge_path.c_str(), (unsigned long)s_purged);
    s_purge_path = "";
    return true;  // next step looks for another trash folder
  }
  if (is_dir) return true;
  if (!name.startsWith("/")) name = s_purge_path + "/" + name;
  if (SD_MMC.remove(name)) s_purged++;
  return true;
}

static void purge_task(void*) {
  while (purge_step()) {
    vTaskDelay(pdMS_TO_TICKS(10));  // yield so WiFi and captures get the card too
  }
  Serial.printf("Dataset purge: done (%lu files)\n", (unsigned long)s_purged);
  s_purge_task = nullptr;
  vTaskDelete(nullptr);
}

void dfr1154_dataset_service() {
  if (!s_purge_pending || s_purge_task) return;
  // Core 0, priority 1: below WiFi, and off the core that runs loop().
  xTaskCreatePinnedToCore(purge_task, "ds_purge", 6144, nullptr, 1, &s_purge_task, 0);
}

bool dfr1154_dataset_capture_fb(camera_fb_t* fb, const char* source,
                                char* out_path, size_t out_len) {
  if (!fb || fb->format != PIXFORMAT_JPEG) return false;
  const char* src = (source && source[0]) ? source : "capture";
  char path[72];
  snprintf(path, sizeof(path), DFR1154_DS_INBOX "/IMG_%lu_%s.jpg",
           (unsigned long)millis(), src);
  // millis() names repeat after a reboot, so a capture can overwrite an old file.
  bool replacing = SD_MMC.exists(path);
  if (!dfr1154_save_jpeg_to_sd(path, fb)) return false;
  if (!replacing) count_adjust(DFR1154_DS_INBOX, +1);
  if (out_path && out_len > 0) {
    strncpy(out_path, path, out_len - 1);
    out_path[out_len - 1] = '\0';
  }
  return true;
}

static void handleApiDatasetList() {
  if (!g_srv->hasArg("folder")) {
    g_srv->send(400, "application/json", "{\"ok\":false,\"error\":\"folder\"}");
    return;
  }
  String folder = g_srv->arg("folder");
  const char* dir = folder_path(folder.c_str());
  if (!dir) {
    g_srv->send(400, "application/json", "{\"ok\":false,\"error\":\"bad_folder\"}");
    return;
  }

  File root = SD_MMC.open(dir);
  if (!root || !root.isDirectory()) {
    g_srv->send(200, "application/json", "{\"ok\":true,\"files\":[]}");
    return;
  }

  // Names only (no per-file open/stat) and capped, so a folder with hundreds
  // of captures can't block the WebServer for long.
  String j = F("{\"ok\":true,\"folder\":\"");
  j += folder;
  j += F("\",\"files\":[");
  int listed = 0;
  bool truncated = false;
  bool is_dir = false;
  String name = root.getNextFileName(&is_dir);
  while (name.length() > 0) {
    if (!is_dir) {
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);
      if (name.endsWith(".jpg") && safe_name(name)) {
        if (listed >= DFR1154_DS_LIST_MAX) {
          truncated = true;
          break;
        }
        if (listed > 0) j += ',';
        listed++;
        j += F("{\"name\":\"");
        j += name;
        j += F("\"}");
      }
    }
    name = root.getNextFileName(&is_dir);
  }
  root.close();
  j += F("],\"total\":");
  int idx = folder_index(dir);
  j += String(idx >= 0 ? s_counts[idx] : listed);
  j += F(",\"truncated\":");
  j += truncated ? F("true") : F("false");
  j += F("}");
  g_srv->send(200, "application/json", j);
}

static void handleApiDatasetImage() {
  if (!g_srv->hasArg("folder") || !g_srv->hasArg("name")) {
    g_srv->send(400, "text/plain", "bad request");
    return;
  }
  String name = g_srv->arg("name");
  if (!safe_name(name)) {
    g_srv->send(400, "text/plain", "bad name");
    return;
  }
  const char* dir = folder_path(g_srv->arg("folder").c_str());
  if (!dir) {
    g_srv->send(400, "text/plain", "bad folder");
    return;
  }
  String path = String(dir) + "/" + name;
  File file = SD_MMC.open(path, FILE_READ);
  if (!file) {
    g_srv->send(404, "text/plain", "not found");
    return;
  }
  g_srv->sendHeader("Cache-Control", "no-cache");
  g_srv->streamFile(file, "image/jpeg");
  file.close();
}

static bool move_dataset_file(const char* from_dir, const char* to_dir, const String& name) {
  String from = String(from_dir) + "/" + name;
  String to = String(to_dir) + "/" + name;
  if (!SD_MMC.exists(from)) return false;
  // Same name already in the destination gets replaced, so it doesn't add one.
  if (SD_MMC.exists(to)) {
    SD_MMC.remove(to);
    count_adjust(to_dir, -1);
  }
  if (SD_MMC.rename(from, to)) {
    count_adjust(from_dir, -1);
    count_adjust(to_dir, +1);
    return true;
  }
  File src = SD_MMC.open(from, FILE_READ);
  if (!src) return false;
  File dst = SD_MMC.open(to, FILE_WRITE);
  if (!dst) {
    src.close();
    return false;
  }
  uint8_t buf[512];
  while (src.available()) {
    size_t n = src.read(buf, sizeof(buf));
    if (n == 0) break;
    dst.write(buf, n);
  }
  dst.close();
  src.close();
  SD_MMC.remove(from);
  count_adjust(from_dir, -1);
  count_adjust(to_dir, +1);
  String json_from = from.substring(0, from.length() - 4) + ".json";
  String json_to = to.substring(0, to.length() - 4) + ".json";
  if (SD_MMC.exists(json_from)) {
    if (SD_MMC.exists(json_to)) SD_MMC.remove(json_to);
    SD_MMC.rename(json_from, json_to);
  }
  return true;
}

bool dfr1154_dataset_archive_from_inbox(const char* filename, const char* label) {
  if (!filename || !filename[0]) return false;
  String name(filename);
  if (!safe_name(name)) return false;
  const char* to = folder_for_label(label);
  if (!to) return false;
  if (strcmp(DFR1154_DS_INBOX, to) == 0) return true;
  return move_dataset_file(DFR1154_DS_INBOX, to, name);
}

bool dfr1154_dataset_write_meta(const char* jpg_path, const String& json_body) {
  if (!jpg_path) return false;
  String path(jpg_path);
  if (!path.endsWith(".jpg")) return false;
  String meta_path = path.substring(0, path.length() - 4) + ".json";
  File f = SD_MMC.open(meta_path, FILE_WRITE);
  if (!f) return false;
  f.print(json_body);
  f.close();
  return true;
}

String dfr1154_dataset_read_meta(const char* jpg_path) {
  if (!jpg_path) return String();
  String path(jpg_path);
  if (!path.endsWith(".jpg")) return String();
  String meta_path = path.substring(0, path.length() - 4) + ".json";
  File f = SD_MMC.open(meta_path, FILE_READ);
  if (!f) return String();
  String body = f.readString();
  f.close();
  return body;
}

static void handleApiDatasetMeta() {
  if (!g_srv->hasArg("folder") || !g_srv->hasArg("name")) {
    g_srv->send(400, "application/json", "{}");
    return;
  }
  String name = g_srv->arg("name");
  if (!safe_name(name)) {
    g_srv->send(400, "application/json", "{}");
    return;
  }
  const char* dir = folder_path(g_srv->arg("folder").c_str());
  if (!dir) {
    g_srv->send(400, "application/json", "{}");
    return;
  }
  String path = String(dir) + "/" + name;
  String body = dfr1154_dataset_read_meta(path.c_str());
  if (body.length() == 0) {
    g_srv->send(200, "application/json", "{}");
    return;
  }
  g_srv->send(200, "application/json", body);
}

static void handleApiDatasetTag() {
  if (!g_srv->hasArg("folder") || !g_srv->hasArg("name") || !g_srv->hasArg("label")) {
    g_srv->send(400, "application/json", "{\"ok\":false}");
    return;
  }
  String name = g_srv->arg("name");
  if (!safe_name(name)) {
    g_srv->send(400, "application/json", "{\"ok\":false,\"error\":\"name\"}");
    return;
  }
  const char* from = folder_path(g_srv->arg("folder").c_str());
  const char* to = folder_for_label(g_srv->arg("label").c_str());
  if (!from || !to) {
    g_srv->send(400, "application/json", "{\"ok\":false,\"error\":\"folder\"}");
    return;
  }
  if (strcmp(from, to) == 0) {
    g_srv->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (!move_dataset_file(from, to, name)) {
    g_srv->send(500, "application/json", "{\"ok\":false,\"error\":\"move\"}");
    return;
  }
  g_srv->send(200, "application/json", "{\"ok\":true}");
}

static void handleApiDatasetCapture() {
  const char* src = "manual";
  if (g_srv->hasArg("src")) {
    src = g_srv->arg("src").c_str();
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    g_srv->send(503, "application/json", "{\"ok\":false,\"error\":\"camera\"}");
    return;
  }
  char path[72];
  bool ok = dfr1154_dataset_capture_fb(fb, src, path, sizeof(path));
  esp_camera_fb_return(fb);
  if (!ok) {
    g_srv->send(500, "application/json", "{\"ok\":false,\"error\":\"sd\"}");
    return;
  }
  String j = F("{\"ok\":true,\"path\":\"");
  j += path;
  j += F("\"}");
  g_srv->send(200, "application/json", j);
}

static void handleApiDatasetCounts() {
  String j = F("{\"ok\":true,");
  j += F("\"inbox\":");
  j += String(s_counts[DS_INBOX]);
  j += F(",\"squirrel\":");
  j += String(s_counts[DS_SQUIRREL]);
  j += F(",\"not_squirrel\":");
  j += String(s_counts[DS_NOT_SQUIRREL]);
  j += F(",\"uncertain\":");
  j += String(s_counts[DS_UNCERTAIN]);
  j += F(",\"purging\":");
  j += s_purge_pending ? F("true") : F("false");
  j += F(",\"purged\":");
  j += String(s_purged);
  j += F("}");
  g_srv->send(200, "application/json", j);
}

static void handleApiDatasetClear() {
  const char* dir = g_srv->hasArg("folder") ? folder_path(g_srv->arg("folder").c_str()) : nullptr;
  int idx = folder_index(dir);
  if (idx < 0) {
    g_srv->send(400, "application/json", "{\"ok\":false,\"error\":\"folder\"}");
    return;
  }
  char trash[48];
  snprintf(trash, sizeof(trash), "/dataset/" DFR1154_DS_TRASH_PREFIX "%lu", (unsigned long)millis());
  if (!SD_MMC.rename(dir, trash)) {
    g_srv->send(500, "application/json", "{\"ok\":false,\"error\":\"rename\"}");
    return;
  }
  SD_MMC.mkdir(dir);
  int cleared = s_counts[idx];
  s_counts[idx] = 0;
  s_purge_pending = true;
  Serial.printf("Dataset clear: %s -> %s (%d photos)\n", dir, trash, cleared);
  String j = F("{\"ok\":true,\"cleared\":");
  j += String(cleared);
  j += F("}");
  g_srv->send(200, "application/json", j);
}

void dfr1154_dataset_register(WebServer& server) {
  g_srv = &server;
  server.on("/api/dataset/list", HTTP_GET, handleApiDatasetList);
  server.on("/api/dataset/image", HTTP_GET, handleApiDatasetImage);
  server.on("/api/dataset/meta", HTTP_GET, handleApiDatasetMeta);
  server.on("/api/dataset/tag", HTTP_POST, handleApiDatasetTag);
  server.on("/api/dataset/capture", HTTP_GET, handleApiDatasetCapture);
  server.on("/api/dataset/counts", HTTP_GET, handleApiDatasetCounts);
  server.on("/api/dataset/clear", HTTP_POST, handleApiDatasetClear);
}
