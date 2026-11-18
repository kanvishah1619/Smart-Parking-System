#include <HTTPClient.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// ============================================================
// SMART PARKING SYSTEM - CORRECTED VERSION
// Fixes included:
//  1. Sensor is the only thing that frees a slot
//  2. Real Wi-Fi status + auto reconnect
//  3. Unclaimed sessions expire
//  4. Car parking in a different slot is adopted by the session
//  5. Secure random codes (esp_random)
//  6. Exit brute-force lockout
//  7. Watchman password injected (no hardcoding in JS)
//  8. Non-blocking sensor scheduling + arrival debounce
//  9. 128x64 I2C OLED status display (SSD1306)
// 11. Green LEDs ON at power-up, OFF when a slot is occupied
// 12. Buzzer beeps on every vehicle entry and exit
// 10. Cosmetic cleanups (unused vars, comments, public revenue)
// ============================================================

// ===================== Wi-Fi ================================
const char* ssid = "username";
const char* password = "password";

// ================= ThingSpeak =================
const char* thingSpeakAPIKey = "placeholderkey";

const unsigned long THINGSPEAK_INTERVAL = 15000; // 15 seconds
unsigned long lastThingSpeakUpdate = 0;

// ================= Watchman Password =========================
const char* watchmanPassword = "watchman123";

// ================= Pins =====================================
const int trigPins[3] = {23, 19, 18};
const int echoPins[3] = {34, 35, 39};   // use 1k/2k divider on each ECHO
const int servoPin = 17;
const int greenLED[3] = {13, 14, 16};   // Green ON = free
const int buzzerPin = 4;   // GPIO 22 is used by the OLED (SCL)

Servo gateServo;

// OLED (SSD1306 128x64, I2C): SDA = GPIO 21, SCL = GPIO 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledOK = false;

// ================= Settings =================================
const int threshold = 25;                              // cm
const unsigned long PARK_CONFIRM_TIME = 2000;          // clear time before slot frees
const int ARRIVE_READINGS = 3;                         // consecutive hits to mark occupied
const unsigned long SENSOR_INTERVAL = 80;              // ms between sensor reads
const unsigned long UNCLAIMED_TIMEOUT = 10UL * 60UL * 1000UL; // 10 min

const float RATE_PER_MINUTE = 1.0;
const float MINIMUM_CHARGE = 1.0;

// Exit lockout
const int MAX_FAILED = 5;
const unsigned long LOCKOUT_MS = 10000;
int failedAttempts = 0;
unsigned long lockUntil = 0;

WebServer server(80);
Preferences prefs;

// ================= Data =====================================
struct Slot {
  bool occupied;
};
Slot slots[3];

struct ParkingSession {
  bool active;
  bool arrived;          // car has been seen in a slot
  int code;
  int slot;
  unsigned long entryTime;
};
ParkingSession sessions[3];

unsigned long emptyStart[3] = {0, 0, 0};
int hitCount[3] = {0, 0, 0};

unsigned long lastDisplayUpdate = 0;
unsigned long lastSensorRead = 0;
int currentSensor = 0;

unsigned long totalVehicles = 0;
float totalRevenue = 0.0;
bool gateOpen = false;


// ============================================================
// UTILITIES
// ============================================================

void clearSession(int index) {
  sessions[index].active = false;
  sessions[index].arrived = false;
  sessions[index].code = 0;
  sessions[index].slot = -1;
  sessions[index].entryTime = 0;
}

void beep(int count, int duration = 100) {
  for (int i = 0; i < count; i++) {
    digitalWrite(buzzerPin, HIGH);
    delay(duration);
    digitalWrite(buzzerPin, LOW);
    if (i < count - 1) delay(40);
  }
}

long getDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 999;
  return (long)(duration * 0.0343 / 2.0);
}


// ============================================================
// SESSION MANAGEMENT
// ============================================================

