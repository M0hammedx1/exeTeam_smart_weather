/*
  Project: Smart Laundry System (.EXE TEAM)
  Board: Receiver + Web Server
*/

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

// Above num = RED, Below or Equal = BLUE
#define TEMP_THRESHOLD    30  

#define AUTO_SUNNY_ANGLE  5   
#define AUTO_RAIN_ANGLE   90  

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Hardware Pins
#define SERVO_PIN 13        
#define BUZZER_PIN 18       
#define OLED_BTN_PIN 4      

// RGB Pins
#define RGB_RED_PIN 14     
#define RGB_GREEN_PIN 32   
#define RGB_BLUE_PIN 33    

#define RED_CHANNEL 0
#define GREEN_CHANNEL 1
#define BLUE_CHANNEL 2

// Room LEDs (Web)
#define LED_ROOM_1_PIN 23  
#define LED_ROOM_2_PIN 27  
#define LED_ROOM_3_PIN 19  

Servo myServo;
WebServer server(80);

typedef struct struct_message {
  float temp;
  float hum;
  bool isRaining;
} struct_message;

struct_message incomingData;

bool manualMode = false;
int servoAngle = AUTO_SUNNY_ANGLE; 
unsigned long lastRecvTime = 0;
const unsigned long SIGNAL_TIMEOUT = 5000; 

unsigned long lastFlickerTime = 0;
bool flickerState = false;
bool rainAlarmTriggered = false; 
unsigned long rainStartTime = 0; 

