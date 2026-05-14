#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"

#define DHTPIN 4
#define DHTTYPE DHT11
#define SDA_PIN 21
#define SCL_PIN 22

Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHTPIN, DHTTYPE);
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// UPDATED PACKET: Added msg array to stay compatible with your new nodes
typedef struct {
  char nodeId;
  float val1;   // Temperature
  float val2;   // Humidity
  bool isSOS;
  char msg[32]; // Custom message for merged data
} Packet;

Packet myData;

void setup() {
  Serial.begin(115200);
  dht.begin();
  
  // OLED Init
  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) return;

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastAddress, 6);
  peer.channel = 1;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  
  myData.nodeId = 'D'; 
}

void loop() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  // Generate random dummy PPM (e.g., between 300 and 500 for normal air)
  int dummyPpm = random(300, 501); 

  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  myData.val1 = t;
  myData.val2 = h;

  display.clearDisplay();

  // THRESHOLD LOGIC & DISPLAY
  if (t > 40.0 || h > 90.0) {
    myData.isSOS = true;
    strcpy(myData.msg, "OCEAN/GAS ALERT"); // Message for the Monitor
    
    // ALERT SCREEN
    display.setTextSize(2);
    display.setCursor(0, 5);
    display.println("!! ALERT !!");
    display.setTextSize(1);
    display.setCursor(0, 30);
    display.print("Temp: "); display.println(t);
    display.print("Hum:  "); display.println(h);
    display.print("PPM:  "); display.println(dummyPpm);
  } 
  else {
    myData.isSOS = false;
    strcpy(myData.msg, "Environment OK");
    
    // NORMAL SCREEN
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("-- HYBRID NODE D --");
    display.setCursor(0, 20);
    display.print("Temp: "); display.print(t); display.println(" C");
    display.setCursor(0, 35);
    display.print("Hum:  "); display.print(h); display.println(" %");
    display.setCursor(0, 50);
    display.print("Gas:  "); display.print(dummyPpm); display.println(" PPM");
  }

  display.display();

  esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
  delay(2000);
}