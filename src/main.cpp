#define BLYNK_TEMPLATE_ID "TMPL6kJtWLfgp"
#define BLYNK_TEMPLATE_NAME "DO AN TOT NGHIEP"
#define BLYNK_AUTH_TOKEN "kdca5KKlaTOACn2_zupkWIWEANXoSfVc"

// MK ESP32_AP= 12345678, MK KEYPAD LÀ 1234, mk master là 000A. 
// 2 THẺ LƯU LÀ 74-DC-6A-05, 98-A1-91-E3

#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <Adafruit_Fingerprint.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>s
#include <Preferences.h>
#include <ESPmDNS.h>
#include <BlynkSimpleEsp32.h>

// ===== LCD =====
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== KEYPAD =====
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {32, 33, 25, 26};
byte colPins[COLS]  = {27, 14, 12, 13};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ===== CHÂN ĐIỀU KHIỂN =====
const int relayPin  = 2;
const int buzzerPin = 15;
const int buttonPin = 35;

#define DOOR_OPEN  1
#define DOOR_CLOSE 0
// ===== RFID =====
#define SS_PIN  5
#define RST_PIN 4
MFRC522 mfrc522(SS_PIN, RST_PIN);
#define MAX_CARDS 5
#define UID_SIZE  4

// ===== VÂN TAY AS608 =====
#define RXD2 16
#define TXD2 17
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
#define FP_DEFAULT_1_ID 1
#define FP_DEFAULT_2_ID 2
#define FP_MAX_ID       5

// ===== EEPROM - MẬT KHẨU =====
#define PASS_ADDR 100
#define PASS_LEN  8

BlynkTimer blynkTimer; 
WidgetTerminal terminal(V3);
bool doorOpen      = false;
int adminCountdown = 0; // giây còn lại của session
bool webEnrollingFP = false;  // ← THÊM
bool ignoreV1Once = false;  // ← THÊM
bool ignoreV2Once = false;  // ← THÊM



// ===== ADMIN SESSION =====
bool     adminSession     = false;
unsigned long adminSessionStart = 0;
#define  ADMIN_SESSION_TIMEOUT 300000UL  // 5 phút, 1 phút=60,000

// Sub-state cho các lệnh admin
enum AdminState {
  ADMIN_IDLE,
  ADMIN_WAIT_MASTER,     // chờ nhập master pass để mở session
  ADMIN_CHANGE_PASS_OLD, // chờ nhập pass cũ
  ADMIN_CHANGE_PASS_NEW1,// chờ nhập pass mới lần 1
  ADMIN_CHANGE_PASS_NEW2, // chờ nhập pass mới lần 2
  ADMIN_ADD_RFID,        // ← thêm
  ADMIN_DEL_RFID,         // ← thêm
  ADMIN_ADD_FP,        // ← THÊM
  ADMIN_DEL_FP,        // ← THÊM
  ADMIN_DEL_FP_CONFIRM,
  ADMIN_RESET_KP_CONFIRM,    // ← THÊM
  ADMIN_RESET_RFID_CONFIRM,  // ← THÊM
  ADMIN_RESET_FP_CONFIRM,    // ← THÊM
  ADMIN_RESET_ALL_CONFIRM,  // ← THÊM
  ADMIN_VIEW_KP,     // ← THÊM
  ADMIN_VIEW_RFID,   // ← THÊM
  ADMIN_VIEW_FP     // ← THÊM
};
AdminState adminState = ADMIN_IDLE;
String pendingCmd = "";  // lệnh admin đang chờ sau khi xác thực
String webNewPass1 = ""; // lưu pass mới lần 1
int webNewPassWrong = 0;  // ← thêm biến này
int adminWrongAttempts = 0;
int webFPID = 0;          // ID FP đang thao tác trên web
int webFPStep = 0;        // bước enroll: 0=chờ ID, 1=scan lần 1, 2=scan lần 2

// ===== BLYNK WEB STATE =====
enum WebState {
  WEB_IDLE,
  WEB_WAIT_KP_PASS,    // chờ nhập mật khẩu keypad
  WEB_SCAN_FP,         // đang quét FP để mở cửa
};
WebState webState   = WEB_IDLE;
int webWrongAttempts = 0;        // đếm sai riêng cho web
unsigned long webFPStart = 0;    // thời điểm bắt đầu quét FP
#define WEB_FP_TIMEOUT 10000UL   // 10 giây

// ===== WIFI & WEB SERVER =====
WebServer server(80);
Preferences prefs;
String savedSSID = "";
String savedPass  = "";
bool   apMode     = false;
unsigned long disconnectTime = 0;
const unsigned long RECONNECT_TIMEOUT = 60000; // 60 giây

// ===== MENU =====
String menuItems[]  = {"#:Check mode","A:Change pass","B:Add/del RFID","C:Add/del FP","D:Reset data"};
String checkItems[] = {"0:Back to menu","1:Check KP","2:Check FP"};
String rfidItems[]  = {"0:Back to menu","1:Add RFID","2:Dele RFID"};
String fpItems[]    = {"0:Back to menu","1:Add FP","2:Dele FP"};
String resetItems[] = {"0:Back to menu","1:Reset KP","2:Reset RFID","3:Reset FP","4:Reset all"};

int menuLength  = 5, menuIndex  = 0;
int checkLength = 3, checkIndex = 0;
int rfidLength  = 3, rfidIndex  = 0;
int fpLength    = 3, fpIndex    = 0;
int resetLength = 5, resetIndex = 0;

// ===== STATE =====
enum State {
  MAIN_MENU, CHECK_MENU, CHECK_PASS, CHECK_RFID, CHECK_FP_LCD, LOCKED,
  MASTER_PASS, CHANGE_OLD, CHANGE_NEW1, CHANGE_NEW2,
  RFID_MASTER, RFID_MENU, RFID_ADD_LCD, RFID_DEL_LCD,
  FP_MASTER, FP_MENU,FP_ADD_LCD, FP_DEL_LCD,
  RESET_KP_LCD, RESET_RFID_LCD, RESET_FP_LCD, RESET_ALL_LCD,
  RESET_MASTER, RESET_MENU
};
State currentState = MAIN_MENU;
State savedStateBeforeFP = MAIN_MENU;  // ← THÊM
State savedStateBeforeRFID = MAIN_MENU;  // ← THÊM vào khai báo biến global
State savedStateBeforeFPAdmin = MAIN_MENU;  // ← THÊM
State savedStateBeforeReset = MAIN_MENU;  // ← THÊM biến global

// ===== MẬT KHẨU =====
String masterPass;
String adminPass   = "000A";
String enteredPass = "";
String newPass1    = "";
int wrongAttempts    = 0;
int wrongNewAttempts = 0;

// ============================================================
// WIFI - LƯU / ĐỌC CREDENTIALS
// ============================================================
void saveWiFiCreds(String ssid, String pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

void loadWiFiCreds() {
  prefs.begin("wifi", true);
  savedSSID = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  prefs.end();
}

// ============================================================
// WIFI - KẾT NỐI STA MODE
// ============================================================
bool connectWiFi() {
  if (savedSSID.length() == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPass.c_str());

  lcd.clear(); lcd.setCursor(0,0); lcd.print("Connecting WiFi");
  lcd.setCursor(0,1); lcd.print(savedSSID.substring(0,16));

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500); tries++;
    lcd.setCursor(15,1); lcd.print(tries % 2 == 0 ? "." : " ");
  }

  if (WiFi.status() == WL_CONNECTED) {
    MDNS.begin("esp32-vth");
    String ip = WiFi.localIP().toString();
    Serial.println("WiFi OK: " + ip);

    // Dùng namespace riêng "net" để tránh conflict với "boot"
    prefs.begin("net", false);
    prefs.putString("ip", ip);
    prefs.end();

    lcd.clear(); lcd.setCursor(0,0); lcd.print("WiFi Connected!");
    lcd.setCursor(0,1); lcd.print(ip);
    delay(2000);
    apMode = false;
    return true;
}

  lcd.clear(); lcd.setCursor(0,0); lcd.print("WiFi Failed!");
  delay(1500);
  return false;
}


void setupWebServer() {
  server.on("/", []() {
    File f = LittleFS.open("/index.html", "r");
    if (!f) { server.send(404, "text/plain", "Not found"); return; }
    server.streamFile(f, "text/html"); f.close();
  });
  server.on("/style.css", []() {
    File f = LittleFS.open("/style.css", "r");
    if (!f) { server.send(404, "text/plain", "Not found"); return; }
    server.streamFile(f, "text/css"); f.close();
  });
  server.on("/script.js", []() {
    File f = LittleFS.open("/script.js", "r");
    if (!f) { server.send(404, "text/plain", "Not found"); return; }
    server.streamFile(f, "application/javascript"); f.close();
  });
  server.on("/scan", []() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + WiFi.RSSI(i) +
              ",\"secure\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
    }
    json += "]";
    WiFi.scanDelete();
    server.send(200, "application/json", json);
  });
  server.on("/save", []() {
    if (!server.hasArg("ssid") || server.arg("ssid").length() == 0) {
      server.send(400, "text/plain", "Missing SSID"); return;
    }
    saveWiFiCreds(server.arg("ssid"), server.arg("pass"));
    server.send(200, "text/plain", "OK");
    delay(1500); ESP.restart();
  });
  server.on("/status", []() {
    String json = "{\"ssid\":\"" + savedSSID + "\",\"ip\":\"" +
                  WiFi.localIP().toString() + "\",\"connected\":" +
                  (WiFi.status() == WL_CONNECTED ? "true" : "false") + "}";
    server.send(200, "application/json", json);
  });
  server.on("/last-result", []() {
    prefs.begin("boot", true);
    String result = prefs.getString("last", "");
    prefs.end();

    prefs.begin("net", true);        // ← đọc IP từ namespace riêng
    String ip = prefs.getString("ip", "");
    prefs.end();

    String response = result;
    if (result == "ok" && ip.length() > 0) response += "|" + ip;
    server.send(200, "text/plain", response);
});
  server.begin();
}
// ============================================================
// WIFI - AP MODE + WEB SERVER
// ============================================================
void startAPMode() {
  apMode = true;
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_AP", "12345678");

  Serial.println("AP Mode IP: " + WiFi.softAPIP().toString());
  lcd.clear(); lcd.setCursor(0,0); lcd.print("AP:ESP32_AP");
  lcd.setCursor(0,1); lcd.print("192.168.4.1");
  delay(2000);
  setupWebServer();  // ← dùng chung 1 hàm
}

// ============================================================
// EEPROM - MẬT KHẨU
// ============================================================
void savePassword(String pass) {
  for (int i = 0; i < PASS_LEN; i++)
    EEPROM.write(PASS_ADDR + i, i < (int)pass.length() ? pass[i] : 0);
  EEPROM.commit();
}

String loadPassword() {
  String pass = "";
  for (int i = 0; i < PASS_LEN; i++) {
    char c = EEPROM.read(PASS_ADDR + i);
    if (c == 0 || c == 0xFF) break;
    pass += c;
  }
  return pass;
}

// ============================================================
// EEPROM - RFID
// ============================================================
void saveUID(int index, byte *uid) {
  for (int i = 0; i < UID_SIZE; i++) EEPROM.write(index * UID_SIZE + i, uid[i]);
  EEPROM.commit(); delay(10);
}
void readUID(int index, byte *uid) {
  for (int i = 0; i < UID_SIZE; i++) uid[i] = EEPROM.read(index * UID_SIZE + i);
}
void deleteUID(int index) {
  for (int i = 0; i < UID_SIZE; i++) EEPROM.write(index * UID_SIZE + i, 0xFF);
  EEPROM.commit(); delay(10);
}
bool uidExists(byte *uid) {
  byte stored[UID_SIZE];
  for (int i = 0; i < MAX_CARDS; i++) {
    readUID(i, stored);
    bool match = true;
    for (int j = 0; j < UID_SIZE; j++) if (stored[j] != uid[j]) { match = false; break; }
    if (match) return true;
  }
  return false;
}
int findSlotByUID(byte *uid) {
  byte stored[UID_SIZE];
  for (int i = 0; i < MAX_CARDS; i++) {
    readUID(i, stored);
    bool match = true;
    for (int j = 0; j < UID_SIZE; j++) if (stored[j] != uid[j]) { match = false; break; }
    if (match) return i;
  }
  return -1;
}
int findEmptySlot() {
  byte stored[UID_SIZE];
  for (int i = 0; i < MAX_CARDS; i++) {
    readUID(i, stored);
    bool empty = true;
    for (int j = 0; j < UID_SIZE; j++) if (stored[j] != 0xFF) { empty = false; break; }
    if (empty) return i;
  }
  return -1;
}
void restoreDefaultRFID() {
  for (int i = 0; i < MAX_CARDS * UID_SIZE; i++) EEPROM.write(i, 0xFF);
  byte card1[UID_SIZE] = {0x98, 0xA1, 0x91, 0xE3};
  byte card2[UID_SIZE] = {0x74, 0xDC, 0x6A, 0x05};
  saveUID(0, card1); saveUID(1, card2);
  EEPROM.commit();
}

// ============================================================
// LCD - HIỂN THỊ MENU
// ============================================================
void showMenu() {
  lcd.clear(); lcd.setCursor(0,0);
  if (menuIndex % 2 == 0) { lcd.print("->"); lcd.print(menuItems[menuIndex]); }
  else                     { lcd.print("  "); lcd.print(menuItems[menuIndex-1]); }
  lcd.setCursor(0,1);
  if (menuIndex % 2 == 1) { lcd.print("->"); lcd.print(menuItems[menuIndex]); }
  else if (menuIndex + 1 < menuLength) { lcd.print("  "); lcd.print(menuItems[menuIndex+1]); }
}

void showCheckMenu() {
  lcd.clear(); lcd.setCursor(0,0); lcd.print("->"); lcd.print(checkItems[checkIndex]);
  lcd.setCursor(0,1);
  if (checkIndex + 1 < checkLength) { lcd.print("  "); lcd.print(checkItems[checkIndex+1]); }
  else lcd.print("                ");
}