bool oledOn = true;
int lastBtnState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <title>.EXE TEAM Dashboard</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Roboto:wght@400;700;900&display=swap');
    * { box-sizing: border-box; }
    body { font-family: 'Roboto', sans-serif; margin: 0; padding: 0; display: flex; flex-direction: column; align-items: center; min-height: 100vh; overflow-x: hidden; transition: background-color 1.5s ease; }
    body.sunny { background-color: #4aa1f3; color: #333; }
    body.rainy { background-color: #0d1b2a; color: #fff; }
    .container { width: 100%; max-width: 450px; padding: 80px 20px 20px 20px; z-index: 10; display: flex; flex-direction: column; align-items: center; }
    .card { background: rgba(255, 255, 255, 0.95); width: 100%; border-radius: 20px; padding: 25px; box-shadow: 0 10px 25px rgba(0,0,0,0.15); text-align: center; }
    h1 { margin: 0; font-size: 1.8rem; font-weight: 900; color: #333; letter-spacing: 1px; }
    .status { margin-top: 10px; font-size: 2.2rem; color: #666; font-weight: 900; text-transform: uppercase; letter-spacing: 2px; }
    .metrics { display: flex; justify-content: space-around; align-items: center; margin: 25px 0; background: #f4f4f4; border-radius: 15px; padding: 15px; }
    .val { font-size: 2.2rem; font-weight: 900; color: #222; }
    .lbl { font-size: 0.8rem; color: #777; font-weight: bold; }
    .control-group { width: 100%; text-align: left; margin-top: 10px; }
    .row { display: flex; justify-content: space-between; align-items: center; padding: 12px 0; border-bottom: 1px solid #eee; }
    .switch { position: relative; display: inline-block; width: 50px; height: 28px; }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ddd; transition: .3s; border-radius: 30px; }
    input:checked + .slider { background-color: #4aa1f3; }
    input:checked + .slider:before { transform: translateX(22px); }
    .slider:before { position: absolute; content: ""; height: 20px; width: 20px; left: 4px; bottom: 4px; background-color: white; transition: .3s; border-radius: 50%; }
    input[type=range] { width: 100%; height: 8px; background: #eee; border-radius: 5px; -webkit-appearance: none; margin-top: 10px; }
    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 22px; height: 22px; background: #4aa1f3; border-radius: 50%; cursor: pointer; }
  </style>
</head>
<body class="sunny">
  <div class="container">
    <div class="card">
      <h1>.EXE TEAM</h1>
      <div class="status" id="status">CLEAR</div>
      <div class="metrics">
        <div><span class="val"><span id="temp">--</span>C</span><br><span class="lbl">TEMP</span></div>
        <div style="width:1px; height:40px; background:#ddd;"></div>
        <div><span class="val"><span id="hum">--</span>%</span><br><span class="lbl">HUMIDITY</span></div>
      </div>
      <div class="control-group">
        <div class="row"><span>Manual Control:</span><label class="switch"><input type="checkbox" id="modeSwitch" onchange="toggleMode()"><span class="slider"></span></label></div>
        
        <div style="padding: 15px 0;"><span>Roof Angle</span><input type="range" min="10" max="165" value="10" id="servoSlider" oninput="updateServo(this.value)" disabled></div>
        
        <div class="row"><span>Room 1</span><label class="switch"><input type="checkbox" onchange="toggleLed(1, this.checked)"><span class="slider"></span></label></div>
        <div class="row"><span>Room 2</span><label class="switch"><input type="checkbox" onchange="toggleLed(2, this.checked)"><span class="slider"></span></label></div>
        <div class="row"><span>Room 3</span><label class="switch"><input type="checkbox" onchange="toggleLed(3, this.checked)"><span class="slider"></span></label></div>
      </div>
    </div>
  </div>
<script>
  function updateData() {
    fetch('/data').then(response => response.json()).then(data => {
      document.getElementById('temp').innerText = data.temp.toFixed(1);
      document.getElementById('hum').innerText = Math.round(data.hum);
      const statusEl = document.getElementById('status');
      if (data.rain) {
        document.body.className = "rainy";
        statusEl.innerText = "RAINING"; 
        statusEl.style.color = "#4aa1f3";
      } else {
        document.body.className = "sunny";
        statusEl.innerText = "CLEAR";
        statusEl.style.color = "#666";
      }
    });
  }
  function toggleMode() {
    const isManual = document.getElementById('modeSwitch').checked;
    document.getElementById('servoSlider').disabled = !isManual;
    fetch('/setMode?manual=' + (isManual ? 1 : 0));
  }
  function updateServo(val) { fetch('/servo?val=' + val); }
  function toggleLed(id, state) { fetch('/led?id=' + id + '&state=' + (state ? 1 : 0)); }
  setInterval(updateData, 2000);
</script>
</body>
</html>
)rawliteral";

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingDataBytes, int len) {
  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
  lastRecvTime = millis();
}

void handleRoot() { server.send(200, "text/html", index_html); }
void handleData() {
  String json = "{";
  json += "\"temp\":" + String(incomingData.temp) + ",";
  json += "\"hum\":" + String(incomingData.hum) + ",";
  json += "\"rain\":" + String(incomingData.isRaining ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}
void handleSetMode() { if (server.hasArg("manual")) manualMode = (server.arg("manual").toInt() == 1); server.send(200, "text/plain", "OK"); }
void handleServo() { if (server.hasArg("val")) { servoAngle = server.arg("val").toInt(); if (manualMode) myServo.write(servoAngle); } server.send(200, "text/plain", "OK"); }

void handleLed() {
  if (server.hasArg("id") && server.hasArg("state")) {
    int id = server.arg("id").toInt();
    bool state = (server.arg("state").toInt() == 1);
    
    if (id == 1) digitalWrite(LED_ROOM_1_PIN, state); 
    if (id == 2) digitalWrite(LED_ROOM_2_PIN, state); 
    if (id == 3) digitalWrite(LED_ROOM_3_PIN, state); 
  }
  server.send(200, "text/plain", "OK");
}

void centerText(String text, int y, int size) {
  int16_t x1, y1; uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2;
  display.setCursor(x, y);
  display.print(text);
}

void setRGB(int r, int g, int b) {
  ledcWrite(RED_CHANNEL, r);
  ledcWrite(GREEN_CHANNEL, g);
  ledcWrite(BLUE_CHANNEL, b);
}

void setup() {
  Serial.begin(115200);

  // Setup pins
  pinMode(BUZZER_PIN, OUTPUT); 
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(LED_ROOM_1_PIN, OUTPUT);
  pinMode(LED_ROOM_2_PIN, OUTPUT);
  pinMode(LED_ROOM_3_PIN, OUTPUT);
  
  pinMode(OLED_BTN_PIN, INPUT_PULLUP);

  // PWM setup
  ledcSetup(RED_CHANNEL, 5000, 8);
  ledcSetup(GREEN_CHANNEL, 5000, 8);
  ledcSetup(BLUE_CHANNEL, 5000, 8);
  
  ledcAttachPin(RGB_RED_PIN, RED_CHANNEL);
  ledcAttachPin(RGB_GREEN_PIN, GREEN_CHANNEL);
  ledcAttachPin(RGB_BLUE_PIN, BLUE_CHANNEL);

  ESP32PWM::allocateTimer(3); 
  myServo.setPeriodHertz(50);
  myServo.attach(SERVO_PIN, 500, 2400); 
  myServo.write(AUTO_SUNNY_ANGLE); 

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("OLED Failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  centerText(".EXE TEAM", 25, 2); 
  display.display();
  delay(2000); 
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Smart_Weather", "12345678", 1, 0, 5); 
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(OnDataRecv);

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/setMode", handleSetMode);
  server.on("/servo", handleServo);
  server.on("/led", handleLed);
  server.begin();
  
  Serial.println("System Ready.");
}

void loop() {
  server.handleClient();
  unsigned long currentTime = millis();

  // Button Debounce
  int reading = digitalRead(OLED_BTN_PIN);
  if (reading != lastBtnState) lastDebounceTime = currentTime;
  if ((currentTime - lastDebounceTime) > debounceDelay) {
    static int buttonState = HIGH;
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) oledOn = !oledOn;
    }
  }
  lastBtnState = reading;

  // Data Timeout
  if (currentTime - lastRecvTime > SIGNAL_TIMEOUT) {
    incomingData.isRaining = false; 
    incomingData.temp = 0;
    incomingData.hum = 0;
  }

  // Servo Logic
  if (!manualMode) {
    if (incomingData.isRaining) {
      myServo.write(AUTO_RAIN_ANGLE);    
      servoAngle = AUTO_RAIN_ANGLE;   
    } else {
      myServo.write(AUTO_SUNNY_ANGLE);    
      servoAngle = AUTO_SUNNY_ANGLE;
    }
  }

  // RGB Logic
  if (incomingData.temp == 0 && incomingData.hum == 0) {
      rainAlarmTriggered = false; 
      setRGB(0, 255, 0); // Standby Green
  } 
  else {
      // Threshold Switch Logic
      int targetRed = 0;
      int targetGreen = 0;
      int targetBlue = 0;

      if (incomingData.temp > TEMP_THRESHOLD) {
          targetRed = 255;  // Pure Red
      } else {
          targetBlue = 255; // Pure Blue
      }

      if (incomingData.isRaining) {
        // Alarm
        if (!rainAlarmTriggered) {
          digitalWrite(BUZZER_PIN, HIGH); delay(200); 
          digitalWrite(BUZZER_PIN, LOW); delay(100);
          digitalWrite(BUZZER_PIN, HIGH); delay(200); 
          digitalWrite(BUZZER_PIN, LOW);
          rainAlarmTriggered = true; 
          rainStartTime = millis(); 
        }

        // Flicker
        if (currentTime - lastFlickerTime > 100) { 
          flickerState = !flickerState;
          lastFlickerTime = currentTime;
        }

        if (flickerState) {
          setRGB(targetRed, targetGreen, targetBlue); 
        } else {
          setRGB(0, 0, 0); 
        }
        
      } else {
        rainAlarmTriggered = false; 
        setRGB(targetRed, targetGreen, targetBlue); 
      }
  }

  // Display Update
  display.clearDisplay();
  if (oledOn) {
    bool showBigAlert = false;
    if (incomingData.isRaining && (currentTime - rainStartTime < 3000)) showBigAlert = true;
  
    if (showBigAlert) {
       centerText("RAINING!", 25, 2); 
    } else {
       display.setTextSize(2); 
       display.setCursor(0, 10); display.print(incomingData.temp, 1); display.print(" C");
       display.setCursor(0, 35); display.print((int)incomingData.hum); display.print(" %");
       display.setTextSize(1); display.setCursor(80, 50);
       
       if (incomingData.temp == 0 && incomingData.hum == 0) {
         display.print("NO DATA");
       } else if (incomingData.isRaining) {
         display.print("RAIN"); 
       } else {
         display.print("CLEAR"); 
       }
    }
  }
  display.display();
  delay(10); 
}