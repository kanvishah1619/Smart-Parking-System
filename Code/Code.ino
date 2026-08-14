#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// ===== WiFi =====
const char* ssid = "AndroidAP";
const char* password = "uuxh4044";

// ===== Watchman Password =====
const char* watchmanPassword = "watchman123";

// ===== Ultrasonic Pins =====
const int trigPins[3] = {5, 18, 19};
const int echoPins[3] = {23, 22, 4};

// ===== Servo =====
Servo gateServo;
const int servoPin = 15;

// ===== Threshold =====
const int threshold = 30;

// ===== Server =====
WebServer server(80);

// ===== Code + Timer =====
int currentCode = 0;
unsigned long entryTime = 0;

// ===== Slot =====
struct Slot {
  bool occupied;
};
Slot slots[3];

unsigned long emptyStart[3] = {0,0,0};

// ===== Ultrasonic =====
long getDistance(int trigPin, int echoPin){
  digitalWrite(trigPin, LOW); delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
  if(duration==0) return 999;
  return duration * 0.034 / 2;
}

// ===== Generate Code =====
void generateCode(){
  currentCode = random(100000,999999);
  entryTime = millis();
  Serial.println("Generated Code: " + String(currentCode));
}

// ===== Exit =====
String vehicleExit(int code){
  if(code == currentCode){
    float minutes = (millis()-entryTime)/60000.0;
    float bill = minutes * 1;

    currentCode = 0;

    return "Paid<br>Time: "+String(minutes,1)+" min<br>Rs "+String(bill,2);
  }
  return "Invalid Code";
}

// ===== API =====
void handleGenerate(){
  if(!server.hasArg("pass") || server.arg("pass")!=watchmanPassword){
    server.send(403,"text/plain","Unauthorized"); return;
  }
  generateCode();
  server.send(200,"text/plain","Code: "+String(currentCode));
}

void handleExit(){
  int code = server.arg("code").toInt();
  String msg = vehicleExit(code);
  server.send(200,"application/json","{\"message\":\""+msg+"\"}");
}

void handleStatus(){
  String json="[";
  for(int i=0;i<3;i++){
    json += slots[i].occupied ? "false":"true";
    if(i<2) json+=",";
  }
  json+="]";
  server.send(200,"application/json",json);
}

void handleGate(){
  if(!server.hasArg("pass") || server.arg("pass")!=watchmanPassword){
    server.send(403,"text/plain","Unauthorized"); return;
  }
  String a = server.arg("action");
  if(a=="open") gateServo.write(90);
  if(a=="close") gateServo.write(0);
  server.send(200,"text/plain","OK");
}

