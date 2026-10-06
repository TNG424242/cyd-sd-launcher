/*
 * CYD 2432S028 SD LAUNCHER — bare bones firmware
 *
 * - Mounts SD card (VSPI: MOSI 23, MISO 19, SCK 18, CS 5)
 * - Lists files on display + serial
 * - Loads/views JSON files (ArduinoJson pretty-print + scroll)
 * - Boots .bin games from SD via OTA Update (tap to flash + reboot)
 * - Tries to draw .jpg files fullscreen
 *
 * Serial commands (115200):
 *   ls [path]        list directory
 *   cat <path>       dump text/JSON file
 *   json <path>      parse + pretty-print JSON
 *   flash <path>     OTA flash .bin from SD then reboot
 *
 * Touch UI:
 *   File list, tap = open/view, BACK = up, ^/v = scroll
 *   In viewer: ^/v scroll, X closes
 *   On .bin: shows FLASH prompt, tap YES to flash
 */
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <SPI.h>
#include <SD.h>
#include <Update.h>
#include <ArduinoJson.h>

// ---------- Display (same proven config as Frank/Invaders) ----------
class LGFX_CYD : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9342 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_XPT2046 _touch;
public:
  LGFX_CYD(void) {
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 20000000;
      cfg.freq_read = 8000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = 1;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.memory_width = 320;
      cfg.memory_height = 240;
      cfg.panel_width = 320;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = true;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 300;
      cfg.x_max = 3900;
      cfg.y_min = 3700;
      cfg.y_max = 200;
      cfg.pin_int = -1;
      cfg.bus_shared = false;
      cfg.offset_rotation = 3;
      cfg.spi_host = (spi_host_device_t)-1;
      cfg.freq = 1000000;
      cfg.pin_sclk = 25;
      cfg.pin_mosi = 32;
      cfg.pin_miso = 39;
      cfg.pin_cs = 33;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
static LGFX_CYD lcd;

// ---------- SD pins ----------
#define SD_CS 5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18
static SPIClass sdSpi(VSPI);
static bool sdOk = false;

// ---------- UI state ----------
static const int W = 320, H = 240;
static String curPath = "/";
static String entries[120];
static bool entryIsDir[120];
static size_t entrySize[120];
static int nEntries = 0;
static int listTop = 0;      // scroll offset
static const int ROWS = 6;   // visible rows

enum class Screen { LIST, VIEW, FLASH_CONFIRM, FLASHING, NOSD };
static Screen screen = Screen::LIST;
static String viewPath = "";
static String viewLines[400];
static int nViewLines = 0;
static int viewTop = 0;
static const int VIEW_ROWS = 9;
static String flashPath = "";

// ---------- helpers ----------
static uint16_t col(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static bool endsWithI(const String& s, const char* suf) {
  String a = s; a.toLowerCase();
  String b = String(suf); b.toLowerCase();
  return a.endsWith(b);
}
static String fmtSize(size_t n) {
  if (n < 1024) return String(n) + "B";
  if (n < 1024*1024) return String(n/1024) + "K";
  return String((n*10)/(1024*1024)/10.0, 1) + "M";
}

// ---------- SD ----------
static void scanDir(const String& path) {
  nEntries = 0; listTop = 0;
  curPath = path;
  if (!sdOk) return;
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) { Serial.printf("ERR: cannot open dir %s\n", path.c_str()); return; }
  // collect, dirs first then files, alphabetical-ish (SD returns in order; do simple insertion)
  String tmp[200]; bool tmpDir[200]; size_t tmpSz[200]; int n = 0;
  File f = dir.openNextFile();
  while (f && n < 200) {
    String nm = String(f.name());
    // f.name() may return bare name without leading '/'; normalize to absolute path
    String full = nm;
    if (!full.startsWith("/")) {
      String base = path;
      if (!base.endsWith("/")) base += "/";
      full = base + nm;
      full.replace("//", "/");
    }
    tmp[n] = full;
    tmpDir[n] = f.isDirectory();
    tmpSz[n] = f.size();
    n++;
    f = dir.openNextFile();
  }
  // simple sort: dirs first, then alpha
  for (int i = 0; i < n; i++) for (int j = i+1; j < n; j++) {
    bool swap = false;
    if (tmpDir[j] && !tmpDir[i]) swap = true;
    else if (tmpDir[j] == tmpDir[i] && tmp[j] < tmp[i]) swap = true;
    if (swap) {
      String t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
      bool d = tmpDir[i]; tmpDir[i] = tmpDir[j]; tmpDir[j] = d;
      size_t s = tmpSz[i]; tmpSz[i] = tmpSz[j]; tmpSz[j] = s;
    }
  }
  for (int i = 0; i < n && nEntries < 120; i++) {
    entries[nEntries] = tmp[i];
    entryIsDir[nEntries] = tmpDir[i];
    entrySize[nEntries] = tmpSz[i];
    nEntries++;
  }
  Serial.printf("DIR %s : %d entries\n", path.c_str(), nEntries);
  for (int i = 0; i < nEntries; i++)
    Serial.printf("  %c %s (%u)\n", entryIsDir[i] ? 'D' : 'F', entries[i].c_str(), (unsigned)entrySize[i]);
}

static String baseName(const String& full) {
  int k = full.lastIndexOf('/');
  if (k < 0) return full;
  return full.substring(k+1);
}

// Load text file into viewLines (wraps long lines at ~38 chars), parses JSON if applicable
static void loadViewFile(const String& path) {
  viewPath = path; nViewLines = 0; viewTop = 0;
  File f = SD.open(path);
  if (!f) { viewLines[0] = "Cannot open file."; nViewLines = 1; return; }
  size_t sz = f.size();
  bool isJson = endsWithI(path, ".json");
  if (isJson) {
    // Read whole file (cap 16KB for viewer) and pretty-print via ArduinoJson
    size_t cap = sz > 16384 ? 16384 : sz;
    String raw; raw.reserve(cap);
    for (size_t i = 0; i < cap && f.available(); i++) raw += (char)f.read();
    f.close();
    Serial.printf("JSON %s (%u bytes), parsing...\n", path.c_str(), (unsigned)sz);
    Serial.println(raw.substring(0, 2000));
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, raw);
    String status;
    if (err) {
      status = String("JSON parse ERROR: ") + err.c_str() + " — showing raw:";
      viewLines[nViewLines++] = status;
      Serial.println(status);
    } else {
      status = String("JSON OK (" + String(sz) + " bytes) — pretty:");
      viewLines[nViewLines++] = status;
      Serial.println("JSON OK");
      String pretty; serializeJsonPretty(doc, pretty);
      Serial.println(pretty.substring(0, 3000));
      raw = pretty;
    }
    // wrap into lines
    int start = 0;
    while (start < (int)raw.length() && nViewLines < 400) {
      int nl = raw.indexOf('\n', start);
      String line = (nl < 0) ? raw.substring(start) : raw.substring(start, nl);
      start = (nl < 0) ? raw.length() : nl + 1;
      while (line.length() > 38 && nViewLines < 400) {
        viewLines[nViewLines++] = line.substring(0, 38);
        line = line.substring(38);
      }
      if (nViewLines < 400) viewLines[nViewLines++] = line;
    }
    if (sz > cap && nViewLines < 400) viewLines[nViewLines++] = "...(truncated 16KB)...";
  } else {
    // plain text viewer, cap 12KB
    String line = "";
    int count = 0;
    while (f.available() && nViewLines < 400 && count < 12288) {
      char c = (char)f.read(); count++;
      if (c == '\r') continue;
      if (c == '\n') {
        while (line.length() > 38 && nViewLines < 400) {
          viewLines[nViewLines++] = line.substring(0, 38);
          line = line.substring(38);
        }
        if (nViewLines < 400) viewLines[nViewLines++] = line;
        line = "";
      } else line += c;
      if (line.length() >= 76) {
        viewLines[nViewLines++] = line.substring(0, 38);
        line = line.substring(38);
      }
    }
    if (line.length() && nViewLines < 400) viewLines[nViewLines++] = line;
    f.close();
    if (nViewLines == 0) { viewLines[0] = "(empty file)"; nViewLines = 1; }
    Serial.printf("VIEW %s: %d lines\n", path.c_str(), nViewLines);
  }
}