void showRFIDMenu() {
  lcd.clear(); lcd.setCursor(0,0); lcd.print("->"); lcd.print(rfidItems[rfidIndex]);
  lcd.setCursor(0,1);
  if (rfidIndex + 1 < rfidLength) { lcd.print("  "); lcd.print(rfidItems[rfidIndex+1]); }
  else lcd.print("                ");
}

void showFPMenu() {
  lcd.clear(); lcd.setCursor(0,0); lcd.print("->"); lcd.print(fpItems[fpIndex]);
  lcd.setCursor(0,1);
  if (fpIndex + 1 < fpLength) { lcd.print("  "); lcd.print(fpItems[fpIndex+1]); }
  else lcd.print("                ");
}

void showResetMenu() {
  lcd.clear(); lcd.setCursor(0,0); lcd.print("->"); lcd.print(resetItems[resetIndex]);
  lcd.setCursor(0,1);
  if (resetIndex + 1 < resetLength) { lcd.print("  "); lcd.print(resetItems[resetIndex+1]); }
  else lcd.print("                ");
}

// ============================================================
// LCD - GIAO DIỆN NHẬP
// ============================================================
void startCheckPass()  { enteredPass=""; lcd.clear(); lcd.setCursor(0,0); lcd.print("Enter Pass:");    lcd.setCursor(0,1); }
void startMasterPass() { enteredPass=""; lcd.clear(); lcd.setCursor(0,0); lcd.print("Enter Master:");  lcd.setCursor(0,1); }
void startOldPass()    { enteredPass=""; lcd.clear(); lcd.setCursor(0,0); lcd.print("Old password:");  lcd.setCursor(0,1); }
void startNewPass1()   { enteredPass=""; lcd.clear(); lcd.setCursor(0,0); lcd.print("New password:");  lcd.setCursor(0,1); }
void startNewPass2()   { enteredPass=""; lcd.clear(); lcd.setCursor(0,0); lcd.print("Repeat new:");    lcd.setCursor(0,1); }

void printHiddenChar(char c) {
  lcd.print(c); delay(200);
  lcd.setCursor(enteredPass.length()-1, 1);
  lcd.print("*");
}

void deleteLastChar() {
  if (enteredPass.length() == 0) return;
  enteredPass.remove(enteredPass.length()-1);
  lcd.setCursor(0,1); lcd.print("                ");
  lcd.setCursor(0,1);
  for (size_t i = 0; i < enteredPass.length(); i++) lcd.print("*");
}

void displayUID(byte *uid) {
  lcd.setCursor(0,1); lcd.print("UID:");
  for (byte i = 0; i < UID_SIZE; i++) {
    if (uid[i] < 0x10) lcd.print("0");
    lcd.print(uid[i], HEX);
    if (i < UID_SIZE-1) lcd.print(" ");
  }
}

// ============================================================
// RELAY / BUZZER / KHÓA
// ============================================================
void openDoor() {
  doorOpen = true;
  wrongAttempts = 0; 
  Blynk.virtualWrite(V0, DOOR_OPEN);   // ← thêm

  lcd.clear(); lcd.setCursor(0,0); lcd.print("Thank you!");
  lcd.setCursor(0,1); lcd.print("Door Opened");
  Serial.println("Open success");
  digitalWrite(relayPin, HIGH);
  digitalWrite(buzzerPin, HIGH); delay(200);
  digitalWrite(buzzerPin, LOW);
  delay(5000);
  digitalWrite(relayPin, LOW);
  
  ignoreV1Once = true;
  doorOpen = false;
  Blynk.virtualWrite(V0, DOOR_CLOSE);  // ← thêm
  Blynk.virtualWrite(V1, 0);           // ← thêm

  currentState = MAIN_MENU;
  showMenu();
}

void webLog(String msg) {
  terminal.println(msg);
  terminal.flush();
  delay(20);
}

void lockSystem(String source = "LCD") {
  currentState = LOCKED;

  Blynk.virtualWrite(V1, 0);
  Blynk.virtualWrite(V2, "");
  ignoreV1Once = true;
  ignoreV2Once = true;

  webLog("🔒 Too many wrong attempts! (" + source + ") Locked 10s...");

  for (int i = 10; i > 0; i--) {
    lcd.clear(); lcd.setCursor(0,0); lcd.print("System Locked!");
    lcd.setCursor(0,1); lcd.print("Wait "); lcd.print(i); lcd.print("s");
    digitalWrite(buzzerPin, i > 8 ? HIGH : LOW);
    delay(1000);
  }
  digitalWrite(buzzerPin, LOW);
  wrongAttempts = 0;
  currentState = MAIN_MENU;

  Blynk.virtualWrite(V1, 0);
  Blynk.virtualWrite(V2, "");
  ignoreV1Once = true;
  ignoreV2Once = true;

  webLog("🔓 Unlocked! You can try again.");
  showMenu();
}

// ============================================================
// VÂN TAY
// ============================================================
bool fpIDExists(int id) {
  for (int i = 1; i <= FP_MAX_ID; i++)
    if (finger.loadModel(i) == FINGERPRINT_OK && i == id) return true;
  return false;
}

int enrollFingerprint(int id) {
  int p = -1;
  lcd.clear(); lcd.print("Place finger 1st");
  lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(id);
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    server.handleClient(); yield();   // ← THÊM
    if (p == FINGERPRINT_NOFINGER) continue;
    if (p != FINGERPRINT_OK) return p;
  }
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) return p;

  if (finger.fingerFastSearch() == FINGERPRINT_OK) {
    lcd.clear(); lcd.print("FP Exists!");
    lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(finger.fingerID);
    delay(2000); return -1;
  }

  lcd.clear(); lcd.print("Remove finger"); delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER) {
    server.handleClient(); yield();   // ← THÊM
  }

  lcd.clear(); lcd.print("Place finger 2nd");
  lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(id);
  p = -1;
  while (true) {
    p = finger.getImage();
    server.handleClient(); yield();   // ← THÊM
    if (p == FINGERPRINT_NOFINGER) continue;
    if (p != FINGERPRINT_OK) return p;
    p = finger.image2Tz(2);
    if (p != FINGERPRINT_OK) return p;
    p = finger.createModel();
    if (p != FINGERPRINT_OK) { lcd.clear(); lcd.print("Not same FP 1st!"); delay(1500); return -2; }
    break;
  }
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    lcd.clear(); lcd.print("FP Saved ID:"); lcd.print(id); delay(2000); return id;
  }
  return p;
}

void setupFPDefaults() {
  for (int id = FP_DEFAULT_1_ID; id <= FP_DEFAULT_2_ID; id++) {
    if (finger.loadModel(id) == FINGERPRINT_OK) {
      Serial.print("ID "); Serial.print(id); Serial.println(" already exists.");
      continue;
    }
    // Hiển thị thông báo trước khi enroll
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Enroll FP");
    lcd.setCursor(0,1);
    lcd.print("ID:");
    lcd.print(id);
    delay(1500);

    // Bắt buộc enroll đến khi thành công mới thoát
    while (enrollFingerprint(id) != id);
  }
}


// ===== FORWARD DECLARATIONS =====
void handleWebFPScan();
void handleWebRFID();
void handleWebFPEnroll();

void runBackground() {
  static unsigned long lastHandle = 0;
  if (millis() - lastHandle > 50) {
    server.handleClient();
    lastHandle = millis();
  }
  if (!apMode && WiFi.status() == WL_CONNECTED) {
    if (Blynk.connected()) Blynk.run();
    handleWebFPScan();
    handleWebRFID();
    handleWebFPEnroll();
  }
  yield();
}

void checkFingerprint() {
  currentState = CHECK_FP_LCD;

  lcd.clear(); lcd.setCursor(0,0); lcd.print("Place your FP");
  unsigned long startTime = millis();
  int p = FINGERPRINT_NOFINGER;
  const int timeout = 10;

  while (millis() - startTime < timeout * 1000UL) {
    p = finger.getImage();
    int left = timeout - (millis() - startTime) / 1000;
    lcd.setCursor(0,1); lcd.print("timeLeft: "); lcd.print(left); lcd.print("s   ");

    // ★ FIX: dùng runBackground() thay vì Blynk.run() + server.handleClient()
    runBackground();

    if (p == FINGERPRINT_NOFINGER) { delay(100); continue; }
    if (p != FINGERPRINT_OK) {
      lcd.setCursor(0,1); lcd.print("Error,try again ");
      delay(1000); lcd.setCursor(0,1); lcd.print("                ");
      startTime = millis(); continue;
    }
    break;
  }

  if (p != FINGERPRINT_OK) {
    lcd.clear(); lcd.print("No finger!");
    digitalWrite(buzzerPin, HIGH); delay(2000); digitalWrite(buzzerPin, LOW);
    currentState = CHECK_MENU; showCheckMenu(); return;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    lcd.clear(); lcd.print("Image Err"); delay(1000);
    currentState = CHECK_MENU; showCheckMenu(); return;
  }

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK && finger.fingerID >= 1 && finger.fingerID <= FP_MAX_ID) {
    lcd.clear(); lcd.print("Finger OK!");
    lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(finger.fingerID);
    delay(1500); openDoor(); return;
  }

  lcd.clear();
  lcd.print(p == FINGERPRINT_NOTFOUND ? "FP Not Found" : "FP Error");
  digitalWrite(buzzerPin, HIGH); delay(2000); digitalWrite(buzzerPin, LOW);
  currentState = CHECK_MENU; showCheckMenu();
}


void addFingerprint() {
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Add FP ID 1-5:");
  lcd.setCursor(0,1); lcd.print("ID: ");
  String inputID = "";

  while (true) {
    char k = keypad.getKey();
    // ★ FIX: dùng runBackground()
    runBackground();
    if (!k) continue;
    if (k >= '1' && k <= '5') {
      inputID = k; lcd.setCursor(4,1); lcd.print(inputID); delay(1000); break;
    }
    lcd.setCursor(0,1); lcd.print("Invalid ID! ");
    delay(1000); lcd.setCursor(0,1); lcd.print("ID:          ");
  }

  int id = inputID.toInt();
  if (fpIDExists(id)) {
    lcd.clear(); lcd.setCursor(0,0); lcd.print("ID "); lcd.print(id); lcd.print(" Exists!");
    delay(1500); return;
  }

  lcd.clear(); lcd.print("Place finger 1st");
  lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(id);
  int p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    // ★ FIX: dùng runBackground()
    runBackground();
    if (p == FINGERPRINT_NOFINGER) continue;
    if (p != FINGERPRINT_OK) {
      lcd.setCursor(0,1); lcd.print("Scan Err");
      delay(1000); lcd.setCursor(0,1); lcd.print("        "); p = -1;
    }
  }
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) { lcd.clear(); lcd.print("Error 1st scan"); delay(1500); return; }

  if (finger.fingerFastSearch() == FINGERPRINT_OK) {
    lcd.clear(); lcd.print("FP Exists!");
    lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(finger.fingerID);
    delay(2000); return;
  }

  lcd.clear(); lcd.print("Remove finger"); delay(1500);
  lcd.clear(); lcd.print("Place finger 2nd");
  lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(id);
  p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    // ★ FIX: dùng runBackground()
    runBackground();
    if (p == FINGERPRINT_NOFINGER) continue;
    if (p != FINGERPRINT_OK) {
      lcd.setCursor(0,1); lcd.print("Scan Err");
      delay(1000); lcd.setCursor(0,1); lcd.print("        "); p = -1;
    }
  }
  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) { lcd.clear(); lcd.print("Error 2nd scan"); delay(1500); return; }
  p = finger.createModel();
  if (p != FINGERPRINT_OK) { lcd.clear(); lcd.print("Not Same FP 1st!"); delay(1500); return; }
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) { lcd.clear(); lcd.print("Saved ID:"); lcd.setCursor(0,1); lcd.print(id); delay(2000); }
  else { lcd.clear(); lcd.print("Save Failed"); delay(1500); }
}


void deleteFingerprint() {
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Del FP ID 1-5:");
  lcd.setCursor(0,1); lcd.print("ID: ");
  String inputID = "";

  while (true) {
    char k = keypad.getKey();
    // ★ FIX: dùng runBackground()
    runBackground();
    if (!k) continue;
    if (k >= '1' && k <= '5') {
      inputID = k; lcd.setCursor(4,1); lcd.print(inputID); delay(1000); break;
    }
    lcd.setCursor(0,1); lcd.print("Invalid ID! ");
    delay(1000); lcd.setCursor(0,1); lcd.print("ID:          ");
  }

  int id = inputID.toInt();
  if (!fpIDExists(id)) {
    lcd.clear(); lcd.setCursor(0,0);
    lcd.print("ID "); lcd.print(id); lcd.print(" not exist!");
    delay(1500); return;
  }

  lcd.clear(); lcd.setCursor(0,0); lcd.print("Delete ID "); lcd.print(id);
  lcd.setCursor(0,1); lcd.print("#:Yes  *:No");
  while (true) {
    char k = keypad.getKey();
    // ★ FIX: dùng runBackground()
    runBackground();
    if (k == '#') {
      bool ok = finger.deleteModel(id) == FINGERPRINT_OK;
      lcd.clear(); lcd.print(ok ? "Deleted OK!" : "Delete Failed!");
      if (ok) { lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(id); }
      delay(2000); break;
    } else if (k == '*') {
      lcd.clear(); lcd.print("Delete canceled"); delay(1500); break;
    }
  }
  currentState = FP_MENU; showFPMenu();
}


