#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// MPU6050
Adafruit_MPU6050 mpu;

// 1. BROADCAST ADDRESS
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// 2. UNIVERSAL PACKET STRUCT (Compatible with City Node A)
typedef struct {
  char nodeId;      // 'E'
  float val1;       // Motion Magnitude
  float val2;       // Spare
  bool isSOS;       // Alert Flag
  char msg[32];     // Custom Message
} Packet;

Packet myData;
esp_now_peer_info_t peerInfo;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // --- OLED INITIALIZATION ---
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Failed");
    for(;;);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("LANDSLIDE NODE E");
  display.println("Connecting Sensor...");
  display.display();

  // --- MPU6050 INITIALIZATION ---
  if (!mpu.begin()) {
    display.clearDisplay();
    display.setCursor(0, 20);
    display.println("MPU6050 NOT FOUND!");
    display.println("Check Wiring (0x68)");
    display.display();
    while (1) delay(10);
  }

  // --- ESP-NOW SETUP ---
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE); // Stay on Monitoring Channel
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) return;

  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 1; 
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  
  myData.nodeId = 'E';
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Calculate Magnitude (Normal gravity is ~9.81)
  float magnitude = sqrt(pow(a.acceleration.x, 2) + pow(a.acceleration.y, 2) + pow(a.acceleration.z, 2));
  float motion = abs(magnitude - 9.81);

  myData.val1 = motion;

  // --- LOCAL DISPLAY LOGIC ---
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("--- LANDSLIDE NODE E ---");
  
  display.setCursor(0, 20);
  display.print("Motion: ");
  display.print(motion, 2);
  display.println(" m/s2");

  // ALERT LOGIC
  if (motion > 2.5) {
    myData.isSOS = true;
    strcpy(myData.msg, "LANDSLIDE DETECTED");
    
    // UI ALERT
    display.setTextSize(2);
    display.setCursor(10, 40);
    display.println("LANDSLIDE");
  } else {
    myData.isSOS = false;
    strcpy(myData.msg, "Stable");
    
    // UI NORMAL
    display.setTextSize(1);
    display.setCursor(0, 50);
    display.println("Status: GROUND STABLE");
  }

  display.display(); // Push layout to screen

  // --- SEND TO CITY HUB ---
  esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));

  delay(200); 
}