bool sessionForSlot(int slot) {
  for (int i = 0; i < 3; i++) {
    if (sessions[i].active && sessions[i].slot == slot) return true;
  }
  return false;
}

int findFreeSession() {
  for (int i = 0; i < 3; i++) {
    if (!sessions[i].active) return i;
  }
  return -1;
}

int findAvailableSlot() {
  for (int i = 0; i < 3; i++) {
    if (!slots[i].occupied && !sessionForSlot(i)) return i;
  }
  return -1;
}

int findSessionByCode(int code) {
  for (int i = 0; i < 3; i++) {
    if (sessions[i].active && sessions[i].code == code) return i;
  }
  return -1;
}

int availableCount() {
  int count = 0;
  for (int i = 0; i < 3; i++) {
    if (!slots[i].occupied && !sessionForSlot(i)) count++;
  }
  return count;
}

// FIX 4: if a car parks in a slot that has no session, attach it to
// a session whose car has not arrived yet (it parked in the "wrong" slot).
void adoptSlot(int slot) {
  if (sessionForSlot(slot)) return;

  for (int s = 0; s < 3; s++) {
    if (sessions[s].active && !sessions[s].arrived) {
      Serial.print("Session moved from S");
      Serial.print(sessions[s].slot + 1);
      Serial.print(" to S");
      Serial.println(slot + 1);
      sessions[s].slot = slot;
      return;
    }
  }
}

// FIX 3: mark arrivals and expire unclaimed sessions
void maintainSessions() {
  for (int s = 0; s < 3; s++) {
    if (!sessions[s].active) continue;

    if (slots[sessions[s].slot].occupied) {
      sessions[s].arrived = true;
    }
    else if (!sessions[s].arrived &&
             millis() - sessions[s].entryTime > UNCLAIMED_TIMEOUT) {
      Serial.print("Unclaimed session expired for S");
      Serial.println(sessions[s].slot + 1);
      clearSession(s);
    }
  }
}


// ============================================================
// LED + OLED
// ============================================================

void updateSlotLEDs() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(greenLED[i], slots[i].occupied ? LOW : HIGH);
  }
}

char slotChar(int i) {
  if (slots[i].occupied) return 'B';
  if (sessionForSlot(i)) return 'A';
  return 'F';
}

void showOLEDMessage(const String& line1, const String& line2) {
  if (!oledOK) return;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(line1);
  display.setCursor(0, 16);
  display.println(line2);
  display.display();
}

void updateOLED() {
  if (!oledOK) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("SMART PARKING");

  display.setCursor(0, 16);
  display.print("S1:");
  display.print(slotChar(0));
  display.print("  S2:");
  display.print(slotChar(1));
  display.print("  S3:");
  display.print(slotChar(2));

  display.setCursor(0, 32);
  display.print("Free: ");
  display.print(availableCount());
  display.println("/3");

  display.setCursor(0, 48);
  display.print("Gate: ");
  display.println(gateOpen ? "OPEN" : "CLOSED");

  display.display();
}


// ============================================================
// PERSISTENT STORAGE
// ============================================================

void savePersistentData() {
  prefs.begin("parking", false);
  prefs.putUInt("vehicles", totalVehicles);
  prefs.putFloat("revenue", totalRevenue);
  prefs.end();
}

void loadPersistentData() {
  prefs.begin("parking", false);   // read/write avoids NVS "not found" error
  totalVehicles = prefs.getUInt("vehicles", 0);
  totalRevenue = prefs.getFloat("revenue", 0.0);
  prefs.end();
}


// ============================================================
// NEW SESSION
// ============================================================