void resetFingerprint() {
  lcd.clear();
  lcd.print("Resetting FP...");
  delay(1000);
  // Xóa các vân tay từ ID 3 → 5
  for (int id = 3; id <= 5; id++) {
    finger.deleteModel(id);
  }
  // Bắt buộc enroll ID1 và ID2
  for (int id = FP_DEFAULT_1_ID; id <= FP_DEFAULT_2_ID; id++) {
    if (fpIDExists(id)) continue;
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Enroll FP");
    lcd.setCursor(0,1);
    lcd.print("ID:");
    lcd.print(id);
    delay(1500);
    while (enrollFingerprint(id) != id);
  }
}


void openDoorRemote() {
  doorOpen = true;
  Blynk.virtualWrite(V0, DOOR_OPEN);
  lcd.clear(); lcd.setCursor(0,0); lcd.print("Thank you!");
  lcd.setCursor(0,1); lcd.print("Door Opened");
  digitalWrite(relayPin, HIGH);
  digitalWrite(buzzerPin, HIGH); delay(200);
  digitalWrite(buzzerPin, LOW);

  unsigned long t = millis();
  while (millis() - t < 5000) {
    // ★ FIX: dùng runBackground()
    runBackground(); delay(10);
  }

  digitalWrite(relayPin, LOW);
  doorOpen = false;
  Blynk.virtualWrite(V0, DOOR_CLOSE);
  Blynk.virtualWrite(V1, 0);
  currentState = MAIN_MENU;
  showMenu();
}

// Tạo hàm helper để tái sử dụng
void restoreStateAfterWebFP() {
  wrongAttempts = 0;
  enteredPass = "";
  if      (savedStateBeforeFP == MAIN_MENU)  { currentState = MAIN_MENU; showMenu(); }
  else if (savedStateBeforeFP == CHECK_MENU) { currentState = CHECK_MENU; showCheckMenu(); }
  else if (savedStateBeforeFP == RFID_MENU)  { currentState = RFID_MENU; showRFIDMenu(); }
  else if (savedStateBeforeFP == FP_MENU)    { currentState = FP_MENU; showFPMenu(); }
  else if (savedStateBeforeFP == RESET_MENU) { currentState = RESET_MENU; showResetMenu(); }
  else { currentState = MAIN_MENU; showMenu(); } // đang nhập master dở → về menu
}

void restoreStateAfterWebRFID() {
  // Các state menu con → trở về đúng menu đó
  if      (savedStateBeforeRFID == RFID_MENU)  { currentState = RFID_MENU;  showRFIDMenu(); }
  else if (savedStateBeforeRFID == FP_MENU)    { currentState = FP_MENU;    showFPMenu(); }
  else if (savedStateBeforeRFID == RESET_MENU) { currentState = RESET_MENU; showResetMenu(); }
  else if (savedStateBeforeRFID == CHECK_MENU) { currentState = CHECK_MENU; showCheckMenu(); }
  // Tất cả còn lại (MAIN_MENU, đang nhập master pass...) → về MAIN_MENU
  else { currentState = MAIN_MENU; showMenu(); }
}

void restoreStateAfterWebFPAdmin() {
  enteredPass = "";
  wrongAttempts = 0;
  if      (savedStateBeforeFPAdmin == RFID_MENU)  { currentState = RFID_MENU;  showRFIDMenu(); }
  else if (savedStateBeforeFPAdmin == FP_MENU)    { currentState = FP_MENU;    showFPMenu(); }
  else if (savedStateBeforeFPAdmin == RESET_MENU) { currentState = RESET_MENU; showResetMenu(); }
  else if (savedStateBeforeFPAdmin == CHECK_MENU) { currentState = CHECK_MENU; showCheckMenu(); }
  else { currentState = MAIN_MENU; showMenu(); }
}

void restoreStateAfterWebReset() {
  enteredPass = "";
  wrongAttempts = 0;
  if      (savedStateBeforeReset == RFID_MENU)  { currentState = RFID_MENU;  showRFIDMenu(); }
  else if (savedStateBeforeReset == FP_MENU)    { currentState = FP_MENU;    showFPMenu(); }
  else if (savedStateBeforeReset == RESET_MENU) { currentState = RESET_MENU; showResetMenu(); }
  else if (savedStateBeforeReset == CHECK_MENU) { currentState = CHECK_MENU; showCheckMenu(); }
  else { currentState = MAIN_MENU; showMenu(); }
}



void handleWebFPScan() {
  if (webState != WEB_SCAN_FP) return;

  unsigned long elapsed = millis() - webFPStart;

  // Hết 10s → timeout
  if (elapsed >= WEB_FP_TIMEOUT) {
    webState = WEB_IDLE;
    // Còi kêu 2s
    digitalWrite(buzzerPin, HIGH); delay(2000); digitalWrite(buzzerPin, LOW);
    lcd.clear(); lcd.setCursor(0,0); lcd.print("No finger!");
    lcd.setCursor(0,1); lcd.print("Timeout!");
    delay(1500);
    webLog("⏰ Timeout! No finger detected. Type CHECK-FP to retry.");
    restoreStateAfterWebFP();
    return;
  }

  // Cập nhật đếm ngược tại chỗ — ghi đè dòng cuối bằng virtualWrite trực tiếp
  static int lastLeft = -1;
  int left = (WEB_FP_TIMEOUT - elapsed) / 1000;
  if (left != lastLeft) {
    lastLeft = left;

    // Hiện lần đầu tiên — init LCD
    if (left == 9) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Web: Place FP");
    }
    // Cập nhật đếm ngược trên LCD dòng 2
    lcd.setCursor(0, 1);
    lcd.print("Time: ");
    lcd.print(left + 1);  // +1 vì left tính từ 9→0, hiện 10→1
    lcd.print("s          ");
    // Ghi lên Terminal
    terminal.println("👆 Scanning... " + String(left + 1) + "s");
    terminal.flush();
  }

  // Thử lấy ảnh
  int p = finger.getImage();
  if (p == FINGERPRINT_NOFINGER) return;

  if (p != FINGERPRINT_OK) {
    // Còi kêu 2s khi lỗi scan
    digitalWrite(buzzerPin, HIGH); delay(2000); digitalWrite(buzzerPin, LOW);
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Scan Error!");
    lcd.setCursor(0,1); lcd.print("Try again");
    delay(1500);
    webState = WEB_IDLE;
    lastLeft = -1;
    webLog("⚠️ Scan error! Type CHECK-FP to retry.");
    restoreStateAfterWebFP();
    return;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    digitalWrite(buzzerPin, HIGH); delay(2000); digitalWrite(buzzerPin, LOW);
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Image Error!");
    lcd.setCursor(0,1); lcd.print("Try again");
    delay(1500);
    webState = WEB_IDLE;
    lastLeft = -1;
    webLog("⚠️ Image error! Type CHECK-FP to retry.");
    restoreStateAfterWebFP();
    return;
  }

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK && finger.fingerID >= 1 && finger.fingerID <= FP_MAX_ID) {
    webState = WEB_IDLE;
    lastLeft = -1;
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Finger OK!");
    lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(finger.fingerID);
    delay(1500);
    webLog("✅ Finger OK! ID:" + String(finger.fingerID));
    webLog("🚪 Opening door...");
    openDoor();
    webLog("⚫ Door closed...");
  } else {
    // Vân tay không khớp → còi kêu 2s
    digitalWrite(buzzerPin, HIGH); delay(2000); digitalWrite(buzzerPin, LOW);
    lcd.clear(); lcd.setCursor(0,0); lcd.print("FP Not Found!");
    lcd.setCursor(0,1); lcd.print("Try again");
    delay(1500);
    webState = WEB_IDLE;
    lastLeft = -1;
    webLog("❌ Finger not found! Type CHECK-FP to retry.");
    restoreStateAfterWebFP(); 
  }
}

bool isAdminSessionValid() {
  if (!adminSession) return false;
  if (millis() - adminSessionStart > ADMIN_SESSION_TIMEOUT) {
    adminSession = false;
    adminState   = ADMIN_IDLE;
    Blynk.virtualWrite(V4, "No session");  // ← thêm dòng này
    webLog("⏰ Admin session expired. Type ADMIN to login again.");
    return false;
  }
  return true;
}

void updateAdminCountdown() {
  if (adminSession) {
    unsigned long elapsed = millis() - adminSessionStart;
    if (elapsed >= ADMIN_SESSION_TIMEOUT) {
      adminCountdown = 0;
      Blynk.virtualWrite(V4, "Session expired");
    } else {
      int remaining = (ADMIN_SESSION_TIMEOUT - elapsed) / 1000;
      int m = remaining / 60;
      int s = remaining % 60;
      char buf[20];
      sprintf(buf, "Time: %d:%02d", m, s);
      Blynk.virtualWrite(V4, buf);
    }
  } else {
    Blynk.virtualWrite(V4, "No session");
  }
}

// ---- Kiểm tra có đang bận không ----
bool isBusy() {
  // Bận vì Web
  if (webState == WEB_WAIT_KP_PASS)        return true;
  if (webState == WEB_SCAN_FP)             return true;
  if (adminState == ADMIN_CHANGE_PASS_OLD) return true;
  if (adminState == ADMIN_CHANGE_PASS_NEW1)return true;
  if (adminState == ADMIN_CHANGE_PASS_NEW2)return true;
  if (adminState == ADMIN_ADD_RFID) return true;
  if (adminState == ADMIN_DEL_RFID) return true;
  if (adminState == ADMIN_ADD_FP)         return true;
  if (adminState == ADMIN_DEL_FP)         return true;
  if (adminState == ADMIN_DEL_FP_CONFIRM) return true;
  if (adminState == ADMIN_RESET_KP_CONFIRM)   return true;
  if (adminState == ADMIN_RESET_RFID_CONFIRM) return true;
  if (adminState == ADMIN_RESET_FP_CONFIRM)   return true;
  if (adminState == ADMIN_RESET_ALL_CONFIRM)  return true;
  if (adminState == ADMIN_VIEW_KP)   return true;
  if (adminState == ADMIN_VIEW_RFID) return true;
  if (adminState == ADMIN_VIEW_FP)   return true;

  if (webEnrollingFP) return true;  // ← THÊM

  // Bận vì LCD/Keypad
  if (currentState == CHECK_PASS)  return true;
  if (currentState == CHECK_RFID)  return true;
  if (currentState == CHECK_FP_LCD) return true;
  if (currentState == CHANGE_OLD)  return true;
  if (currentState == CHANGE_NEW1) return true;
  if (currentState == CHANGE_NEW2) return true;
  if (currentState == RFID_MENU) return true;  // ← THÊM LẠI
  if (currentState == RFID_ADD_LCD) return true;
  if (currentState == RFID_DEL_LCD) return true;
  if (currentState == FP_MENU)     return true;
  if (currentState == FP_ADD_LCD) return true;  // ← THÊM
  if (currentState == FP_DEL_LCD) return true;  // ← THÊM
  if (currentState == RESET_MENU)  return true;
  if (currentState == RESET_KP_LCD)   return true;
  if (currentState == RESET_RFID_LCD) return true;
  if (currentState == RESET_FP_LCD)   return true;
  if (currentState == RESET_ALL_LCD)  return true;

  return false;
}

bool isWebBusy() {
  return (webState == WEB_WAIT_KP_PASS ||
          webState == WEB_SCAN_FP ||
          adminState == ADMIN_CHANGE_PASS_OLD ||
          adminState == ADMIN_CHANGE_PASS_NEW1 ||
          adminState == ADMIN_CHANGE_PASS_NEW2 ||
          adminState == ADMIN_ADD_RFID || 
          adminState == ADMIN_ADD_FP ||
          adminState == ADMIN_DEL_FP ||
          adminState == ADMIN_DEL_FP_CONFIRM || // ← thêm
          adminState == ADMIN_RESET_KP_CONFIRM   ||
          adminState == ADMIN_RESET_RFID_CONFIRM ||
          adminState == ADMIN_RESET_FP_CONFIRM   ||
          adminState == ADMIN_RESET_ALL_CONFIRM  ||
          adminState == ADMIN_VIEW_KP   ||
          adminState == ADMIN_VIEW_RFID ||
          adminState == ADMIN_VIEW_FP   ||
          adminState == ADMIN_DEL_RFID);           // ← thêm
}

String getBusyReason() {
  // Bận vì Web
  if (webState == WEB_WAIT_KP_PASS)        return "WEB is busy with check keypad password!";
  if (webState == WEB_SCAN_FP)             return "WEB is busy with check FP!";
  if (adminState == ADMIN_CHANGE_PASS_OLD) return "WEB is busy with verify old password!";
  if (adminState == ADMIN_CHANGE_PASS_NEW1)return "WEB is busy with set new password (1st)!";
  if (adminState == ADMIN_CHANGE_PASS_NEW2)return "WEB is busy with confirm new password (2nd)!";
  if (adminState == ADMIN_ADD_RFID) return "WEB is busy with add RFID card!";
  if (adminState == ADMIN_DEL_RFID) return "WEB is busy with delete RFID card!";
  if (adminState == ADMIN_ADD_FP)         return "WEB is busy with add FP!";
  if (adminState == ADMIN_DEL_FP)         return "WEB is busy with delete FP!";
  if (adminState == ADMIN_DEL_FP_CONFIRM) return "WEB is busy with confirm delete FP!";
  if (adminState == ADMIN_RESET_KP_CONFIRM)   return "WEB is busy with reset keypad password!";
  if (adminState == ADMIN_RESET_RFID_CONFIRM) return "WEB is busy with reset RFID!";
  if (adminState == ADMIN_RESET_FP_CONFIRM)   return "WEB is busy with reset FP!";
  if (adminState == ADMIN_RESET_ALL_CONFIRM)  return "WEB is busy with reset ALL!";
  if (adminState == ADMIN_VIEW_KP)   return "WEB is busy with view keypad password!";
  if (adminState == ADMIN_VIEW_RFID) return "WEB is busy with view RFID!";
  if (adminState == ADMIN_VIEW_FP)   return "WEB is busy with view FP!";

  if (webEnrollingFP) return "WEB is busy with enrolling FP!";  // ← THÊM

  // Bận vì LCD/Keypad
  if (currentState == CHECK_PASS)  return "LCD is busy with check keypad password!";
  if (currentState == CHECK_RFID)  return "LCD is busy with check RFID!";
  if (currentState == CHANGE_OLD)  return "LCD is busy with verify old password!";
  if (currentState == CHECK_FP_LCD)  return "LCD is busy with check FP!";  // ← thêm
  if (currentState == CHANGE_NEW1) return "LCD is busy with set new password (1st)!";
  if (currentState == CHANGE_NEW2) return "LCD is busy with confirm new password (2nd)!";
  if (currentState == RFID_MENU) return "LCD is busy with RFID menu!";
  if (currentState == RFID_ADD_LCD) return "LCD is busy with add RFID!";
  if (currentState == RFID_DEL_LCD) return "LCD is busy with del RFID!";
  if (currentState == FP_MENU)     return "LCD is busy with FP menu!";
  if (currentState == FP_ADD_LCD) return "LCD is busy with add FP!";  // ← THÊM
  if (currentState == FP_DEL_LCD) return "LCD is busy with del FP!";  // ← THÊM
  if (currentState == RESET_MENU)  return "LCD is busy with RESET DATA menu";
  if (currentState == RESET_KP_LCD)   return "LCD is busy with reset keypad password!";
  if (currentState == RESET_RFID_LCD) return "LCD is busy with reset RFID!";
  if (currentState == RESET_FP_LCD)   return "LCD is busy with reset FP!";
  if (currentState == RESET_ALL_LCD)  return "LCD is busy with reset ALL!";

  return "System busy";
}

