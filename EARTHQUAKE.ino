#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

#define VIB_PIN 4        // Digital Input Pin
#define SDA_PIN 21
#define SCL_PIN 22

Adafruit_SSD1306 display(128, 64, &Wire, -1);
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct {
  char nodeId;
  float val1;  // Vibration State
  float val2;
  bool isSOS;
} Packet;

Packet myData;

void setup() {
  Serial.begin(115200);
  pinMode(VIB_PIN, INPUT); 
  
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
  
  myData.nodeId = 'C'; 
}

void loop() {
  int vibration = digitalRead(VIB_PIN);
  myData.val1 = vibration;
  myData.val2 = 0;

  display.clearDisplay();

  // THRESHOLD LOGIC & DISPLAY
  if (vibration == HIGH) {
    myData.isSOS = true;
    
    // ALERT SCREEN
    display.setTextSize(2);
    display.setCursor(5, 20);
    display.println("EARTHQUAKE");
    display.setTextSize(1);
    display.setCursor(10, 50);
    display.println("Sending Alert...");
  } 
  else {
    myData.isSOS = false;
    
    // NORMAL SCREEN
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("-- SEISMIC NODE C --");
    display.setCursor(0, 30);
    display.println("Vibration: None");
    display.setCursor(0, 50);
    display.println("Status: Stable");
  }

  display.display();

  esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
  delay(500); 
}