bool generateCode(int &generatedCode, int &assignedSlot) {
  int slot = findAvailableSlot();
  int session = findFreeSession();

  if (slot == -1 || session == -1) return false;

  int code;
  do {
    code = 100000 + (esp_random() % 900000);   // FIX 5
  } while (findSessionByCode(code) != -1);

  sessions[session].active = true;
  sessions[session].arrived = false;
  sessions[session].code = code;
  sessions[session].slot = slot;
  sessions[session].entryTime = millis();

  generatedCode = code;
  assignedSlot = slot;

  Serial.println("-----------------------------");
  Serial.println("NEW PARKING SESSION");
  Serial.print("Slot : S");
  Serial.println(slot + 1);
  Serial.print("Code : ");
  Serial.println(code);
  Serial.println("-----------------------------");

  beep(2, 80);
  updateSlotLEDs();
  updateOLED();
  return true;
}


// ============================================================
// EXIT + BILLING
// ============================================================

String vehicleExit(int code) {
  int index = findSessionByCode(code);

  if (index == -1) {
    beep(3, 100);
    return "Invalid code";
  }

  unsigned long elapsed = millis() - sessions[index].entryTime;
  float minutes = elapsed / 60000.0;

  float bill = minutes * RATE_PER_MINUTE;
  if (bill < MINIMUM_CHARGE) bill = MINIMUM_CHARGE;

  int slot = sessions[index].slot;

  totalVehicles++;
  totalRevenue += bill;
  savePersistentData();

  clearSession(index);

  // FIX 1: slot is NOT forced free here; the sensor frees it.

  updateSlotLEDs();
  updateOLED();
  beep(2, 100);

  String msg =
      "Paid<br>"
      "Slot: S" + String(slot + 1) +
      "<br>Time: " + String(minutes, 1) + " min" +
      "<br>Rate: Rs " + String(RATE_PER_MINUTE, 0) + "/min" +
      "<br><b>Total: Rs " + String(bill, 2) + "</b>";

  return msg;
}


// ============================================================
// API HANDLERS
// ============================================================

bool authorized() {
  return server.hasArg("pass") && server.arg("pass") == watchmanPassword;
}

void handleGenerate() {
  if (!authorized()) {
    server.send(403, "text/plain", "Unauthorized");
    return;
  }

  if (availableCount() == 0) {
    beep(3, 120);
    server.send(409, "text/plain", "PARKING FULL - No slot available");
    return;
  }

  int generatedCode, assignedSlot;

  if (!generateCode(generatedCode, assignedSlot)) {
    server.send(500, "text/plain", "Could not create parking session");
    return;
  }

  String response =
      "Code: " + String(generatedCode) +
      "\nAssigned Slot: S" + String(assignedSlot + 1) +
      "\nDriver: please go to S" + String(assignedSlot + 1);

  server.send(200, "text/plain", response);
}

// FIX 6: lockout after repeated wrong codes
void handleExit() {
  if (millis() < lockUntil) {
    server.send(429, "application/json",
      "{\"message\":\"Too many wrong attempts. Try again in a few seconds.\"}");
    return;
  }

  if (!server.hasArg("code")) {
    server.send(400, "application/json",
      "{\"message\":\"Please enter your 6-digit code\"}");
    return;
  }

  int code = server.arg("code").toInt();
  String msg = vehicleExit(code);

  if (msg == "Invalid code") {
    failedAttempts++;
    if (failedAttempts >= MAX_FAILED) {
      lockUntil = millis() + LOCKOUT_MS;
      failedAttempts = 0;
    }
  } else {
    failedAttempts = 0;
  }

  msg.replace("\"", "\\\"");
  server.send(200, "application/json", "{\"message\":\"" + msg + "\"}");
}

void handleStatus() {
  String json = "{\"slots\":[";

  for (int i = 0; i < 3; i++) {
    bool assigned = sessionForSlot(i);
    json += "{\"occupied\":";
    json += slots[i].occupied ? "true" : "false";
    json += ",\"assigned\":";
    json += assigned ? "true" : "false";
    json += ",\"available\":";
    json += (!slots[i].occupied && !assigned) ? "true" : "false";
    json += "}";
    if (i < 2) json += ",";
  }

  json += "],\"available\":";
  json += availableCount();
  json += ",\"totalVehicles\":";
  json += totalVehicles;
  json += ",\"revenue\":";
  json += String(totalRevenue, 2);
  json += ",\"gateOpen\":";
  json += gateOpen ? "true" : "false";
  json += "}";

  server.send(200, "application/json", json);
}