void handleWebRFID() {
  if (adminState != ADMIN_ADD_RFID && adminState != ADMIN_DEL_RFID) return;

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  byte *uid = mfrc522.uid.uidByte;

  // Format UID thành string
  String uidStr = "";
  for (byte i = 0; i < UID_SIZE; i++) {
    if (uid[i] < 0x10) uidStr += "0";
    uidStr += String(uid[i], HEX);
    if (i < UID_SIZE - 1) uidStr += " ";
  }
  uidStr.toUpperCase();

  if (adminState == ADMIN_ADD_RFID) {
    if (uidExists(uid)) {
      // Thất bại: thẻ đã tồn tại
      webLog("⚠️ Card already exists! UID: " + uidStr);
      lcd.clear();
      lcd.setCursor(0,0); lcd.print("Card Exists!");
      lcd.setCursor(0,1); lcd.print(uidStr.substring(0,16));
      delay(2000);
    } else {
      int slot = findEmptySlot();
      if (slot == -1) {
        // Thất bại: hết slot
        webLog("❌ Memory full! Cannot add more cards.");
        lcd.clear();
        lcd.setCursor(0,0); lcd.print("Memory Full!");
        lcd.setCursor(0,1); lcd.print("Cannot add card");
        delay(2000);
      } else {
        // Thành công
        saveUID(slot, uid);
        webLog("✅ Card added! UID: " + uidStr);
        lcd.clear();
        lcd.setCursor(0,0); lcd.print("Web: Added OK!");
        lcd.setCursor(0,1); lcd.print(uidStr.substring(0,16));
        delay(2000);
      }
    }
  } else if (adminState == ADMIN_DEL_RFID) {
    int slot = findSlotByUID(uid);
    if (slot == -1) {
      // Thất bại: thẻ không tồn tại
      webLog("⚠️ Card not found! UID: " + uidStr);
      lcd.clear();
      lcd.setCursor(0,0); lcd.print("Card Not Found!");
      lcd.setCursor(0,1); lcd.print(uidStr.substring(0,16));
      delay(2000);   // ← THÊM DÒNG NÀY
    } else {
      // Thành công
      deleteUID(slot);
      webLog("✅ Card deleted! UID: " + uidStr);
      lcd.clear();
      lcd.setCursor(0,0); lcd.print("Web: Deleted OK!");
      lcd.setCursor(0,1); lcd.print(uidStr.substring(0,16));
      delay(2000);
    }
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(5);

  // Sau khi ADD/DEL (dù thành công hay thất bại) → về menu chính
  adminState = ADMIN_IDLE;
  restoreStateAfterWebRFID();
}

void handleWebFPEnroll() {
  if (adminState != ADMIN_ADD_FP) return;
  if (webFPStep == 0) return; // chờ nhập ID trên terminal trước

  int p = finger.getImage();

  if (webFPStep == 1) {
    // ── Bước 1: quét lần 1 ──
    if (p == FINGERPRINT_NOFINGER) return;

    if (p != FINGERPRINT_OK) {
      webLog("⚠️ Scan error! Try again:");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Scan Error!");
      lcd.setCursor(0,1); lcd.print("Try again");
      delay(1500);
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Web:Place FP 1st");
      return;
    }

    p = finger.image2Tz(1);
    if (p != FINGERPRINT_OK) {
      webLog("⚠️ Image error! Try again:");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Image Error!");
      lcd.setCursor(0,1); lcd.print("Try again");
      delay(1500);
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Web:Place FP 1st");
      return;
    }

    // Kiểm tra FP đã tồn tại chưa
    if (finger.fingerFastSearch() == FINGERPRINT_OK) {
      webLog("⚠️ Finger already exists! ID:" + String(finger.fingerID));
      lcd.clear(); lcd.setCursor(0,0); lcd.print("FP Exists!");
      lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(finger.fingerID);
      delay(2000);
      adminState = ADMIN_IDLE; webFPStep = 0; webFPID = 0;
      restoreStateAfterWebFPAdmin();
      return;
    }

    // Lần 1 OK → yêu cầu rút tay
    webFPStep = 2;
    webLog("✅ 1st scan OK! Remove finger...");
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Remove finger");
    delay(2000);
    // Chờ rút tay
    while (finger.getImage() != FINGERPRINT_NOFINGER) { delay(100); }
    webLog("👆 Place same finger again (2nd scan):");
    lcd.clear(); lcd.setCursor(0,0); lcd.print("Web:Place FP 2nd");

  } else if (webFPStep == 2) {
    // ── Bước 2: quét lần 2 ──
    if (p == FINGERPRINT_NOFINGER) return;

    if (p != FINGERPRINT_OK) {
      webLog("⚠️ Scan error! Try again:");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Scan Error!");
      lcd.setCursor(0,1); lcd.print("Try again");
      delay(1500);
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Web:Place FP 2nd");
      return;
    }

    p = finger.image2Tz(2);
    if (p != FINGERPRINT_OK) {
      webLog("⚠️ Image error! Try again:");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Image Error!");
      lcd.setCursor(0,1); lcd.print("Try again");
      delay(1500);
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Web:Place FP 2nd");
      return;
    }

    p = finger.createModel();
    if (p != FINGERPRINT_OK) {
      webLog("❌ Fingers do not match! Add FP cancelled.");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Not Same FP 1st!");
      lcd.setCursor(0,1); lcd.print("Cancelled!");
      delay(2000);
      adminState = ADMIN_IDLE; webFPStep = 0; webFPID = 0;
      restoreStateAfterWebFPAdmin();
      return;
    }

    p = finger.storeModel(webFPID);
    if (p == FINGERPRINT_OK) {
      webLog("✅ Fingerprint saved! ID:" + String(webFPID));
      lcd.clear(); lcd.setCursor(0,0); lcd.print("FP Saved OK!");
      lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(webFPID);
      delay(2000);
    } else {
      webLog("❌ Save failed! Error:" + String(p));
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Save Failed!");
      lcd.setCursor(0,1); lcd.print("Error:"); lcd.print(p);
      delay(2000);
    }

    adminState = ADMIN_IDLE; webFPStep = 0; webFPID = 0;
    restoreStateAfterWebFPAdmin();
  }
}

// WEB RESET FP
// ============================================================
void handleWebResetFP(bool resetAll) {
  for (int id = 3; id <= 5; id++) finger.deleteModel(id);

  for (int id = FP_DEFAULT_1_ID; id <= FP_DEFAULT_2_ID; id++) {
    if (finger.loadModel(id) == FINGERPRINT_OK) continue;

    webEnrollingFP = true;
    webLog("⚠️ FP ID " + String(id) + " missing! Please scan to re-enroll...");

    bool enrolled = false;
    while (!enrolled) {
      webLog("👆 Place finger " + String(id) + " (1st scan):");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Web:Place FP 1st");
      lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(id);

      int p = -1;
      while (p != FINGERPRINT_OK) {
        p = finger.getImage();
        // ★ FIX: dùng runBackground()
        runBackground();
        if (p == FINGERPRINT_NOFINGER) { delay(100); continue; }
        if (p != FINGERPRINT_OK) { p = -1; }
      }
      p = finger.image2Tz(1);
      if (p != FINGERPRINT_OK) { webLog("⚠️ Image error, try again..."); continue; }

      uint8_t searchResult = finger.fingerSearch();
      if (searchResult == FINGERPRINT_OK) {
        webLog("⚠️ Finger already exists! ID:" + String(finger.fingerID));
        lcd.clear(); lcd.setCursor(0,0); lcd.print("FP Exists!");
        lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(finger.fingerID);
        delay(2000);
        lcd.clear(); lcd.setCursor(0,0); lcd.print("Web:Place FP 1st");
        lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(id);
        continue;
      }

      webLog("✅ 1st scan OK! Remove finger...");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Remove finger");
      delay(1500);
      while (finger.getImage() != FINGERPRINT_NOFINGER) { delay(100); }

      webLog("👆 Place same finger (2nd scan):");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Web:Place FP 2nd");
      lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(id);

      p = -1;
      while (p != FINGERPRINT_OK) {
        p = finger.getImage();
        // ★ FIX: dùng runBackground()
        runBackground();
        if (p == FINGERPRINT_NOFINGER) { delay(100); continue; }
        if (p != FINGERPRINT_OK) { p = -1; }
      }
      p = finger.image2Tz(2);
      if (p != FINGERPRINT_OK) { webLog("⚠️ Image error, try again from 1st scan..."); continue; }

      p = finger.createModel();
      if (p != FINGERPRINT_OK) {
        webLog("❌ Not same FP 1st! Try again from 1st scan...");
        lcd.clear(); lcd.setCursor(0,0); lcd.print("Not Same FP 1st!");
        delay(2000);
        lcd.clear(); lcd.setCursor(0,0); lcd.print("Web:Place FP 1st");
        lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(id);
        continue;
      }
      p = finger.storeModel(id);
      if (p == FINGERPRINT_OK) {
        webLog("✅ FP ID " + String(id) + " enrolled!");
        lcd.clear(); lcd.setCursor(0,0); lcd.print("FP Saved OK!");
        lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(id);
        delay(1500);
        enrolled = true;
      } else {
        webLog("⚠️ Save failed, try again...");
      }
    }
  }

  ignoreV1Once = true;
  Blynk.virtualWrite(V1, 0);
  delay(100);
  webEnrollingFP = false;

  if (resetAll) {
    webLog("✅ Reset all done!");
  } else {
    webLog("✅ FP reset done!");
  }
  restoreStateAfterWebReset();
}



void doViewKP() {
  webLog("🔑 Current keypad password: " + masterPass);
  adminState = ADMIN_IDLE;
}

void doViewRFID() {
  int count = 0;
  byte stored[UID_SIZE];
  String cards[MAX_CARDS];

  for (int i = 0; i < MAX_CARDS; i++) {
    readUID(i, stored);
    bool empty = true;
    for (int j = 0; j < UID_SIZE; j++) if (stored[j] != 0xFF) { empty = false; break; }
    if (empty) { cards[i] = ""; continue; }

    count++;
    String uidStr = "";
    for (int j = 0; j < UID_SIZE; j++) {
      if (stored[j] < 0x10) uidStr += "0";
      uidStr += String(stored[j], HEX);
      if (j < UID_SIZE - 1) uidStr += "-";
    }
    uidStr.toUpperCase();
    cards[i] = "Card " + String(count) + ": " + uidStr;
  }

  terminal.println("📋 Stored RFID: " + String(count) + "/" + String(MAX_CARDS));
  terminal.flush(); delay(150);

  for (int i = 0; i < MAX_CARDS; i++) {
    if (cards[i].length() == 0) continue;
    terminal.println("  " + cards[i]);
    terminal.flush(); delay(150);
  }

  adminState = ADMIN_IDLE;
}

void doViewFP() {
  int count = 0;
  String idList = "";
  for (int id = 1; id <= FP_MAX_ID; id++) {
    if (finger.loadModel(id) == FINGERPRINT_OK) {
      count++;
      if (idList.length() > 0) idList += ", ";
      idList += "ID " + String(id);
    }
  }
  webLog("📋 Stored FP: " + String(count) + "/" + String(FP_MAX_ID));
  if (count > 0) webLog("  " + idList);
  else webLog("  (none)");
  adminState = ADMIN_IDLE;
}

bool isWebChangingPass() {
  return (adminState == ADMIN_CHANGE_PASS_OLD ||
          adminState == ADMIN_CHANGE_PASS_NEW1 ||
          adminState == ADMIN_CHANGE_PASS_NEW2);
}

bool isWebResetting() {
  return (adminState == ADMIN_RESET_KP_CONFIRM  ||
          adminState == ADMIN_RESET_RFID_CONFIRM ||
          adminState == ADMIN_RESET_FP_CONFIRM   ||
          adminState == ADMIN_RESET_ALL_CONFIRM  ||
          webEnrollingFP);
}

// ← THÊM hàm này trước BLYNK_WRITE(V2)
bool handleGlobalCommands(String input) {
  if (adminState == ADMIN_WAIT_MASTER) return false;

  // Chỉ xử lý các lệnh global
  if (input != "VIEW-KP"  && input != "VIEW-RFID" && input != "VIEW-FP" &&
      input != "MENU"     && input != "LOGOUT"     && input != "ADMIN") {
    return false;
  }

  // ← Lưu state hiện tại trước khi xử lý
  AdminState savedAdminState = adminState;
  int savedWebFPStep = webFPStep;

  if (input == "VIEW-KP") {
    if (isAdminSessionValid()) {
      doViewKP();
      // ← Khôi phục
      adminState = savedAdminState;
      webFPStep  = savedWebFPStep;
    } else {
      pendingCmd = "VIEW-KP";
      adminState = ADMIN_WAIT_MASTER;
      adminWrongAttempts = 0;
      webLog("🔐 Admin required. Enter master password:");
    }

  } else if (input == "VIEW-RFID") {
    if (isAdminSessionValid()) {
      doViewRFID();
      adminState = savedAdminState;
      webFPStep  = savedWebFPStep;
    } else {
      pendingCmd = "VIEW-RFID";
      adminState = ADMIN_WAIT_MASTER;
      adminWrongAttempts = 0;
      webLog("🔐 Admin required. Enter master password:");
    }

  } else if (input == "VIEW-FP") {
    if (isAdminSessionValid()) {
      doViewFP();
      adminState = savedAdminState;
      webFPStep  = savedWebFPStep;
    } else {
      pendingCmd = "VIEW-FP";
      adminState = ADMIN_WAIT_MASTER;
      adminWrongAttempts = 0;
      webLog("🔐 Admin required. Enter master password:");
    }

  } else if (input == "MENU") {
    // MENU chỉ in ra terminal, không đụng state
    webLog("📋 COMMAND LIST: 16 commands"); delay(100);
    webLog("🔑 CHECK-KP | CHECK-FP | CHANGE-PASS | ADMIN | LOGOUT"); delay(100);
    webLog("📡 ADD-RFID | DEL-RFID | ADD-FP | DEL-FP"); delay(100);
    webLog("👁 VIEW-KP | VIEW-RFID | VIEW-FP"); delay(100);
    webLog("🔄 RESET-KP | RESET-RFID | RESET-FP | RESET-ALL"); delay(100);
    webLog("💡 Type MENU to show this list");
    // ← Không cần khôi phục vì không thay đổi gì

  } else if (input == "LOGOUT") {
    // ← Check TRƯỚC khi reset
  bool wasUsingLCD = (adminState == ADMIN_ADD_RFID || adminState == ADMIN_DEL_RFID || adminState == ADMIN_ADD_FP);
  adminSession  = false;
  adminState    = ADMIN_IDLE;
  webFPStep     = 0;
  webFPID       = 0;
  pendingCmd    = "";
  webNewPass1   = "";
  webNewPassWrong = 0;
  adminWrongAttempts = 0;
  Blynk.virtualWrite(V4, "No session");
  webLog("👋 Logged out.");
  // LCD về menu chính nếu web đang chiếm LCD
  // ← Dùng biến đã lưu trước đó
  if (wasUsingLCD) {
    currentState = MAIN_MENU;
    showMenu(); 
   }
  } else if (input == "ADMIN") {
    if (isAdminSessionValid()) {
      webLog("✅ Already logged in! Session active.");
      // ← Không thay đổi state, không cần khôi phục
    } else {
      adminState = ADMIN_WAIT_MASTER;
      adminWrongAttempts = 0;
      webLog("🔐 Enter master password:");
      // ← Không khôi phục vì đang chờ nhập master pass
    }
  }

  return true;
}

void startAdminSession() {
  adminSession      = true;
  adminSessionStart = millis();
  adminState        = ADMIN_IDLE;
  adminWrongAttempts = 0;
  Blynk.virtualWrite(V4, "Time: 5:00"); // hiện ngay không chờ 1s
}


BLYNK_WRITE(V1) {
  int val = param.asInt();
  if (val == 1) {
    if (ignoreV1Once) {
      ignoreV1Once = false;
      Blynk.virtualWrite(V1, 0);
      return;
    }
    if (currentState == LOCKED) {  // ← THÊM
      Blynk.virtualWrite(V1, 0);
      return;  // im lặng
    }
    if (doorOpen) { Blynk.virtualWrite(V1, 0); return; }
    if (isBusy()) {
      Blynk.virtualWrite(V1, 0);
      webLog("→ " + getBusyReason());
      webLog("⏳ Please wait until current operation is done.");
      return;
    }
    openDoorRemote();
  }
}

BLYNK_WRITE(V2) {
  String input = param.asStr();
  input.trim();
  if (input.length() == 0) return;
  Blynk.virtualWrite(V2, "");
  // ← THÊM
  if (ignoreV2Once) {
    ignoreV2Once = false;
    return;
  }

  if (currentState == LOCKED) return;  // im lặng, không làm gì
  // ← THÊM: 1 dòng duy nhất
  if (handleGlobalCommands(input)) return;
  
  // Giữ lại, nhưng thêm exception cho ADMIN_WAIT_MASTER
  if (webState == WEB_SCAN_FP && input != "CANCEL") {
    if (adminState == ADMIN_WAIT_MASTER) {
    // cho qua — đang nhập master pass để VIEW
    } else {
     webLog("⏳ WEB is busy with CHECK-FP! Type CANCEL to stop.");
     return;
    }
  }

  
  // ← THÊM: chặn input khi LCD đang scan FP
  if (currentState == CHECK_FP_LCD) {
  if (adminState == ADMIN_WAIT_MASTER) { // ← THÊM: cho phép nhập master pass khi đang chờ session cho VIEW
  } else {
    webLog("⏳ LCD is busy with CHECK-FP! Please wait.");
    return;
     }
  }

  if (currentState == CHANGE_OLD || currentState == CHANGE_NEW1 || currentState == CHANGE_NEW2) {
   if (adminState == ADMIN_WAIT_MASTER) { // ← THÊM: cho phép nhập master pass khi đang chờ session cho VIEW
  } else {
    webLog("⏳ LCD is busy with CHANGE-PASS! Please wait.");
    return;
  }
}

  // ← THÊM
if (currentState == RFID_ADD_LCD || currentState == RFID_DEL_LCD) {
 if (adminState == ADMIN_WAIT_MASTER) { // cho nhập master pass để VIEW
  } else {
    webLog("⏳ LCD is busy with ADD/DEL RFID! Please wait.");
    return;
  }
}

// ← THÊM
if (currentState == FP_ADD_LCD || currentState == FP_DEL_LCD) {
  if (adminState == ADMIN_WAIT_MASTER) { // cho nhập master pass để VIEW
  } else {
    webLog("⏳ LCD is busy with ADD/DEL FP! Please wait.");
    return;
  }
}

if (currentState == RESET_KP_LCD  || currentState == RESET_RFID_LCD ||
    currentState == RESET_FP_LCD  || currentState == RESET_ALL_LCD) {
  if (adminState == ADMIN_WAIT_MASTER) {
    // cho qua — đang nhập master pass để VIEW
  } else {
    webLog("⏳ LCD is busy with RESET DATA! Please wait.");
    return;
  }
}

// ← THÊM: chặn input web khi đang ADD/DEL FP từ web
if (adminState == ADMIN_ADD_FP || adminState == ADMIN_DEL_FP || 
    adminState == ADMIN_DEL_FP_CONFIRM) {
  // Cho phép CANCEL để hủy
  if (input == "CANCEL") {
    // cho qua xử lý bình thường bên dưới
  }
  // Cho phép VIEW/ADMIN/LOGOUT/MENU
  else if (input == "VIEW-KP"  || input == "VIEW-RFID" || input == "VIEW-FP" ||
           input == "ADMIN"    || input == "LOGOUT"     || input == "MENU") {
    // cho qua
  }
  // Cho phép nhập master pass khi đang chờ session cho VIEW
  else if (adminState == ADMIN_WAIT_MASTER) {
    // cho qua
  }
  // Cho phép nhập ID (1-5) khi đang ADD/DEL FP
  else if (adminState == ADMIN_ADD_FP && webFPStep == 0) {
    // cho qua — đang chờ nhập ID
  }
  else if (adminState == ADMIN_DEL_FP) {
    // cho qua — đang chờ nhập ID
  }
  else if (adminState == ADMIN_DEL_FP_CONFIRM) {
    // cho qua — đang chờ YES/NO
  }
  else {
    webLog("⏳ WEB is busy with ADD/DEL FP! Please wait.");
    return;
  }
}

  // ADMIN STATE MACHINE

  // ── Chờ nhập master pass để mở session ──
  if (adminState == ADMIN_WAIT_MASTER) {
    if (input == adminPass) {
      adminWrongAttempts = 0;
      startAdminSession();
      webLog("✅ Admin session started! Valid for 5 minutes.");
      // Nếu có lệnh pending thì thực thi luôn
      if (pendingCmd == "CHANGE-PASS") {
        pendingCmd = "";
        adminState = ADMIN_CHANGE_PASS_OLD;
        webLog("🔑 Enter old password:");
      }
    else if (pendingCmd == "ADD-RFID") {   // ← thêm
     savedStateBeforeRFID = currentState;
     pendingCmd = "";
     adminState = ADMIN_ADD_RFID;
     webLog("📡 Please scan card to ADD...");
     webLog("Type CANCEL to stop.");
     lcd.clear();
     lcd.setCursor(0,0); lcd.print("Web: Add Card");
    } else if (pendingCmd == "DEL-RFID") {   // ← thêm
     savedStateBeforeRFID = currentState;
     pendingCmd = "";
     adminState = ADMIN_DEL_RFID;
     webLog("📡 Please scan card to DELETE...");
     webLog("Type CANCEL to stop.");
     lcd.clear();
     lcd.setCursor(0,0); lcd.print("Web: Dele Card");

    } else if (pendingCmd == "ADD-FP") {
      savedStateBeforeFPAdmin = currentState;
      pendingCmd = "";
      adminState = ADMIN_ADD_FP;
      webFPStep  = 0;
      webLog("🖐 Enter FP ID to add (1-5):");
      //lcd.clear(); lcd.setCursor(0,0); lcd.print("Web:Add FP");
      //lcd.setCursor(0,1); lcd.print("Enter ID 1-5");
    } else if (pendingCmd == "DEL-FP") {
      savedStateBeforeFPAdmin = currentState;
      pendingCmd = "";
      adminState = ADMIN_DEL_FP;
      webFPStep  = 0;
      webLog("🖐 Enter FP ID to delete (1-5):");
      } else if (pendingCmd == "RESET-KP") {
        savedStateBeforeReset = currentState;
        pendingCmd = ""; adminState = ADMIN_RESET_KP_CONFIRM;
        webLog("⚠️ Reset keypad password? Type YES or NO:");
      } else if (pendingCmd == "RESET-RFID") {
        savedStateBeforeReset = currentState;
        pendingCmd = ""; adminState = ADMIN_RESET_RFID_CONFIRM;
        webLog("⚠️ Reset RFID? Type YES or NO:");
      } else if (pendingCmd == "RESET-FP") {
        savedStateBeforeReset = currentState;
        pendingCmd = ""; adminState = ADMIN_RESET_FP_CONFIRM;
        webLog("⚠️ Reset FP? Type YES or NO:");
      } else if (pendingCmd == "RESET-ALL") {
        savedStateBeforeReset = currentState;
        pendingCmd = ""; adminState = ADMIN_RESET_ALL_CONFIRM;
        webLog("⚠️ Reset ALL (KP+RFID+FP)? Type YES or NO:");

      } else if (pendingCmd == "VIEW-KP") {
        pendingCmd = ""; adminState = ADMIN_IDLE;
        doViewKP();
      } else if (pendingCmd == "VIEW-RFID") {
        pendingCmd = ""; adminState = ADMIN_IDLE;
        doViewRFID();
      } else if (pendingCmd == "VIEW-FP") {
        pendingCmd = ""; adminState = ADMIN_IDLE;
        doViewFP();
      }

    } else {
      adminWrongAttempts++;
      if (adminWrongAttempts >= 3) {
        adminState = ADMIN_IDLE;
        adminWrongAttempts = 0;
        lockSystem("WEB");
      } else {
        webLog("❌ Wrong master pass! " + String(adminWrongAttempts) + "/3. Try again:");
      }
    }
    return;
  }

  // ── CHANGE-PASS sub-states ──
  if (adminState == ADMIN_CHANGE_PASS_OLD) {
    if (input == masterPass) {
      adminState = ADMIN_CHANGE_PASS_NEW1;
      webLog("✅ Old password correct!");
      webLog("🔑 Enter new password:");
    } else {
      adminWrongAttempts++;
      if (adminWrongAttempts >= 3) {
        adminState = ADMIN_IDLE;
        adminWrongAttempts = 0;
        lockSystem("WEB");
      } else {
        webLog("❌ Wrong password! " + String(adminWrongAttempts) + "/3. Try again:");
      }
    }
    return;
  }

  if (adminState == ADMIN_CHANGE_PASS_NEW1) {
    if (input == masterPass) {
      webLog("❌ New password is same as old! Enter a different password:");
      return;
    }
    
    // Lọc ký tự không hợp lệ (chỉ cho 0-9)
    String filtered = "";
    for (char c : input) {
      if (c >= '0' && c <= '9') filtered += c;
    }
    if (filtered != input || filtered.length() == 0) {
      webLog("❌ Password must contain digits only (0-9)! Try again:");
      return;
    }
    webNewPass1      = filtered;
    webNewPassWrong  = 0;
    adminState  = ADMIN_CHANGE_PASS_NEW2;
    webLog("🔑 Repeat new password:");
    return;
  }

  if (adminState == ADMIN_CHANGE_PASS_NEW2) {
    if (input == webNewPass1) {
      masterPass      = webNewPass1;
      savePassword(masterPass);
      webNewPass1     = "";
      webNewPassWrong = 0;
      adminState      = ADMIN_IDLE;
      webLog("✅ Password changed successfully!");
      webLog("Admin session still active. Type next command.");
    } else {
      webNewPassWrong++;
      if (webNewPassWrong >= 3) {
        webNewPass1     = "";
        webNewPassWrong = 0;
        adminState      = ADMIN_IDLE;
        lockSystem("WEB"); 
      } else {
        webLog("❌ Passwords do not match! " + String(webNewPassWrong) + "/3. Try again:");
        // Giữ nguyên ADMIN_CHANGE_PASS_NEW2 để nhập lại lần 2
      }
    }
    return;
  }
  // ============================================================
  // NORMAL COMMANDS (webState machine cũ)
  // ============================================================
  if (webState == WEB_IDLE && adminState == ADMIN_IDLE) {

    if (input == "CHECK-KP") {
      if (currentState == CHECK_PASS) {
        webLog("⏳ LCD is busy with CHECK-KP! Please wait.");
        return;
      }
      webState = WEB_WAIT_KP_PASS;
      webWrongAttempts = 0;
      webLog("🔑 Enter keypad password:");

    } else if (input == "CHECK-FP") {
      if (currentState == CHECK_PASS) {
        webLog("⏳ LCD is busy with CHECK-KP! Please wait.");
        return;
      }
      savedStateBeforeFP = currentState;
      webState   = WEB_SCAN_FP;
      webFPStart = millis();
      webLog("👆 Place finger... 10s");

    } else if (input == "ADMIN") {
      if (isAdminSessionValid()) {
        webLog("✅ Already logged in! Session active.");
      } else {
        adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Enter master password:");
      }

    } else if (input == "CHANGE-PASS") {
      // ← THÊM
       if (currentState == CHECK_PASS) {
       webLog("⏳ LCD is busy with CHECK-KP! Please wait.");
       return;
       }
      if (isAdminSessionValid()) {
        adminState = ADMIN_CHANGE_PASS_OLD;
        adminWrongAttempts = 0;
        webLog("🔑 Enter old password:");
      } else {
        pendingCmd = "CHANGE-PASS";
        adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      }

    } else if (input == "ADD-RFID") {  
      if (currentState == CHECK_PASS) {
        webLog("⏳ LCD is busy with CHECK-KP! Please wait.");
        return;
      }
      if (isAdminSessionValid()) {
        savedStateBeforeRFID = currentState;
        adminState = ADMIN_ADD_RFID;
        webLog("📡 Please scan card to ADD...");
        webLog("Type CANCEL to stop.");
        lcd.clear();
        lcd.setCursor(0,0); lcd.print("Web:Add Card");
      } else {
        pendingCmd = "ADD-RFID";
        adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      }

    } else if (input == "DEL-RFID") {
      if (currentState == CHECK_PASS) {
        webLog("⏳ LCD is busy with CHECK-KP! Please wait.");
        return;
      }
      if (isAdminSessionValid()) {
        savedStateBeforeRFID = currentState;
        adminState = ADMIN_DEL_RFID;
        webLog("📡 Please scan card to DELETE...");
        webLog("Type CANCEL to stop.");
        lcd.clear();
        lcd.setCursor(0,0); lcd.print("Web:Del Card");
      } else {
        pendingCmd = "DEL-RFID";
        adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      } 
    } else if (input == "ADD-FP") {
      if (currentState == CHECK_PASS) {
        webLog("⏳ LCD is busy with CHECK-KP! Please wait.");
        return;
      }
      if (isAdminSessionValid()) {
        savedStateBeforeFPAdmin = currentState;
        adminState = ADMIN_ADD_FP;
        webFPStep  = 0;
        webLog("🖐 Enter FP ID to add (1-5):");
      } else {
        pendingCmd = "ADD-FP";
        adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      }

    } else if (input == "DEL-FP") {
      if (currentState == CHECK_PASS) {
        webLog("⏳ LCD is busy with CHECK-KP! Please wait.");
        return;
      }
      if (isAdminSessionValid()) {
        savedStateBeforeFPAdmin = currentState;
        adminState = ADMIN_DEL_FP;
        webFPStep  = 0;
        webLog("🖐 Enter FP ID to delete (1-5):");
      } else {
        pendingCmd = "DEL-FP";
        adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      }
    } else if (input == "RESET-KP") {
      if (currentState == CHECK_PASS) {
        webLog("⏳ LCD is busy with CHECK-KP! Please wait.");
        return;
      }
      if (isAdminSessionValid()) {
        savedStateBeforeReset = currentState; 
        adminState = ADMIN_RESET_KP_CONFIRM;
        webLog("⚠️ Reset keypad password? Type YES or NO:");
      } else {
        pendingCmd = "RESET-KP"; adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      }

    } else if (input == "RESET-RFID") {
      if (currentState == CHECK_PASS) {
        webLog("⏳ LCD is busy with CHECK-KP! Please wait.");
        return;
      }
      if (isAdminSessionValid()) {
        savedStateBeforeReset = currentState; 
        adminState = ADMIN_RESET_RFID_CONFIRM;
        webLog("⚠️ Reset RFID? Type YES or NO:");
      } else {
        pendingCmd = "RESET-RFID"; adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      }

    } else if (input == "RESET-FP") {
      if (currentState == CHECK_PASS) {
        webLog("⏳ LCD is busy with CHECK-KP! Please wait.");
        return;
      }
      if (isAdminSessionValid()) {
        savedStateBeforeReset = currentState; 
        adminState = ADMIN_RESET_FP_CONFIRM;
        webLog("⚠️ Reset FP? Type YES or NO:");
      } else {
        pendingCmd = "RESET-FP"; adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      }

    } else if (input == "RESET-ALL") {
      if (currentState == CHECK_PASS) {
        webLog("⏳ LCD is busy with CHECK-KP! Please wait.");
        return;
      }
      if (isAdminSessionValid()) {
        savedStateBeforeReset = currentState; 
        adminState = ADMIN_RESET_ALL_CONFIRM;
        webLog("⚠️ Reset ALL (KP+RFID+FP)? Type YES or NO:");
      } else {
        pendingCmd = "RESET-ALL"; adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      }
    
    } else if (input == "VIEW-KP") {
      if (isAdminSessionValid()) {
        doViewKP();
      } else {
        pendingCmd = "VIEW-KP";
        adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      }

    } else if (input == "VIEW-RFID") {
      if (isAdminSessionValid()) {
        doViewRFID();
      } else {
        pendingCmd = "VIEW-RFID";
        adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      }

    } else if (input == "VIEW-FP") {
      if (isAdminSessionValid()) {
        doViewFP();
      } else {
        pendingCmd = "VIEW-FP";
        adminState = ADMIN_WAIT_MASTER;
        adminWrongAttempts = 0;
        webLog("🔐 Admin required. Enter master password:");
      }
    } else if (input == "MENU") {
      webLog("📋 COMMAND LIST: 16 commands"); delay(100);
      webLog("🔑 CHECK-KP | CHECK-FP | CHANGE-PASS | ADMIN | LOGOUT"); delay(100);
      webLog("📡 ADD-RFID | DEL-RFID | ADD-FP | DEL-FP"); delay(100);
      webLog("👁 VIEW-KP | VIEW-RFID | VIEW-FP"); delay(100);
      webLog("🔄 RESET-KP | RESET-RFID | RESET-FP | RESET-ALL"); delay(100);
      webLog("💡 Type MENU to show this list"); 

    } else if (input == "LOGOUT") {
      adminSession = false;
      adminState   = ADMIN_IDLE;
      Blynk.virtualWrite(V4, "No session");  // ← thêm dòng này
      webLog("👋 Logged out.");

    } else {
      webLog("❓ Unknown command.");
    }
    
    } else if (adminState == ADMIN_ADD_FP) {
    if (input == "CANCEL") {
      adminState = ADMIN_IDLE; webFPStep = 0; webFPID = 0;
      webLog("🚫 Add FP cancelled.");
      restoreStateAfterWebFPAdmin();
      return;
    }
    if (webFPStep == 0) {
      // Chờ nhập ID
      int id = input.toInt();
      if (id < 1 || id > FP_MAX_ID || input.length() != 1 || !isDigit(input[0])) {
        webLog("❌ Invalid ID! Enter 1-5:");
        return;
      }
      if (fpIDExists(id)) {
        webLog("❌ ID " + String(id) + " already exists! Enter another ID:");
        return;
      }
      webFPID   = id;
      webFPStep = 1;
      webLog("✅ ID " + String(id) + " selected.");
      webLog("👆 Place finger (1st scan):");
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Web:Place FP 1st");
      lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(id);
    } else {
      webLog("⏳ Scanning finger... type CANCEL to stop.");
    }

  } else if (adminState == ADMIN_DEL_FP) {
    if (input == "CANCEL") {
      adminState = ADMIN_IDLE; webFPStep = 0; webFPID = 0;
      webLog("🚫 Delete FP cancelled.");
      restoreStateAfterWebFPAdmin();
      return;
    }
    // Chờ nhập ID
    int id = input.toInt();
    if (id < 1 || id > FP_MAX_ID || input.length() != 1 || !isDigit(input[0])) {
      webLog("❌ Invalid ID! Enter 1-5:");
      return;
    }
    if (!fpIDExists(id)) {
      webLog("❌ ID " + String(id) + " does not exist! Enter another ID:");
      return;
    }
    webFPID   = id;
    adminState = ADMIN_DEL_FP_CONFIRM;
    webLog("⚠️ Delete FP ID " + String(id) + "? Type YES to confirm or NO to cancel:");
  } else if (adminState == ADMIN_DEL_FP_CONFIRM) {
    if (input == "YES") {
      bool ok = finger.deleteModel(webFPID) == FINGERPRINT_OK;
      if (ok) {
        webLog("✅ FP ID " + String(webFPID) + " deleted successfully!");
        lcd.clear(); lcd.setCursor(0,0); lcd.print("FP Deleted OK!");
        lcd.setCursor(0,1); lcd.print("ID:"); lcd.print(webFPID);
      } else {
        webLog("❌ Delete failed! Error.");
        lcd.clear(); lcd.setCursor(0,0); lcd.print("Delete Failed!");
      }
      delay(2500);
      adminState = ADMIN_IDLE; webFPID = 0;
      restoreStateAfterWebFPAdmin();

    } else if (input == "NO") {
      webLog("🚫 Delete cancelled.");
      adminState = ADMIN_IDLE; webFPID = 0;
      restoreStateAfterWebFPAdmin();

    } else {
      webLog("❓ Type YES to confirm or NO to cancel:");
    }

    } else if (adminState == ADMIN_RESET_KP_CONFIRM) {
    if (input == "YES") {
      masterPass = "1234"; savePassword(masterPass);
      webLog("✅ Keypad password reset done!");
      adminState = ADMIN_IDLE;
      restoreStateAfterWebReset();
    } else if (input == "NO") {
      webLog("🚫 Reset cancelled.");
      adminState = ADMIN_IDLE;
      restoreStateAfterWebReset();
    } else {
      webLog("❓ Type YES or NO:");
    }

  } else if (adminState == ADMIN_RESET_RFID_CONFIRM) {
    if (input == "YES") {
      restoreDefaultRFID();
      webLog("✅ RFID reset done!");
      adminState = ADMIN_IDLE;
      restoreStateAfterWebReset();
    } else if (input == "NO") {
      webLog("🚫 Reset cancelled.");
      adminState = ADMIN_IDLE;
      restoreStateAfterWebReset();
    } else {
      webLog("❓ Type YES or NO:");
    }

  } else if (adminState == ADMIN_RESET_FP_CONFIRM) {
    if (input == "YES") {
      adminState = ADMIN_IDLE;
      handleWebResetFP(false);
    } else if (input == "NO") {
      webLog("🚫 Reset cancelled.");
      adminState = ADMIN_IDLE;
      restoreStateAfterWebReset();
    } else {
      webLog("❓ Type YES or NO:");
    }

  } else if (adminState == ADMIN_RESET_ALL_CONFIRM) {
    if (input == "YES") {
      // Reset pass
      masterPass = "1234"; savePassword(masterPass);
      // Reset RFID
      restoreDefaultRFID();
      // Reset FP (hiện LCD khi scan)
      adminState = ADMIN_IDLE;
      handleWebResetFP(true);
    } else if (input == "NO") {
      webLog("🚫 Reset cancelled.");
      adminState = ADMIN_IDLE;
      restoreStateAfterWebReset();
    } else {
      webLog("❓ Type YES or NO:");
    }

  } else if (webState == WEB_WAIT_KP_PASS) {
    if (input == masterPass) {
      webWrongAttempts = 0;
      webState = WEB_IDLE;
      webLog("✅ Password OK! Opening door...");
      openDoor();
      webLog("⚫ Door closed...");
    } else {
      webWrongAttempts++;
      if (webWrongAttempts >= 3) {
        webState = WEB_IDLE;
        webWrongAttempts = 0;
        lockSystem("WEB"); 
      } else {
        webLog("❌ Wrong password! " + String(webWrongAttempts) + "/3. Try again:");
      }
    }

  } else if (webState == WEB_SCAN_FP) {
  if (input == "CANCEL") {
    webState = WEB_IDLE;
    static int lastLeft = -1;
    lastLeft = -1;

    // Cập nhật LCD ngay lập tức
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("FP Cancelled!");
    delay(1000);
    restoreStateAfterWebFP();
    webLog("🚫 FP scan cancelled.");
    }
  }
   else if (adminState == ADMIN_ADD_RFID || adminState == ADMIN_DEL_RFID) {
  if (input == "CANCEL") {
    adminState = ADMIN_IDLE;
    webLog("🚫 RFID operation cancelled.");
    enteredPass = "";
    wrongAttempts = 0;
    restoreStateAfterWebRFID();
  } else {
    webLog("⏳ WEB is busy with ADD/DEL RFID! Please wait.");
  }
}
}


// Gửi trạng thái cửa lên Blynk mỗi 1s
void sendDoorStatus() {
  Blynk.virtualWrite(V0, doorOpen ? DOOR_OPEN : DOOR_CLOSE);
}


// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  mySerial.begin(57600, SERIAL_8N1, RXD2, TXD2);
  // LCD
  lcd.init(); lcd.backlight();
  lcd.setCursor(0,0); lcd.print("DO AN TOT NGHIEP");
  lcd.setCursor(0,1); lcd.print("VO THANH HAI");
  delay(2000);

  // Load mật khẩu
  masterPass = loadPassword();
  if (masterPass.length() == 0) { masterPass = "1234"; savePassword(masterPass); }

  // Kiểm tra RFID EEPROM
  bool allEmpty = true;
  for (int i = 0; i < MAX_CARDS * UID_SIZE; i++)
    if (EEPROM.read(i) != 0xFF) { allEmpty = false; break; }
  if (allEmpty) restoreDefaultRFID();

  // Vân tay
  if (!finger.verifyPassword()) { Serial.println("No FP sensor!"); while (1) delay(1); }
  setupFPDefaults();

  // Hardware
  pinMode(relayPin,  OUTPUT); digitalWrite(relayPin,  LOW);
  pinMode(buzzerPin, OUTPUT); digitalWrite(buzzerPin, LOW);
  pinMode(buttonPin, INPUT);
  SPI.begin();
  mfrc522.PCD_Init();

  // LittleFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed!");
  } else {
    Serial.println("LittleFS OK");
  }

  // WiFi
