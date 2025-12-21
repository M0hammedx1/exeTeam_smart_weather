/*
  Board: RED (Sender)
  Mac to send to: 84:1F:E8:1B:A3:78 
*/
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "DHT.h"

#define DHTPIN 4        
#define DHTTYPE DHT11   
#define RAIN_PIN 34     
#define LOCAL_LED 2     

DHT dht(DHTPIN, DHTTYPE);
int rainThreshold = 3500; 

uint8_t broadcastAddress[] = {0x84, 0x1F, 0xE8, 0x1B, 0xA3, 0x78};

typedef struct struct_message {
  float temp;
  float hum;
  bool isRaining;
} struct_message;

struct_message myData;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Optional: Debugging
}

void setup() {
  Serial.begin(115200);
  pinMode(RAIN_PIN, INPUT);
  pinMode(LOCAL_LED, OUTPUT);
  dht.begin();
  
  WiFi.mode(WIFI_STA);

  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 1;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    return;
  }
}

void loop() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int rainValue = analogRead(RAIN_PIN);
  
  if (isnan(h) || isnan(t)) { h = 0.0; t = 0.0; }

  bool rainDetected = (rainValue < rainThreshold);
  digitalWrite(LOCAL_LED, rainDetected ? HIGH : LOW);

  myData.temp = t;
  myData.hum = h;
  myData.isRaining = rainDetected;

  esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
  delay(2000); 
}