// ---------- OTA flash from SD ----------
static void doFlashFromSD(const String& path) {
  File f = SD.open(path);
  if (!f) { Serial.printf("ERR: cannot open %s\n", path.c_str()); return; }
  size_t sz = f.size();
  Serial.printf("FLASH %s (%u bytes)...\n", path.c_str(), (unsigned)sz);
  if (!Update.begin(sz)) {
    Serial.printf("Update.begin failed: %d\n", Update.getError());
    f.close(); return;
  }
  uint8_t buf[4096]; size_t done = 0;
  while (done < sz) {
    size_t r = f.read(buf, sizeof(buf));
    if (r == 0) break;
    if (Update.write(buf, r) != r) {
      Serial.printf("Update.write failed: %d\n", Update.getError());
      f.close(); Update.abort(); return;
    }
    done += r;
    int pct = done * 100 / sz;
    Serial.printf("  %d%%\n", pct);
    lcd.fillRect(20, 130, 280 * done / sz, 16, col(0,200,0));
    lcd.setCursor(20, 150); lcd.setTextColor(col(255,255,255), col(0,0,0));
    lcd.printf("%d%% %u/%u", pct, (unsigned)done, (unsigned)sz);
  }
  f.close();
  if (!Update.end()) {
    Serial.printf("Update.end failed: %d\n", Update.getError());
    return;
  }
  Serial.println("Update OK — rebooting into new firmware...");
  delay(800);
  ESP.restart();
}