loadWiFiCreds();
bool wifiOK = connectWiFi();  // ← connectWiFi() tự lưu IP bên trong

prefs.begin("boot", false);   // ← mở SAU khi connectWiFi() đã xong
if (!wifiOK) {
  prefs.putString("last", "fail");
  prefs.end();
  startAPMode();
} else {
  prefs.putString("last", "ok");
  prefs.end();
  setupWebServer();
  Blynk.config(BLYNK_AUTH_TOKEN);       // ← thêm
  Blynk.connect();                       // ← thêm
  blynkTimer.setInterval(1000L, sendDoorStatus);  // ← thêm
  blynkTimer.setInterval(1000L, updateAdminCountdown);
}
  showMenu();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // Web server
  server.handleClient();

  // ★ FIX: Tách rõ AP mode và STA mode
  if (!apMode && WiFi.status() != WL_CONNECTED) {
    if (disconnectTime == 0) disconnectTime = millis();
    if (millis() - disconnectTime > RECONNECT_TIMEOUT) {
      disconnectTime = 0;
      startAPMode();
    }
  } else if (!apMode && WiFi.status() == WL_CONNECTED) {
    disconnectTime = 0;
    if (!Blynk.connected()) {
      Serial.println("Reconnecting Blynk...");
      Blynk.connect();
    }
    Blynk.run();
    blynkTimer.run();
    handleWebFPScan();
    handleWebRFID();
    handleWebFPEnroll();
  }
  // AP mode: chỉ server.handleClient() đã gọi ở đầu loop, không gọi Blynk

  char key = keypad.getKey();
  if (key) {
    if (currentState == LOCKED) return;
    if (webState == WEB_SCAN_FP) return;
    if (adminState == ADMIN_ADD_RFID || adminState == ADMIN_DEL_RFID) return;
    if (adminState == ADMIN_ADD_FP   || adminState == ADMIN_DEL_FP   ||
      adminState == ADMIN_DEL_FP_CONFIRM) return;  // ← THÊM

    digitalWrite(buzzerPin, HIGH); delay(50); digitalWrite(buzzerPin, LOW);
    // ---- MAIN MENU ----
    if (currentState == MAIN_MENU) {
      if      (key == '*') { menuIndex = (menuIndex+1) % menuLength; showMenu(); }
      else if (key == '#') { currentState = CHECK_MENU;  checkIndex = 0; showCheckMenu(); }
      else if (key == 'A') { wrongAttempts = 0; currentState = MASTER_PASS;  startMasterPass(); }
      else if (key == 'B') { wrongAttempts = 0; currentState = RFID_MASTER;  startMasterPass(); }
      else if (key == 'C') { wrongAttempts = 0; currentState = FP_MASTER;    startMasterPass(); }
      else if (key == 'D') { wrongAttempts = 0; currentState = RESET_MASTER; startMasterPass(); }
    }

    // ---- CHECK MENU ----
    else if (currentState == CHECK_MENU) {
      if      (key == '*') { checkIndex = (checkIndex+1) % checkLength; showCheckMenu(); }
      else if (key == '0') { currentState = MAIN_MENU; showMenu(); }
      else if (key == '1') { 
       if (isWebChangingPass()) {
        lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
        lcd.setCursor(0,1); lcd.print("CHANGE-PASS...");
        delay(2000); showCheckMenu(); return;
       }
       if (webState == WEB_WAIT_KP_PASS) {
        lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
        lcd.setCursor(0,1); lcd.print("CHECK-KP...");
        delay(2000); showCheckMenu(); return;
        }

       if (isWebResetting()) {  // ← THÊM
        lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
        lcd.setCursor(0,1); lcd.print("RESET-DATA...");
        delay(2000); showCheckMenu(); return;
       }
        currentState = CHECK_PASS; startCheckPass(); }

      else if (key == '2') { 
       if (isWebChangingPass()) {
        lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
        lcd.setCursor(0,1); lcd.print("CHANGE-PASS...");
        delay(2000); showCheckMenu(); return;
        }

        if (webState == WEB_WAIT_KP_PASS) {  // ← THÊM
          lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
          lcd.setCursor(0,1); lcd.print("CHECK-KP...");
          delay(2000); showCheckMenu(); return;
        }

        if (isWebResetting()) {  // ← THÊM
        lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
        lcd.setCursor(0,1); lcd.print("RESET-DATA...");
        delay(2000); showCheckMenu(); return;
       }
        checkFingerprint(); }
    }

    // ---- CHECK PASS ----
    else if (currentState == CHECK_PASS) {
      if (key >= '0' && key <= '9') { enteredPass += key; printHiddenChar(key); }
      else if (key == '*') { deleteLastChar(); }
      else if (key == '0') { currentState = CHECK_MENU; showCheckMenu(); }
      else if (key == '#') {
        if (enteredPass.length() == 0) {
          lcd.setCursor(0,1); lcd.print("Enter pass!");
          delay(1000); lcd.setCursor(0,1); lcd.print("                ");
        } else if (enteredPass == masterPass) {
          wrongAttempts = 0; lcd.clear(); lcd.print("Password OK!"); delay(1000); openDoor();
        } else {
          wrongAttempts++;
          lcd.clear(); lcd.print("Wrong Pass!");
          lcd.setCursor(0,1); lcd.print(wrongAttempts); lcd.print("/3");
          delay(1000);
          if (wrongAttempts >= 3) lockSystem("LCD"); else startCheckPass();
        }
      }
    }

    // ---- MASTER PASS ----
    else if (currentState == MASTER_PASS) {
      if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'D')) { enteredPass += key; printHiddenChar(key); }
      else if (key == '*') { deleteLastChar(); }
      else if (key == '0') { currentState = MAIN_MENU; showMenu(); }
      else if (key == '#') {
        if (enteredPass.length() == 0) {
          lcd.setCursor(0,1); lcd.print("Enter pass!");
          delay(1000); lcd.setCursor(0,1); lcd.print("                ");
        } else if (enteredPass == adminPass) {
          wrongAttempts = 0; lcd.clear(); lcd.print("Master OK!"); delay(1000);
          // ← THÊM
          if (webState == WEB_WAIT_KP_PASS) {
           lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
           lcd.setCursor(0,1); lcd.print("CHECK-KP...");
           delay(2000); currentState = MAIN_MENU; showMenu();
          // ← THÊM
         } else if (isWebChangingPass()) {
           lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
           lcd.setCursor(0,1); lcd.print("CHANGE-PASS...");
           delay(2000); currentState = MAIN_MENU; showMenu();

         } else if (isWebResetting()) {  // ← THÊM
             lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
             lcd.setCursor(0,1); lcd.print("RESET-DATA...");
             delay(2000); currentState = MAIN_MENU; showMenu();
           } else {
            currentState = CHANGE_OLD; startOldPass();
         }
         } else {
          wrongAttempts++;
          lcd.clear(); lcd.print("Wrong Master!");
          lcd.setCursor(0,1); lcd.print(wrongAttempts); lcd.print("/3");
          delay(1000);
          if (wrongAttempts >= 3) lockSystem("LCD"); else startMasterPass();
        }
      }
    }

    // ---- CHANGE OLD ----
    else if (currentState == CHANGE_OLD) {
      if (key >= '0' && key <= '9') { enteredPass += key; printHiddenChar(key); }
      else if (key == '*') { deleteLastChar(); }
      else if (key == '#') {
        if (enteredPass.length() == 0) {
          lcd.setCursor(0,1); lcd.print("Enter pass!");
          delay(1000); lcd.setCursor(0,1); lcd.print("                ");
        } else if (enteredPass == masterPass) {
          wrongAttempts = 0; lcd.clear(); lcd.print("Old OK!"); delay(1000);
          currentState = CHANGE_NEW1; startNewPass1();
        } else {
          wrongAttempts++;
          lcd.clear(); lcd.print("Wrong Old!");
          lcd.setCursor(0,1); lcd.print(wrongAttempts); lcd.print("/3");
          delay(1000);
          if (wrongAttempts >= 3) lockSystem("LCD"); else startOldPass();
        }
      }
    }

    // ---- CHANGE NEW 1 ----
    else if (currentState == CHANGE_NEW1) {
      if (key >= '0' && key <= '9') { enteredPass += key; printHiddenChar(key); }
      else if (key == '*') { deleteLastChar(); }
      else if (key == '#') {
        if (enteredPass.length() == 0) {
          lcd.setCursor(0,1); lcd.print("Enter pass!");
          delay(1000); lcd.setCursor(0,1); lcd.print("                ");
        } else {
          newPass1 = enteredPass;
          if (newPass1 == masterPass) {
            lcd.clear(); lcd.print("Same as Old!"); delay(1500); startNewPass1();
          } else {
            currentState = CHANGE_NEW2; startNewPass2();
          }
        }
      }
    }

    // ---- CHANGE NEW 2 ----
    else if (currentState == CHANGE_NEW2) {
      if (key >= '0' && key <= '9') { enteredPass += key; printHiddenChar(key); }
      else if (key == '*') { deleteLastChar(); }
      else if (key == '#') {
        if (enteredPass.length() == 0) {
          lcd.setCursor(0,1); lcd.print("Enter pass!");
          delay(1000); lcd.setCursor(0,1); lcd.print("                ");
        } else if (enteredPass == newPass1) {
          masterPass = newPass1; savePassword(masterPass);
          lcd.clear(); lcd.print("Change Success!"); delay(1500);
          wrongNewAttempts = 0; currentState = MAIN_MENU; showMenu();
        } else {
          wrongNewAttempts++;
          lcd.clear(); lcd.print("Not Match!");
          lcd.setCursor(0,1); lcd.print(wrongNewAttempts); lcd.print("/3");
          delay(1200);
          if (wrongNewAttempts >= 3) {
            wrongNewAttempts = 0;
            lockSystem("LCD");
          } else startNewPass2();
        }
      }
    }

    // ---- RFID MASTER ----
    else if (currentState == RFID_MASTER) {
      if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'D')) { enteredPass += key; printHiddenChar(key); }
      else if (key == '*') { deleteLastChar(); }
      else if (key == '0') { currentState = MAIN_MENU; showMenu(); }
      else if (key == '#') {
        if (enteredPass.length() == 0) {
          lcd.setCursor(0,1); lcd.print("Enter pass!");
          delay(1000); lcd.setCursor(0,1); lcd.print("                ");
        } else if (enteredPass == adminPass) {
          wrongAttempts = 0; lcd.clear(); lcd.print("Master OK!"); delay(1000);
          currentState = RFID_MENU; rfidIndex = 0; showRFIDMenu();
        } else {
          wrongAttempts++;
          lcd.clear(); lcd.print("Wrong Master!");
          lcd.setCursor(0,1); lcd.print(wrongAttempts); lcd.print("/3");
          delay(1000);
          if (wrongAttempts >= 3) lockSystem("LCD"); else startMasterPass();
        }
      }
    }

    // ---- RFID MENU ----
    else if (currentState == RFID_MENU) {
      if (key == '*') { rfidIndex = (rfidIndex+1) % rfidLength; showRFIDMenu(); }
      else if (key == '0') { currentState = MAIN_MENU; showMenu(); }
      else if (key == '1') {
        if (webState == WEB_WAIT_KP_PASS) {  // ← THÊM
          lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
          lcd.setCursor(0,1); lcd.print("CHECK-KP...");
          delay(2000); showRFIDMenu(); return;
        }
        if (isWebChangingPass()) {
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("CHANGE-PASS...");
         delay(2000); showRFIDMenu(); return;
        }
        if (isWebResetting()) {  // ← THÊM
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("RESET-DATA...");
         delay(2000); showRFIDMenu(); return;
       }

        currentState = RFID_ADD_LCD;
        lcd.clear(); lcd.print("Scan add card");
        while (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) { 
        // ★ FIX: dùng runBackground()
          runBackground(); delay(10);
        }
        byte *newUID = mfrc522.uid.uidByte;
        // ← THÊM
         mfrc522.PICC_HaltA();
         mfrc522.PCD_StopCrypto1();

        if (uidExists(newUID)) { lcd.clear(); lcd.print("Card Exists!"); delay(2000); }
        else {
          int slot = findEmptySlot();
          if (slot != -1) { saveUID(slot, newUID); lcd.clear(); lcd.setCursor(0,0); lcd.print("Added OK!"); displayUID(newUID); delay(2500); }
          else { lcd.clear(); lcd.print("Memory Full!"); delay(2000); }
        }
        currentState = RFID_MENU;
        showRFIDMenu();
      }
      else if (key == '2') {
        if (webState == WEB_WAIT_KP_PASS) {  // ← THÊM
          lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
          lcd.setCursor(0,1); lcd.print("CHECK-KP...");
          delay(2000); showRFIDMenu(); return;
        }
        if (isWebChangingPass()) {
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("CHANGE-PASS...");
         delay(2000); showRFIDMenu(); return;
         }
        if (isWebResetting()) {  // ← THÊM
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("RESET-DATA...");
         delay(2000); showRFIDMenu(); return;
       }

         currentState = RFID_DEL_LCD;
        lcd.clear(); lcd.print("Scan dele card");
        while (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
        // ★ FIX: dùng runBackground()
          runBackground(); delay(10);
        }
        byte *delUID = mfrc522.uid.uidByte;
        // ← THÊM
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();

        int slot = findSlotByUID(delUID);
        if (slot == -1) { lcd.clear(); lcd.print("Card Not Exists!"); delay(2000); }
        else { deleteUID(slot); lcd.clear(); lcd.setCursor(0,0); lcd.print("Deleted OK!"); displayUID(delUID); delay(2500); }
        currentState = RFID_MENU;
        showRFIDMenu();
      }
    }

    // ---- FP MASTER ----
    else if (currentState == FP_MASTER) {
      if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'D')) { enteredPass += key; printHiddenChar(key); }
      else if (key == '*') { deleteLastChar(); }
      else if (key == '0') { currentState = MAIN_MENU; showMenu(); }
      else if (key == '#') {
        if (enteredPass.length() == 0) {
          lcd.setCursor(0,1); lcd.print("Enter pass!");
          delay(1000); lcd.setCursor(0,1); lcd.print("                ");
        } else if (enteredPass == adminPass) {
          wrongAttempts = 0; lcd.clear(); lcd.print("Master OK!"); delay(1000);
          currentState = FP_MENU; fpIndex = 0; showFPMenu();
        } else {
          wrongAttempts++;
          lcd.clear(); lcd.print("Wrong Master!");
          lcd.setCursor(0,1); lcd.print(wrongAttempts); lcd.print("/3");
          delay(1000);
          if (wrongAttempts >= 3) lockSystem("LCD"); else startMasterPass();
        }
      }
    }

    // ---- FP MENU ----
    else if (currentState == FP_MENU) {
      if      (key == '*') { fpIndex = (fpIndex+1) % fpLength; showFPMenu(); }
      else if (key == '0') { currentState = MAIN_MENU; showMenu(); }
      else if (key == '1') { 
        if (webState == WEB_WAIT_KP_PASS) {  // ← THÊM
          lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
          lcd.setCursor(0,1); lcd.print("CHECK-KP...");
          delay(2000); showFPMenu(); return;
        }
        if (isWebChangingPass()) {
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("CHANGE-PASS...");
        delay(2000); showFPMenu(); return;
        }
        if (isWebResetting()) {  // ← THÊM
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("RESET-DATA...");
         delay(2000); showFPMenu(); return;
       }

        currentState = FP_ADD_LCD;  // ← THÊM
        addFingerprint();    currentState = FP_MENU; showFPMenu(); }
      else if (key == '2') { 
        if (webState == WEB_WAIT_KP_PASS) {  // ← THÊM
          lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
          lcd.setCursor(0,1); lcd.print("CHECK-KP...");
          delay(2000); showFPMenu(); return;
        }
        if (isWebChangingPass()) {
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("CHANGE-PASS...");
        delay(2000); showFPMenu(); return;
        } 
        if (isWebResetting()) {  // ← THÊM
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("RESET-DATA...");
         delay(2000); showFPMenu(); return;
       }

        currentState = FP_DEL_LCD;  // ← THÊM
        deleteFingerprint(); currentState = FP_MENU; showFPMenu(); }
    }

    // ---- RESET MASTER ----
    else if (currentState == RESET_MASTER) {
      if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'D')) { enteredPass += key; printHiddenChar(key); }
      else if (key == '*') { deleteLastChar(); }
      else if (key == '0') { currentState = MAIN_MENU; showMenu(); }
      else if (key == '#') {
        if (enteredPass.length() == 0) {
          lcd.setCursor(0,1); lcd.print("Enter pass!");
          delay(1000); lcd.setCursor(0,1); lcd.print("                ");
        } else if (enteredPass == adminPass) {
          lcd.clear(); lcd.print("Master OK!"); delay(1000);
          currentState = RESET_MENU; resetIndex = 0; showResetMenu();
        } else {
          wrongAttempts++;
          lcd.clear(); lcd.print("Wrong Master!");
          lcd.setCursor(0,1); lcd.print(wrongAttempts); lcd.print("/3");
          delay(1000);
          if (wrongAttempts >= 3) lockSystem("LCD"); else startMasterPass();
        }
      }
    }

    // ---- RESET MENU ----
    else if (currentState == RESET_MENU) {
      if (key == '*') { resetIndex = (resetIndex+1) % resetLength; showResetMenu(); }
      else if (key == '0') { currentState = MAIN_MENU; showMenu(); }
      else if (key == '1') {
        if (webState == WEB_WAIT_KP_PASS) {  // ← THÊM
          lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
          lcd.setCursor(0,1); lcd.print("CHECK-KP...");
          delay(2000); showResetMenu(); return;
        }
        if (isWebChangingPass()) {
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("CHANGE-PASS...");
        delay(2000); showResetMenu(); return;
        }
        if (isWebResetting()) {  // ← THÊM
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("RESET-DATA...");
         delay(2000); showResetMenu(); return;
       }

        currentState = RESET_KP_LCD;  // ← THÊM

        lcd.clear(); lcd.setCursor(0,0); lcd.print("Reset KP Pass?");
        lcd.setCursor(0,1); lcd.print("#:Yes  *:No");
        while (true) {
          char k = keypad.getKey();
          // ★ FIX: dùng runBackground()
          runBackground();
          if (k == '#') { masterPass = "1234"; savePassword(masterPass); lcd.clear(); lcd.print("Pass Reset Done!"); delay(1500); break; }
          else if (k == '*') { lcd.clear(); lcd.print("Canceled"); delay(1000); break; }
        }
        currentState = RESET_MENU;
        showResetMenu();
      }
      else if (key == '2') {
        if (webState == WEB_WAIT_KP_PASS) {  // ← THÊM
          lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
          lcd.setCursor(0,1); lcd.print("CHECK-KP...");
          delay(2000); showResetMenu(); return;
        }
        if (isWebChangingPass()) {
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("CHANGE-PASS...");
        delay(2000); showResetMenu(); return;
        }
        if (isWebResetting()) {  // ← THÊM
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("RESET-DATA...");
         delay(2000); showResetMenu(); return;
       }
        currentState = RESET_RFID_LCD,

        lcd.clear(); lcd.setCursor(0,0); lcd.print("Reset RFID?");
        lcd.setCursor(0,1); lcd.print("#:Yes  *:No");
        while (true) {
          char k = keypad.getKey();
          // ★ FIX: dùng runBackground()
          runBackground();
          if (k == '#') { restoreDefaultRFID(); lcd.clear(); lcd.print("RFID Reset Done!"); delay(1500); break; }
          else if (k == '*') { lcd.clear(); lcd.print("Canceled"); delay(1000); break; }
        }
        currentState = RESET_MENU;
        showResetMenu();
      }
      else if (key == '3') {
        if (webState == WEB_WAIT_KP_PASS) {  // ← THÊM
          lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
          lcd.setCursor(0,1); lcd.print("CHECK-KP...");
          delay(2000); showResetMenu(); return;
        }
        if (isWebChangingPass()) {
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("CHANGE-PASS...");
        delay(2000); showResetMenu(); return;
        } 
        if (isWebResetting()) {  // ← THÊM
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("RESET-DATA...");
         delay(2000); showResetMenu(); return;
       }

        currentState = RESET_FP_LCD;

        lcd.clear(); lcd.setCursor(0,0); lcd.print("Reset FP?");
        lcd.setCursor(0,1); lcd.print("#:Yes  *:No");
        while (true) {
          char k = keypad.getKey();
          // ★ FIX: dùng runBackground()
          runBackground();
          if (k == '#') { resetFingerprint(); lcd.clear(); lcd.print("FP Reset Done!"); delay(1500); break; }
          else if (k == '*') { lcd.clear(); lcd.print("Canceled"); delay(1000); break; }
        }
        currentState = RESET_MENU;
        showResetMenu();
      }
      else if (key == '4') {
        if (webState == WEB_WAIT_KP_PASS) {  // ← THÊM
          lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
          lcd.setCursor(0,1); lcd.print("CHECK-KP...");
          delay(2000); showResetMenu(); return;
        }
        if (isWebChangingPass()) {
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("CHANGE-PASS...");
        delay(2000); showResetMenu(); return;
        }
        if (isWebResetting()) {  // ← THÊM
         lcd.clear(); lcd.setCursor(0,0); lcd.print("Web is busy!");
         lcd.setCursor(0,1); lcd.print("RESET-DATA...");
         delay(2000); showResetMenu(); return;
       }

        currentState = RESET_ALL_LCD; 
        
        lcd.clear(); lcd.setCursor(0,0); lcd.print("Reset ALL?");
        lcd.setCursor(0,1); lcd.print("#:Yes  *:No");
        while (true) {
          char k = keypad.getKey();
          // ★ FIX: dùng runBackground()
          runBackground();
          if (k == '#') {
            masterPass = "1234"; savePassword(masterPass);
            restoreDefaultRFID(); resetFingerprint();
            lcd.clear(); lcd.print("All Reset Done!"); delay(2000); break;
          } else if (k == '*') { lcd.clear(); lcd.print("Canceled"); delay(1000); break; }
        }
        currentState = RESET_MENU;
        showResetMenu();
      }
    }

  } // end if(key)

  // ---- Nút mở cửa vật lý ----