void handleGate() {
  if (!authorized()) {
    server.send(403, "text/plain", "Unauthorized");
    return;
  }

  String action = server.arg("action");

  if (action == "open") {
    gateServo.write(90);
    gateOpen = true;
    beep(1, 80);
  } else if (action == "close") {
    gateServo.write(0);
    gateOpen = false;
    beep(1, 80);
  }

  updateOLED();
  server.send(200, "text/plain", "OK");
}


// ============================================================
// USER PAGE (no revenue shown to customers)
// ============================================================

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Smart Parking</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:#f0f4f8;min-height:100vh;padding:1.5rem 1rem;color:#1a1a1a}
.page{max-width:500px;margin:0 auto}
.header{margin-bottom:1.2rem}
.logo{display:flex;align-items:center;gap:10px}
.logo-icon{width:34px;height:34px;background:#185FA5;border-radius:8px;display:flex;align-items:center;justify-content:center;color:#fff;font-weight:bold}
.logo-text{font-size:20px;font-weight:700}
.live{font-size:12px;color:#666;margin-top:4px}
.dot{display:inline-block;width:6px;height:6px;background:#1D9E75;border-radius:50%;margin-right:4px}
.notice{font-size:12px;padding:10px;border-radius:8px;background:#fff;border:1px solid #e0e0e0;margin-bottom:10px;font-weight:600}
.slots-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:1rem}
.slot-card{border-radius:12px;border:1px solid #e0e0e0;padding:14px 6px;text-align:center;background:#fff}
.slot-card.free{border-color:#5DCAA5;background:#E1F5EE}
.slot-card.occupied{border-color:#F0997B;background:#FAECE7}
.slot-card.assigned{border-color:#85B7EB;background:#E6F1FB}
.slot-num{font-size:10px;font-weight:700;margin-bottom:8px;color:#666}
.slot-label{font-size:11px;font-weight:700}
.stats-row{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;margin-bottom:1rem}
.stat{background:#fff;border-radius:10px;padding:12px 10px;border:1px solid #e0e0e0}
.stat-label{font-size:10px;color:#888;margin-bottom:4px}
.stat-val{font-size:17px;font-weight:700}
.card{background:#fff;border:1px solid #e0e0e0;border-radius:12px;padding:1.1rem;margin-bottom:1rem}
.card-title{font-size:13px;font-weight:700;margin-bottom:12px}
.input-row{display:flex;gap:8px}
.code-input{flex:1;padding:10px 12px;border:1px solid #ddd;border-radius:8px;font-size:15px;letter-spacing:2px;outline:none}
.btn-primary{padding:10px 16px;border-radius:8px;border:none;background:#185FA5;color:#fff;font-size:13px;font-weight:700;cursor:pointer}
.bill-box{margin-top:12px;border-top:1px solid #eee;padding-top:12px;display:none;font-size:13px;line-height:1.7}
.bill-box.visible{display:block}
.tariff{font-size:12px;color:#666;line-height:1.6}
</style>
<script>
function update(){
  fetch('/status').then(r=>r.json()).then(d=>{
    for(let i=0;i<3;i++){
      let c=document.getElementById('s'+i),l=document.getElementById('l'+i);
      if(d.slots[i].occupied){c.className='slot-card occupied';l.textContent='Occupied';}
      else if(d.slots[i].assigned){c.className='slot-card assigned';l.textContent='Assigned';}
      else{c.className='slot-card free';l.textContent='Free';}
    }
    document.getElementById('avail').textContent=d.available+' / 3';
    document.getElementById('vehicles').textContent=d.totalVehicles;
    document.getElementById('notice').textContent=
      d.available===0?'PARKING FULL':'Parking available - entry open';
  }).catch(()=>{document.getElementById('notice').textContent='Connection error';});
}
function exitVehicle(){
  let code=document.getElementById('code').value;
  let box=document.getElementById('billBox');
  if(!code){box.innerHTML='Please enter your 6-digit code';box.className='bill-box visible';return;}
  fetch('/exit?code='+encodeURIComponent(code)).then(r=>r.json()).then(d=>{
    box.innerHTML=d.message;box.className='bill-box visible';
    document.getElementById('code').value='';update();
  }).catch(()=>{box.innerHTML='Unable to contact parking server';box.className='bill-box visible';});
}
setInterval(update,2000);
window.onload=update;
</script></head>
<body><div class='page'>
<div class='header'>
<div class='logo'><div class='logo-icon'>P</div><div class='logo-text'>Smart Parking</div></div>
<div class='live'><span class='dot'></span>Live availability</div>
</div>
<div class='notice' id='notice'>Checking parking status...</div>
<div class='slots-grid'>
<div id='s0' class='slot-card free'><div class='slot-num'>SLOT 1</div><div class='slot-label' id='l0'>Loading</div></div>
<div id='s1' class='slot-card free'><div class='slot-num'>SLOT 2</div><div class='slot-label' id='l1'>Loading</div></div>
<div id='s2' class='slot-card free'><div class='slot-num'>SLOT 3</div><div class='slot-label' id='l2'>Loading</div></div>
</div>
<div class='stats-row'>
<div class='stat'><div class='stat-label'>Available</div><div class='stat-val' id='avail'>- / 3</div></div>
<div class='stat'><div class='stat-label'>Vehicles served</div><div class='stat-val' id='vehicles'>0</div></div>
</div>
<div class='card'>
<div class='card-title'>Exit &amp; Pay</div>
<div class='input-row'>
<input class='code-input' id='code' placeholder='6-digit code' maxlength='6' inputmode='numeric'>
<button class='btn-primary' onclick='exitVehicle()'>Submit</button>
</div>
<div class='bill-box' id='billBox'></div>
</div>
<div class='card'>
<div class='card-title'>Tariff</div>
<div class='tariff'>Rs 1 per minute<br>Minimum charge: Rs 1<br>Please park in the slot given by the watchman.</div>
</div>
</div></body></html>
)rawliteral";

  server.send(200, "text/html", html);
}


// ============================================================
// WATCHMAN PANEL (FIX 7: password injected, not hardcoded)
// ============================================================

void handleWatchman() {

  if (authorized()) {

    String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Watchman Panel</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:#f0f4f8;min-height:100vh;padding:1.5rem 1rem}
.page{max-width:500px;margin:0 auto}
.title{font-size:22px;font-weight:700;margin-bottom:4px}
.sub{font-size:13px;color:#888;margin-bottom:1.2rem}
.card{background:#fff;border:1px solid #e0e0e0;border-radius:12px;padding:1.1rem;margin-bottom:1rem}
.card-title{font-size:11px;font-weight:700;color:#888;text-transform:uppercase;letter-spacing:.7px;margin-bottom:12px}
button{padding:9px 14px;border-radius:8px;border:1px solid #ccc;font-weight:700;cursor:pointer}
.open{background:#E1F5EE;color:#085041;border-color:#5DCAA5}
.close{background:#FAECE7;color:#712B13;border-color:#F0997B}
.generate{background:#185FA5;color:#fff;border-color:#185FA5;width:100%}
.slot{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid #eee}
.slot:last-child{border-bottom:none}
.badge{padding:4px 9px;border-radius:20px;font-size:11px;font-weight:700}
.free{background:#E1F5EE;color:#085041}
.occ{background:#FAECE7;color:#712B13}
.assigned{background:#E6F1FB;color:#0C447C}
.stats{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.stat{background:#f8f8f8;padding:10px;border-radius:8px}
.small{font-size:11px;color:#888}
.big{font-size:18px;font-weight:700;margin-top:3px}
</style>
<script>
var P='%PASS%';
function gen(){
  fetch('/generate?pass='+encodeURIComponent(P)).then(async r=>{
    alert(await r.text());update();
  });
}
function gate(a){
  fetch('/gate?action='+a+'&pass='+encodeURIComponent(P)).then(()=>update());
}
function update(){
  fetch('/status').then(r=>r.json()).then(d=>{
    for(let i=0;i<3;i++){
      let b=document.getElementById('b'+i),x=document.getElementById('x'+i);
      if(d.slots[i].occupied){b.className='badge occ';b.textContent='Occupied';x.textContent=' Vehicle detected';}
      else if(d.slots[i].assigned){b.className='badge assigned';b.textContent='Assigned';x.textContent=' Waiting for vehicle';}
      else{b.className='badge free';b.textContent='Free';x.textContent=' No vehicle detected';}
    }
    document.getElementById('av').textContent=d.available+'/3';
    document.getElementById('rev').textContent='Rs '+Number(d.revenue).toFixed(0);
    document.getElementById('gate').textContent=d.gateOpen?'OPEN':'CLOSED';
  });
}
setInterval(update,2000);
window.onload=update;
</script></head>
<body><div class='page'>
<div class='title'>Watchman Control Panel</div>
<div class='sub'>Smart Parking System</div>
<div class='card'><div class='card-title'>Entry Code</div>
<button class='generate' onclick='gen()'>Generate Code + Assign Slot</button></div>
<div class='card'><div class='card-title'>Gate Control</div>
<div style='margin-bottom:10px'>Gate: <b id='gate'>CLOSED</b></div>
<button class='open' onclick="gate('open')">Open Gate</button>
<button class='close' onclick="gate('close')">Close Gate</button></div>
<div class='card'><div class='card-title'>Live Slots</div>
<div class='slot'><span>Slot 1<small id='x0'> Loading</small></span><span id='b0' class='badge free'>Free</span></div>
<div class='slot'><span>Slot 2<small id='x1'> Loading</small></span><span id='b1' class='badge free'>Free</span></div>
<div class='slot'><span>Slot 3<small id='x2'> Loading</small></span><span id='b2' class='badge free'>Free</span></div>
</div>
<div class='card'><div class='card-title'>Lifetime Statistics</div>
<div class='stats'>
<div class='stat'><div class='small'>Available</div><div class='big' id='av'>-</div></div>
<div class='stat'><div class='small'>Revenue</div><div class='big' id='rev'>Rs 0</div></div>
</div></div>
</div></body></html>
)rawliteral";

    html.replace("%PASS%", watchmanPassword);
    server.send(200, "text/html", html);
    return;
  }

  // ---------- Login page ----------
  String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Watchman Login</title>
<style>
body{font-family:Arial;background:#f0f4f8;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:1rem}
.card{background:#fff;border:1px solid #ddd;border-radius:12px;padding:2rem;width:100%;max-width:340px}
h2{margin-bottom:5px}
p{font-size:13px;color:#888;margin-bottom:1.5rem}
input{width:100%;padding:10px;border:1px solid #ddd;border-radius:8px;margin-bottom:12px}
button{width:100%;padding:10px;background:#185FA5;color:#fff;border:0;border-radius:8px;font-weight:700}
</style></head>
<body><div class='card'>
<h2>Watchman Login</h2><p>Smart Parking System</p>
<form action='/watchman' method='GET'>
<input name='pass' type='password' placeholder='Password'>
<button>Login</button>
</form></div></body></html>
)rawliteral";

  server.send(200, "text/html", html);
}


// ============================================================
// SENSOR HANDLING (FIX 8: one sensor per call, arrival debounce)
// ============================================================

void readSensor(int i) {
  long distance = getDistance(trigPins[i], echoPins[i]);

  if (distance > 0 && distance < threshold) {
    // Vehicle present
    emptyStart[i] = 0;

    if (!slots[i].occupied) {
      hitCount[i]++;

      if (hitCount[i] >= ARRIVE_READINGS) {
        slots[i].occupied = true;
        hitCount[i] = 0;

        adoptSlot(i);   // FIX 4

        beep(1, 60);
        Serial.print("Vehicle detected in Slot ");
        Serial.println(i + 1);
      }
    }
  }
  else {
    // No vehicle
    hitCount[i] = 0;

    if (slots[i].occupied) {
      if (emptyStart[i] == 0) emptyStart[i] = millis();

      if (millis() - emptyStart[i] > PARK_CONFIRM_TIME) {
        slots[i].occupied = false;
        emptyStart[i] = 0;
        Serial.print("Slot ");
        Serial.print(i + 1);
        Serial.println(" is now FREE");

        updateSlotLEDs();
        beep(2, 60);   // exit beep
      }
    }
  }
}


// ============================================================
// WI-FI HELPERS
// ============================================================

void printPortals() {
  Serial.println();
  Serial.println("========== PORTAL ADDRESSES ==========");

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();

    Serial.print("User Portal     : http://");
    Serial.println(ip);

    Serial.print("Watchman Portal : http://");
    Serial.print(ip);
    Serial.println("/watchman");

    Serial.print("Watchman Direct : http://");
    Serial.print(ip);
    Serial.print("/watchman?pass=");
    Serial.println(watchmanPassword);
  } else {
    Serial.println("Wi-Fi NOT connected - no IP address yet.");
    Serial.println("Check ssid/password and that the hotspot is on (2.4 GHz).");
  }

  Serial.println("======================================");
  Serial.println();
}

void wifiDiagnostics() {
  Serial.println("\n--- WiFi diagnostics ---");
  Serial.print("SSID in code : [");
  Serial.print(ssid);
  Serial.println("]");
  Serial.print("Status code  : ");
  Serial.println(WiFi.status());
  Serial.println("(1=SSID not found, 4=connect failed, 6=wrong password/disconnected)");

  Serial.println("Scanning for networks...");
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    Serial.println("No networks found at all.");
  }
  bool found = false;
  for (int i = 0; i < n; i++) {
    Serial.print(i + 1);
    Serial.print(") ");
    Serial.print(WiFi.SSID(i));
    Serial.print("  RSSI: ");
    Serial.print(WiFi.RSSI(i));
    Serial.print("  Ch: ");
    Serial.println(WiFi.channel(i));
    if (WiFi.SSID(i) == String(ssid)) found = true;
  }
  if (n > 0) {
    Serial.println(found ? ">> Your SSID IS visible. Problem is likely the password/security (use WPA2)."
                         : ">> Your SSID is NOT in the list. Check spelling, 2.4 GHz band, hotspot ON.");
  }
  WiFi.scanDelete();
  Serial.println("------------------------\n");
}


// ============================================================
// SETUP
// ============================================================
uint8_t findOLEDAddress() {
  uint8_t addrs[2] = {0x3C, 0x3D};
  for (int i = 0; i < 2; i++) {
    Wire.beginTransmission(addrs[i]);
    if (Wire.endTransmission() == 0) return addrs[i];
  }
  return 0;
}

// ============================================================
// THINGSPEAK
// ============================================================

void updateThingSpeak() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ThingSpeak: WiFi not connected");
    return;
  }

  int occupied = 0;

  for (int i = 0; i < 3; i++) {
    if (slots[i].occupied) {
      occupied++;
    }
  }

  int slot1 = slots[0].occupied ? 1 : 0;
  int slot2 = slots[1].occupied ? 1 : 0;
  int slot3 = slots[2].occupied ? 1 : 0;

  int gateStatus = gateOpen ? 1 : 0;

  String url = "https://api.thingspeak.com/update?api_key=";
  url += thingSpeakAPIKey;

  url += "&field1=";
  url += availableCount();

  url += "&field2=";
  url += occupied;

  url += "&field3=";
  url += slot1;

  url += "&field4=";
  url += slot2;

  url += "&field5=";
  url += slot3;

  url += "&field6=";
  url += totalVehicles;

  url += "&field7=";
  url += String(totalRevenue, 2);

  url += "&field8=";
  url += gateStatus;

  HTTPClient http;

  http.begin(url);

  int httpCode = http.GET();

  if (httpCode > 0) {

    String response = http.getString();

    Serial.print("ThingSpeak HTTP: ");
    Serial.println(httpCode);

    Serial.print("ThingSpeak response: ");
    Serial.println(response);

  } else {

    Serial.print("ThingSpeak error: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}


void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 3; i++) {
    pinMode(trigPins[i], OUTPUT);
    pinMode(echoPins[i], INPUT);
    pinMode(greenLED[i], OUTPUT);
    digitalWrite(greenLED[i], HIGH);   // green ON at start (slot free)
    slots[i].occupied = false;
    clearSession(i);
  }

  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  // ---------- OLED ----------
  Wire.begin(21, 22);
  uint8_t addr = findOLEDAddress();

  if (addr == 0) {
    Serial.println("No OLED found on I2C. Check SDA=21, SCL=22, power.");
    oledOK = false;
  } else {
    Serial.print("OLED found at 0x");
    Serial.println(addr, HEX);
    oledOK = display.begin(SSD1306_SWITCHCAPVCC, addr);
    if (oledOK) showOLEDMessage("SMART PARKING", "Starting...");
  }
  delay(500);

  loadPersistentData();
  updateSlotLEDs();

  gateServo.attach(servoPin);
  gateServo.write(0);
  gateOpen = false;

  randomSeed(micros());

  // ---------- Wi-Fi ----------
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(200);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  Serial.println("Connecting to WiFi...");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    showOLEDMessage("WiFi Connected", WiFi.localIP().toString());
  } else {
    showOLEDMessage("WiFi FAILED", "See Serial Mon.");
    wifiDiagnostics();
  }

  printPortals();
  delay(2500);

  // ---------- Routes ----------
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/exit", handleExit);
  server.on("/watchman", handleWatchman);
  server.on("/gate", handleGate);
  server.on("/generate", handleGenerate);
  server.begin();

  updateOLED();
  Serial.println("Smart Parking System Ready.");
}


// ============================================================
// LOOP
// ============================================================

void loop() {
  server.handleClient();   // runs every pass

  // Print portal addresses when Wi-Fi connects/reconnects; retry if down
  static bool wasConnected = false;
  static unsigned long lastRetry = 0;
  bool nowConnected = (WiFi.status() == WL_CONNECTED);

  if (nowConnected && !wasConnected) {
    printPortals();
  }
  if (!nowConnected && millis() - lastRetry > 15000) {
    lastRetry = millis();
    Serial.println("Wi-Fi down, retrying...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
  }
  wasConnected = nowConnected;

  // Read ONE sensor every SENSOR_INTERVAL ms (no long blocking)
  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = millis();
    readSensor(currentSensor);
    currentSensor = (currentSensor + 1) % 3;
  }

  maintainSessions();
  updateSlotLEDs();

  if (millis() - lastDisplayUpdate > 1000) {
    updateOLED();
    lastDisplayUpdate = millis();
  }

  // ---------- ThingSpeak update ----------
if (millis() - lastThingSpeakUpdate >= THINGSPEAK_INTERVAL) {
  lastThingSpeakUpdate = millis();
  updateThingSpeak();
}
}