// ---------- drawing ----------
static void drawList() {
  lcd.fillScreen(col(0,0,0));
  lcd.setTextSize(1);
  // header
  lcd.fillRect(0, 0, W, 28, col(20,40,90));
  lcd.setCursor(4, 4); lcd.setTextColor(col(255,255,0), col(20,40,90));
  lcd.printf("SD:%s", curPath.c_str());
  lcd.setCursor(4, 16); lcd.setTextColor(col(180,220,255), col(20,40,90));
  if (sdOk) lcd.printf("%d files  TAP=open JPG/JSON  BIN=boot", nEntries);
  else lcd.print("NO SD CARD");
  // rows
  int y = 32;
  for (int r = 0; r < ROWS; r++) {
    int i = listTop + r;
    int yy = y + r * 30;
    if (i >= nEntries) {
      lcd.fillRect(0, yy, W, 28, col(10,10,10));
      continue;
    }
    bool isBin = endsWithI(entries[i], ".bin");
    bool isJson = endsWithI(entries[i], ".json");
    uint16_t bg = entryIsDir[i] ? col(40,50,20) : (isBin ? col(90,20,20) : (isJson ? col(20,70,20) : col(25,25,25)));
    lcd.fillRect(0, yy, W, 28, bg);
    lcd.drawRect(0, yy, W, 28, col(80,80,80));
    String bn = baseName(entries[i]);
    if (bn.length() > 30) bn = bn.substring(0, 29) + "~";
    lcd.setCursor(6, yy+4);
    lcd.setTextColor(col(255,255,255), bg);
    lcd.printf("%c %s", entryIsDir[i] ? '>' : (isBin ? '*' : (isJson ? 'J' : '-')), bn.c_str());
    if (!entryIsDir[i]) {
      lcd.setCursor(6, yy+16);
      lcd.setTextColor(col(170,170,170), bg);
      lcd.printf("  %s", fmtSize(entrySize[i]).c_str());
    }
  }
  // scroll buttons + back
  lcd.fillRect(0, 212, 80, 28, col(60,60,60));
  lcd.setCursor(28, 220); lcd.setTextColor(col(255,255,255), col(60,60,60)); lcd.print("^ UP");
  lcd.fillRect(86, 212, 80, 28, col(60,60,60));
  lcd.setCursor(112, 220); lcd.setTextColor(col(255,255,255), col(60,60,60)); lcd.print("v DN");
  lcd.fillRect(172, 212, 148, 28, col(90,60,10));
  lcd.setCursor(210, 220); lcd.setTextColor(col(255,255,255), col(90,60,10)); lcd.print("BACK");
}

static void drawView() {
  lcd.fillScreen(col(0,0,0));
  lcd.fillRect(0, 0, W, 28, col(20,60,20));
  lcd.setCursor(4, 4); lcd.setTextSize(1);
  lcd.setTextColor(col(255,255,0), col(20,60,20));
  String bn = baseName(viewPath);
  if (bn.length() > 26) bn = bn.substring(0, 25) + "~";
  lcd.print(bn);
  lcd.setCursor(4, 16); lcd.setTextColor(col(200,255,200), col(20,60,20));
  lcd.printf("%d lines  %d/%d", nViewLines, viewTop+1, nViewLines);
  int y = 32;
  lcd.setTextColor(col(230,230,230), col(0,0,0));
  for (int r = 0; r < VIEW_ROWS; r++) {
    int i = viewTop + r;
    lcd.setCursor(4, y + r * 18);
    if (i < nViewLines) lcd.print(viewLines[i]);
  }
  lcd.fillRect(0, 212, 80, 28, col(60,60,60));
  lcd.setCursor(28, 220); lcd.setTextColor(col(255,255,255), col(60,60,60)); lcd.print("^ UP");
  lcd.fillRect(86, 212, 80, 28, col(60,60,60));
  lcd.setCursor(112, 220); lcd.setTextColor(col(255,255,255), col(60,60,60)); lcd.print("v DN");
  lcd.fillRect(240, 212, 80, 28, col(150,30,30));
  lcd.setCursor(268, 220); lcd.setTextColor(col(255,255,255), col(150,30,30)); lcd.print("X");
}