if (digitalRead(buttonPin) == LOW) {
  if (currentState == LOCKED) return;  // ← THÊM
  if (doorOpen) return;  // ← THÊM

  bool allowButton = (currentState == MAIN_MENU   || currentState == CHECK_MENU  ||
                      currentState == MASTER_PASS || currentState == RFID_MASTER ||
                      currentState == FP_MASTER   || currentState == RESET_MASTER);

  bool webBusyNoFP = (webState == WEB_WAIT_KP_PASS || adminState == ADMIN_CHANGE_PASS_OLD ||
                      adminState == ADMIN_CHANGE_PASS_NEW1 || adminState == ADMIN_DEL_FP || adminState == ADMIN_DEL_FP_CONFIRM ||
                      adminState == ADMIN_RESET_KP_CONFIRM || adminState == ADMIN_RESET_RFID_CONFIRM ||
                      adminState == ADMIN_CHANGE_PASS_NEW2 ||
                      ((adminState == ADMIN_RESET_FP_CONFIRM || adminState == ADMIN_RESET_ALL_CONFIRM) && !webEnrollingFP));

  // ← THÊM: im lặng khi web đang thực hiện các chức năng có LCD riêng
  bool webBusySilent = (adminState == ADMIN_ADD_RFID  || adminState == ADMIN_DEL_RFID ||
                        adminState == ADMIN_ADD_FP || 
                        ((adminState == ADMIN_RESET_FP_CONFIRM || adminState == ADMIN_RESET_ALL_CONFIRM) && webEnrollingFP));

  if (webState == WEB_SCAN_FP) {
    // im lặng

  } else if (webBusySilent) {
    // ← im lặng, không hiện gì cả

  } else if (webBusyNoFP) {
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("Web is busy!");
    lcd.setCursor(0,1); lcd.print("Please wait...");
    delay(2000);
    showMenu();

  } else if (allowButton) {
    openDoor();
  }
}


  // ---- Quét RFID ----
  bool rfidAllowed = (currentState == MAIN_MENU    ||
                      currentState == CHECK_MENU   ||
                      currentState == MASTER_PASS  ||
                      currentState == RFID_MASTER  ||
                      currentState == FP_MASTER    ||
                      currentState == RESET_MASTER);
                
  // THÊM DÒNG NÀY: chặn quét mở cửa khi web đang ADD/DEL RFID
  if (adminState == ADMIN_ADD_RFID || adminState == ADMIN_DEL_RFID) rfidAllowed = false; 
  if (webState == WEB_SCAN_FP)  rfidAllowed = false;   // ← THÊM
  if (adminState == ADMIN_ADD_FP && webFPStep > 0) rfidAllowed = false; // ← THÊM
  if (webEnrollingFP) rfidAllowed = false;              // ← THÊM     
                
  if (rfidAllowed && mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    byte *uid = mfrc522.uid.uidByte;
    State savedState = currentState;

    if (uidExists(uid)) {
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Card OK!");
      displayUID(uid); delay(1500);
      // ← THÊM
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Thank you!");
      lcd.setCursor(0,1); lcd.print("Door Opened");

      doorOpen = true;                              // ← thêm
      Blynk.virtualWrite(V0, DOOR_OPEN);           // ← thêm

      digitalWrite(relayPin, HIGH);
      digitalWrite(buzzerPin, HIGH); delay(200); digitalWrite(buzzerPin, LOW);
      delay(5000);
      digitalWrite(relayPin, LOW);
      ignoreV1Once = true; 
      doorOpen = false;                             // ← thêm
      Blynk.virtualWrite(V0, DOOR_CLOSE);          // ← thêm

      currentState = savedState;
      // Khôi phục màn hình
      if      (savedState == MAIN_MENU)   showMenu();
      else if (savedState == CHECK_MENU)  showCheckMenu();
      else if (savedState == MASTER_PASS) startMasterPass();
      else if (savedState == RFID_MASTER) startMasterPass();
      else if (savedState == FP_MASTER)   startMasterPass();
      else if (savedState == RESET_MASTER)startMasterPass();
      else showMenu();
    } else {
      lcd.clear(); lcd.print("Unknown Card!");
      digitalWrite(buzzerPin, HIGH); delay(2000); digitalWrite(buzzerPin, LOW);
      currentState = savedState;
      if (savedState == MAIN_MENU) showMenu();
      else if (savedState == CHECK_MENU) showCheckMenu();
      else startMasterPass();
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    delay(5);
  }

  delay(50);
}