// ===== USER PAGE (UNCHANGED) =====
void handleRoot(){
  // ⚠️ KEEP YOUR ORIGINAL UI HERE EXACTLY (no change)
  String html = "<!DOCTYPE html><html><head><title>Smart Parking</title>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>";
  html += "*{box-sizing:border-box;margin:0;padding:0}";
  html += "body{font-family:Arial,sans-serif;background:#f0f4f8;min-height:100vh;padding:1.5rem 1rem}";
  html += ".page{max-width:420px;margin:0 auto}";
  html += ".header{margin-bottom:1.5rem}";
  html += ".logo{display:flex;align-items:center;gap:10px;margin-bottom:4px}";
  html += ".logo-icon{width:32px;height:32px;background:#185FA5;border-radius:8px;display:flex;align-items:center;justify-content:center}";
  html += ".logo-icon svg{width:18px;height:18px;fill:white}";
  html += ".logo-text{font-size:20px;font-weight:600;color:#1a1a1a}";
  html += ".live{display:flex;align-items:center;gap:5px;font-size:12px;color:#666}";
  html += ".dot{width:6px;height:6px;background:#1D9E75;border-radius:50%;animation:pulse 1.5s infinite}";
  html += "@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}";
  html += ".slots-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:1rem}";
  html += ".slot-card{border-radius:12px;border:1px solid #e0e0e0;padding:14px 8px;text-align:center;background:#fff}";
  html += ".slot-card.free{border-color:#5DCAA5;background:#E1F5EE}";
  html += ".slot-card.occupied{border-color:#F0997B;background:#FAECE7}";
  html += ".slot-num{font-size:10px;font-weight:600;letter-spacing:.5px;margin-bottom:6px;color:#888}";
  html += ".slot-card.free .slot-num{color:#0F6E56}";
  html += ".slot-card.occupied .slot-num{color:#993C1D}";
  html += ".slot-circle{width:32px;height:32px;border-radius:50%;margin:0 auto 6px;display:flex;align-items:center;justify-content:center}";
  html += ".slot-card.free .slot-circle{background:#9FE1CB}";
  html += ".slot-card.occupied .slot-circle{background:#F5C4B3}";
  html += ".slot-circle svg{width:14px;height:14px}";
  html += ".slot-card.free .slot-circle svg{fill:#085041}";
  html += ".slot-card.occupied .slot-circle svg{fill:#712B13}";
  html += ".slot-label{font-size:11px;font-weight:600}";
  html += ".slot-card.free .slot-label{color:#085041}";
  html += ".slot-card.occupied .slot-label{color:#712B13}";
  html += ".stats-row{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:1rem}";
  html += ".stat{background:#fff;border-radius:10px;padding:12px 14px;border:1px solid #e0e0e0}";
  html += ".stat-label{font-size:11px;color:#888;margin-bottom:4px}";
  html += ".stat-val{font-size:20px;font-weight:600;color:#1a1a1a}";
  html += ".card{background:#fff;border:1px solid #e0e0e0;border-radius:12px;padding:1.25rem;margin-bottom:1rem}";
  html += ".card-title{font-size:13px;font-weight:600;color:#1a1a1a;margin-bottom:12px}";
  html += ".input-row{display:flex;gap:8px}";
  html += ".code-input{flex:1;padding:9px 12px;border:1px solid #ddd;border-radius:8px;font-size:15px;letter-spacing:2px;outline:none;color:#1a1a1a}";
  html += ".code-input:focus{border-color:#185FA5}";
  html += ".btn-primary{padding:9px 18px;border-radius:8px;border:none;background:#185FA5;color:#fff;font-size:13px;font-weight:600;cursor:pointer}";
  html += ".btn-primary:active{opacity:.85}";
  html += ".bill-box{margin-top:12px;border-top:1px solid #eee;padding-top:12px;display:none}";
  html += ".bill-box.visible{display:block}";
  html += ".bill-badge{display:inline-block;background:#E1F5EE;color:#085041;font-size:11px;font-weight:600;padding:3px 10px;border-radius:99px;margin-bottom:10px}";
  html += ".bill-row{display:flex;justify-content:space-between;font-size:13px;padding:3px 0;color:#666}";
  html += ".bill-row.total{font-size:15px;font-weight:600;color:#1a1a1a;border-top:1px solid #eee;margin-top:6px;padding-top:8px}";
  html += ".bill-hint{font-size:12px;color:#888;margin-top:10px}";
  html += ".bill-err{color:#A32D2D;font-size:13px;margin-top:10px}";
  html += "</style>";

  html += "<script>";
  html += "function update(){";
  html += "fetch('/status').then(r=>r.json()).then(d=>{";
  html += "var labels=['FREE','OCCUPIED'];";
  html += "for(var i=0;i<3;i++){";
  html += "var c=document.getElementById('s'+i);";
  html += "var lbl=document.getElementById('l'+i);";
  html += "if(d[i]){c.className='slot-card free';lbl.textContent='Free';}";
  html += "else{c.className='slot-card occupied';lbl.textContent='Occupied';}";
  html += "}";
  html += "var free=d.filter(function(x){return x;}).length;";
  html += "document.getElementById('avail').textContent=free+' / 3';";
  html += "});}";

  html += "function exitVehicle(){";
  html += "var code=document.getElementById('code').value;";
  html += "fetch('/exit?code='+code).then(r=>r.json()).then(function(data){";
  html += "var box=document.getElementById('billBox');";
  html += "box.innerHTML=data.message;";
  html += "box.className='bill-box visible';";
  html += "});}";

  html += "setInterval(update,2000);";
  html += "window.onload=update;";
  html += "</script>";

  html += "</head><body><div class='page'>";

  html += "<div class='header'>";
  html += "<div class='logo'>";
  html += "<div class='logo-icon'><svg viewBox='0 0 24 24'><path d='M5 17H3a2 2 0 01-2-2V5a2 2 0 012-2h11a2 2 0 012 2v3m-4 12H23v-6h-3v-4h-2l-1 4h-5l-1-4h-2v4h-3v6z'/></svg></div>";
  html += "<span class='logo-text'>Smart Parking</span></div>";
  html += "<div class='live'><div class='dot'></div>Live availability</div>";
  html += "</div>";

  html += "<div class='slots-grid'>";
  for(int i=0;i<3;i++){
    html += "<div id='s"+String(i)+"' class='slot-card free'>";
    html += "<div class='slot-num'>SLOT "+String(i+1)+"</div>";
    html += "<div class='slot-circle'><svg viewBox='0 0 24 24'><path d='M9 17v-2m3 2v-4m3 4v-6m2 10H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z'/></svg></div>";
    html += "<div class='slot-label' id='l"+String(i)+"'>Loading</div>";
    html += "</div>";
  }
  html += "</div>";

  html += "<div class='stats-row'>";
  html += "<div class='stat'><div class='stat-label'>Available</div><div class='stat-val' id='avail'>- / 3</div></div>";
  html += "<div class='stat'><div class='stat-label'>Rate</div><div class='stat-val'>Rs 1/min</div></div>";
  html += "</div>";

  html += "<div class='card'><div class='card-title'>Exit &amp; Pay</div>";
  html += "<div class='input-row'>";
  html += "<input class='code-input' id='code' placeholder='Enter 6-digit code' maxlength='6' type='number'>";
  html += "<button class='btn-primary' onclick='exitVehicle()'>Submit</button>";
  html += "</div>";
  html += "<div class='bill-box' id='billBox'></div>";
  html += "</div>";

  html += "</div></body></html>";

  server.send(200,"text/html",html);
}