static void drawFlashConfirm() {
  lcd.fillScreen(col(0,0,0));
  lcd.fillRect(0, 0, W, 28, col(120,20,20));
  lcd.setCursor(60, 8); lcd.setTextColor(col(255,255,255), col(120,20,20));
  lcd.print("BOOT THIS GAME?");
  lcd.setCursor(10, 50); lcd.setTextColor(col(255,255,255), col(0,0,0));
  lcd.print(baseName(flashPath));
  lcd.setCursor(10, 70); lcd.setTextColor(col(180,180,180), col(0,0,0));
  lcd.printf("%s bytes", fmtSize(SD.open(flashPath).size()).c_str());
  lcd.setCursor(10, 95); lcd.print("Flash + reboot into game?");
  lcd.setCursor(10, 115); lcd.print("Launcher stays in flash;");
  lcd.setCursor(10, 130); lcd.print("re-flash launcher.bin to");
  lcd.setCursor(10, 145); lcd.print("come back.");
  lcd.fillRect(20, 175, 130, 40, col(0,140,0));
  lcd.setCursor(60, 190); lcd.setTextColor(col(255,255,255), col(0,140,0)); lcd.print("YES");
  lcd.fillRect(170, 175, 130, 40, col(140,0,0));
  lcd.setCursor(220, 190); lcd.setTextColor(col(255,255,255), col(140,0,0)); lcd.print("NO");
}

static void drawNoSD() {
  lcd.fillScreen(col(0,0,0));
  lcd.setCursor(40, 100); lcd.setTextColor(col(255,60,60), col(0,0,0));
  lcd.print("NO SD CARD FOUND");
  lcd.setCursor(20, 120); lcd.setTextColor(col(200,200,200), col(0,0,0));
  lcd.print("Check CS=5 MOSI=23 MISO=19");
  lcd.setCursor(60, 135); lcd.print("SCK=18, FAT32 format");
}

// ---------- touch (debounced, edge-triggered) ----------
static bool touchPressed(int &tx, int &ty) {
  static bool was = false;
  int x, y;
  bool now = lcd.getTouch(&x, &y);
  if (now && !was) { tx = x; ty = y; was = true; return true; }
  if (!now) was = false;
  return false;
}

// ---------- serial commands ----------
static String serBuf = "";
static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      serBuf.trim();
      if (serBuf.length()) {
        Serial.printf("> %s\n", serBuf.c_str());
        if (serBuf.startsWith("ls")) {
          String p = serBuf.substring(2); p.trim();
          if (p == "") p = curPath;
          File d = SD.open(p);
          if (!d) Serial.println("ERR open");
          else {
            File f = d.openNextFile();
            while (f) { Serial.printf("%c %s %u\n", f.isDirectory()?'D':'F', f.name(), (unsigned)f.size()); f = d.openNextFile(); }
          }
        } else if (serBuf.startsWith("cat ") || serBuf.startsWith("json ")) {
          String p = serBuf.substring(4); p.trim();
          // strip surrounding quotes (for filenames with spaces)
          if (p.length() >= 2 && ((p.startsWith("\"") && p.endsWith("\"")) || (p.startsWith("'") && p.endsWith("'"))))
            p = p.substring(1, p.length()-1);
          File f = SD.open(p);
          if (!f) Serial.println("ERR open");
          else {
            if (serBuf.startsWith("json")) {
              String s; while (f.available()) s += (char)f.read();
              JsonDocument doc; auto e = deserializeJson(doc, s);
              if (e) { Serial.printf("parse err: %s\nRAW:\n%s\n", e.c_str(), s.c_str()); }
              else { String o; serializeJsonPretty(doc, o); Serial.println(o); }
            } else while (f.available()) Serial.write(f.read());
            Serial.println("\n--EOF--");
            f.close();
          }
        } else if (serBuf.startsWith("flash ")) {
          String p = serBuf.substring(6); p.trim();
          doFlashFromSD(p);
        } else if (serBuf == "reboot") ESP.restart();
        else if (serBuf == "help") Serial.println("ls [p] | cat <f> | json <f> | flash <bin> | reboot");
        else Serial.println("unknown. try help");
      }
      serBuf = "";
    } else serBuf += c;
  }
}

