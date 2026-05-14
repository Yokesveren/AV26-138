#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ✅ OLED Configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ✅ Receiver MAC Address (Node Z)
uint8_t broadcastAddress[] = {0x68, 0xFE, 0x71, 0xFA, 0x9C, 0x00};

// ✅ ESP-NOW Data Structure
typedef struct struct_message {
  int nodeID;
  float sensorValue;
  int alert;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;
Adafruit_MPU6050 mpu;

float gravityOffset = 9.81; 

// ✅ ESP-NOW Sent Callback (Updated for Core 3.x)
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  Serial.print("\rSend Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void setup() {
  Serial.begin(115200);

  // 1. Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED error");
    for (;;);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("LANDSLIDE NODE v1.0");
  display.display();

  // 2. Initialize MPU6050
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found!");
    display.println("MPU6050 ERROR");
    display.display();
    while (1) delay(10);
  }

  // 3. Auto-Calibration (Stay still during this!)
  display.println("Calibrating...");
  display.display();
  
  float sum = 0;
  for(int i=0; i<50; i++) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    sum += sqrt(pow(a.acceleration.x, 2) + pow(a.acceleration.y, 2) + pow(a.acceleration.z, 2));
    delay(20);
  }
  gravityOffset = sum / 50.0; 

  // 4. Initialize ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }
  esp_now_register_send_cb(OnDataSent);

  // 5. Register Receiver
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  display.println("Ready!");
  display.display();
  delay(1000);
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Magnitude Calculation
  float magnitude = sqrt(pow(a.acceleration.x, 2) + 
                         pow(a.acceleration.y, 2) + 
                         pow(a.acceleration.z, 2));

  // Calculate motion relative to gravity
  float motion = abs(magnitude - gravityOffset);

  // Filter sensor noise
  if (motion < 0.12) motion = 0.0;

  // Prepare Message
  myData.nodeID = 1;
  myData.sensorValue = motion;
  myData.alert = (motion > 2.5) ? 1 : 0;

  // Send via ESP-NOW
  esp_now_send(broadcastAddress, (uint8_t *)&myData, sizeof(myData));

  // Update Display
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.printf("Node ID:  %d\n", myData.nodeID);
  display.printf("Motion:   %.2f\n", motion);
  display.println("---------------------");

  display.setCursor(0, 35);
  if (myData.alert == 1) {
    display.setTextSize(2);
    display.println("!!ALERT!!");
    Serial.println("!!! LANDSLIDE ALERT !!!");
  } else {
    display.setTextSize(2);
    display.println("STABLE");
  }

  display.display();
  delay(300); 
}