// ===== WATCHMAN =====
void handleWatchman() {
  if (server.hasArg("pass") && server.arg("pass") == watchmanPassword) {

    String html = "<!DOCTYPE html><html><head><title>Watchman Panel</title>";
    html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<style>";
    html += "*{box-sizing:border-box;margin:0;padding:0}";
    html += "body{font-family:Arial,sans-serif;background:#f0f4f8;min-height:100vh;padding:1.5rem 1rem}";
    html += ".page{max-width:420px;margin:0 auto}";
    html += ".badge{display:inline-flex;align-items:center;gap:5px;background:#E6F1FB;color:#0C447C;font-size:11px;font-weight:600;padding:4px 10px;border-radius:99px;margin-bottom:8px}";
    html += ".badge-dot{width:5px;height:5px;background:#185FA5;border-radius:50%}";
    html += ".page-title{font-size:22px;font-weight:700;color:#1a1a1a}";
    html += ".page-sub{font-size:13px;color:#888;margin-top:2px;margin-bottom:1.5rem}";
    html += ".card{background:#fff;border:1px solid #e0e0e0;border-radius:12px;padding:1.25rem;margin-bottom:1rem}";
    html += ".card-title{font-size:10px;font-weight:700;color:#aaa;text-transform:uppercase;letter-spacing:.8px;margin-bottom:14px}";
    html += ".gate-row{display:flex;align-items:center;justify-content:space-between}";
    html += ".gate-left{display:flex;align-items:center;gap:10px}";
    html += ".gate-ind{width:10px;height:10px;border-radius:50%;background:#E24B4A;flex-shrink:0}";
    html += ".gate-ind.open{background:#1D9E75}";
    html += ".gate-text{font-size:16px;font-weight:600;color:#1a1a1a}";
    html += ".gate-sub{font-size:12px;color:#888;margin-top:1px}";
    html += ".gate-btns{display:flex;gap:8px}";
    html += ".gbtn{padding:8px 18px;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;border:1px solid}";
    html += ".gbtn:active{opacity:.75}";
    html += ".gbtn-open{background:#E1F5EE;color:#085041;border-color:#5DCAA5}";
    html += ".gbtn-close{background:#FAECE7;color:#712B13;border-color:#F0997B}";
    html += ".slot-list{display:flex;flex-direction:column}";
    html += ".slot-item{display:flex;align-items:center;gap:12px;padding:12px 0;border-bottom:1px solid #f0f0f0}";
    html += ".slot-item:last-child{border-bottom:none;padding-bottom:0}";
    html += ".slot-item:first-child{padding-top:0}";
    html += ".slot-box{width:38px;height:38px;border-radius:8px;display:flex;align-items:center;justify-content:center;font-size:13px;font-weight:700;flex-shrink:0}";
    html += ".free-box{background:#E1F5EE;color:#085041}";
    html += ".occ-box{background:#FAECE7;color:#712B13}";
    html += ".slot-info{flex:1}";
    html += ".slot-name{font-size:14px;font-weight:600;color:#1a1a1a}";
    html += ".slot-detail{font-size:12px;color:#888;margin-top:1px}";
    html += ".pill{font-size:11px;font-weight:600;padding:3px 10px;border-radius:99px}";
    html += ".pill-free{background:#E1F5EE;color:#085041}";
    html += ".pill-occ{background:#FAECE7;color:#712B13}";
    html += "</style>";

    html += "<script>";

    // ===== Gate =====
    html += "function setGate(open){";
    html += "var action=open?'open':'close';";
    html += "fetch('/gate?action='+action+'&pass=watchman123').then(function(){";
    html += "var ind=document.getElementById('gateInd');";
    html += "var txt=document.getElementById('gateText');";
    html += "var sub=document.getElementById('gateSub');";
    html += "if(open){ind.className='gate-ind open';txt.textContent='Open';sub.textContent='Gate is open';}";
    html += "else{ind.className='gate-ind';txt.textContent='Closed';sub.textContent='Gate is shut';}";
    html += "});}";

    // =====  GENERATE CODE FUNCTION =====
    html += "function generateCode(){";
    html += "fetch('/generate?pass=watchman123').then(r=>r.text()).then(function(d){";
    html += "alert(d);";
    html += "});}";
    
    // ===== Slots =====
    html += "function updateSlots(){";
    html += "fetch('/status').then(r=>r.json()).then(function(d){";
    html += "for(var i=0;i<3;i++){";
    html += "var box=document.getElementById('wb'+i);";
    html += "var det=document.getElementById('wd'+i);";
    html += "var pill=document.getElementById('wp'+i);";
    html += "if(d[i]){box.className='slot-box free-box';box.textContent='S'+(i+1);det.textContent='No vehicle detected';pill.className='pill pill-free';pill.textContent='Free';}";
    html += "else{box.className='slot-box occ-box';box.textContent='S'+(i+1);det.textContent='Occupied';pill.className='pill pill-occ';pill.textContent='Occupied';}";
    html += "}});}";

    html += "setInterval(updateSlots,2000);";
    html += "window.onload=updateSlots;";
    html += "</script>";

    html += "</head><body><div class='page'>";

    html += "<div class='badge'><div class='badge-dot'></div>Watchman access</div>";
    html += "<div class='page-title'>Control Panel</div>";
    html += "<div class='page-sub'>Smart Parking System</div>";

    // =====  GENERATE CODE CARD (ADDED CLEANLY) =====
    html += "<div class='card'><div class='card-title'>Entry</div>";
    html += "<button class='gbtn gbtn-open' onclick='generateCode()'>Generate Code</button>";
    html += "</div>";

    // ===== Gate =====
    html += "<div class='card'><div class='card-title'>Gate control</div>";
    html += "<div class='gate-row'>";
    html += "<div class='gate-left'>";
    html += "<div class='gate-ind' id='gateInd'></div>";
    html += "<div><div class='gate-text' id='gateText'>Closed</div><div class='gate-sub' id='gateSub'>Gate is shut</div></div>";
    html += "</div>";
    html += "<div class='gate-btns'>";
    html += "<button class='gbtn gbtn-open' onclick='setGate(true)'>Open</button>";
    html += "<button class='gbtn gbtn-close' onclick='setGate(false)'>Close</button>";
    html += "</div></div></div>";

    // ===== Slots =====
    html += "<div class='card'><div class='card-title'>Slot status</div>";
    html += "<div class='slot-list'>";
    for(int i=0;i<3;i++){
      html += "<div class='slot-item'>";
      html += "<div class='slot-box free-box' id='wb"+String(i)+"'>S"+String(i+1)+"</div>";
      html += "<div class='slot-info'>";
      html += "<div class='slot-name'>Slot "+String(i+1)+"</div>";
      html += "<div class='slot-detail' id='wd"+String(i)+"'>Loading...</div>";
      html += "</div>";
      html += "<span class='pill pill-free' id='wp"+String(i)+"'>Free</span>";
      html += "</div>";
    }
    html += "</div></div>";

    html += "</div></body></html>";

    server.send(200,"text/html",html);
    return;
  }

  // ===== LOGIN PAGE (UNCHANGED) =====
  String html = "<!DOCTYPE html><html><head><title>Watchman Login</title>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<style>";
  html += "*{box-sizing:border-box;margin:0;padding:0}";
  html += "body{font-family:Arial,sans-serif;background:#f0f4f8;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:1rem}";
  html += ".card{background:#fff;border:1px solid #e0e0e0;border-radius:12px;padding:2rem;width:100%;max-width:340px}";
  html += ".card-title{font-size:18px;font-weight:700;color:#1a1a1a;margin-bottom:4px}";
  html += ".card-sub{font-size:13px;color:#888;margin-bottom:1.5rem}";
  html += ".label{font-size:12px;font-weight:600;color:#555;margin-bottom:6px;display:block}";
  html += ".inp{width:100%;padding:10px 12px;border:1px solid #ddd;border-radius:8px;font-size:14px;outline:none;color:#1a1a1a;margin-bottom:1rem}";
  html += ".inp:focus{border-color:#185FA5}";
  html += ".btn{width:100%;padding:10px;border-radius:8px;border:none;background:#185FA5;color:#fff;font-size:14px;font-weight:600;cursor:pointer}";
  html += ".btn:active{opacity:.85}";
  html += "</style></head><body>";
  html += "<div class='card'>";
  html += "<div class='card-title'>Watchman Login</div>";
  html += "<div class='card-sub'>Smart Parking System</div>";
  html += "<form action='/watchman' method='GET'>";
  html += "<label class='label'>Password</label>";
  html += "<input class='inp' name='pass' type='password' placeholder='Enter password'>";
  html += "<button class='btn' type='submit'>Login</button>";
  html += "</form></div>";
  html += "</body></html>";

  server.send(200,"text/html",html);
}