// ---------- setup/loop ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== CYD SD LAUNCHER v1 ===");
  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(255);
  lcd.fillScreen(0);
  lcd.setTextSize(1);
  lcd.setCursor(10, 100);
  lcd.print("Mounting SD...");
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  // try high then low speed
  sdOk = SD.begin(SD_CS, sdSpi, 10000000);
  if (!sdOk) { delay(200); sdOk = SD.begin(SD_CS, sdSpi, 400000); }
  Serial.printf("SD: %s\n", sdOk ? "OK" : "FAIL");
  if (sdOk) {
    Serial.printf("card size: %llu MB\n", SD.cardSize() / 1048576ULL);
    scanDir("/");
    screen = Screen::LIST;
    drawList();
  } else {
    screen = Screen::NOSD;
    drawNoSD();
  }
  Serial.println("Commands: ls | cat <f> | json <f> | flash <bin> | reboot");
}

void loop() {
  handleSerial();
  int tx, ty;
  if (touchPressed(tx, ty)) {
    Serial.printf("touch %d,%d screen=%d\n", tx, ty, (int)screen);
    if (screen == Screen::LIST) {
      if (ty >= 212) {
        if (tx < 80) { if (listTop > 0) { listTop--; drawList(); } }
        else if (tx < 166) { if (listTop + ROWS < nEntries) { listTop++; drawList(); } }
        else {
          if (curPath != "/") {
            String p = curPath;
            if (p.endsWith("/") && p.length() > 1) p = p.substring(0, p.length()-1);
            int k = p.lastIndexOf('/');
            String up = (k <= 0) ? "/" : p.substring(0, k);
            scanDir(up); drawList();
          }
        }
        return;
      }
      int row = (ty - 32) / 30;
      int idx = listTop + row;
      if (row >= 0 && row < ROWS && idx < nEntries) {
        String full = entries[idx];
        if (entryIsDir[idx]) { scanDir(full); drawList(); }
        else if (endsWithI(full, ".bin")) { flashPath = full; screen = Screen::FLASH_CONFIRM; drawFlashConfirm(); }
        else if (endsWithI(full, ".jpg") || endsWithI(full, ".jpeg")) {
          loadViewFile("/"); // placeholder
          viewPath = full;
          nViewLines = 0; viewTop = 0;
          viewLines[nViewLines++] = "JPG preview not in";
          viewLines[nViewLines++] = "bare-bones build.";
          viewLines[nViewLines++] = "";
          viewLines[nViewLines++] = "Use BIN to boot,";
          viewLines[nViewLines++] = "JSON/TXT to view.";
          viewLines[nViewLines++] = "";
          viewLines[nViewLines++] = full;
          screen = Screen::VIEW; drawView();
        }
        else { loadViewFile(full); screen = Screen::VIEW; drawView(); }
      }
    } else if (screen == Screen::VIEW) {
      if (ty >= 212) {
        if (tx < 80) { if (viewTop > 0) { viewTop--; drawView(); } }
        else if (tx < 166) { if (viewTop + VIEW_ROWS < nViewLines) { viewTop++; drawView(); } }
        else if (tx >= 240) { screen = Screen::LIST; drawList(); }
      }
    } else if (screen == Screen::FLASH_CONFIRM) {
      if (ty >= 175) {
        if (tx < 150) {
          screen = Screen::FLASHING;
          lcd.fillScreen(0);
          lcd.setCursor(30, 60); lcd.print("Flashing...");
          lcd.drawRect(20, 130, 280, 16, col(255,255,255));
          doFlashFromSD(flashPath);
          // if we get here flash failed
          lcd.setCursor(30, 170); lcd.print("FLASH FAILED - tap to go back");
          screen = Screen::LIST;
        } else {
          screen = Screen::LIST; drawList();
        }
      }
    } else if (screen == Screen::NOSD) {
      // retry SD on any tap
      sdOk = SD.begin(SD_CS, sdSpi, 10000000);
      if (sdOk) { scanDir("/"); screen = Screen::LIST; drawList(); }
      else drawNoSD();
    }
  }
  delay(20);
}