// ===== SETUP =====
void setup(){
  Serial.begin(115200);

  for(int i=0;i<3;i++){
    pinMode(trigPins[i],OUTPUT);
    pinMode(echoPins[i],INPUT);
    slots[i].occupied=false;
  }

  gateServo.attach(servoPin);

  WiFi.begin(ssid,password);

  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  
  // SHOW URLs CLEARLY
  Serial.print("User Page: http://");
  Serial.println(WiFi.localIP());

  Serial.print("Watchman Page: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/watchman?pass=watchman123");

  server.on("/",handleRoot);
  server.on("/status",handleStatus);
  server.on("/exit",handleExit);
  server.on("/watchman",handleWatchman);
  server.on("/gate",handleGate);
  server.on("/generate",handleGenerate);

  server.begin();
}

// ===== LOOP =====
void loop(){
  server.handleClient();

  for(int i=0;i<3;i++){
    long d=getDistance(trigPins[i],echoPins[i]);

    if(d>0 && d<threshold){
      emptyStart[i]=0;
      slots[i].occupied=true;
    }
    else{
      if(slots[i].occupied){
        if(emptyStart[i]==0) emptyStart[i]=millis();

        if(millis()-emptyStart[i]>5000){
          slots[i].occupied=false;
          emptyStart[i]=0;
        }
      }
    }
    delay(100);
  }
}