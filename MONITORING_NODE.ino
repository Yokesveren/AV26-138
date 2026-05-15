#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define BUZZER_PIN 15
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// UNIVERSAL STRUCT: Must match all nodes perfectly
typedef struct {
  char nodeId;      
  float val1;       
  float val2;       
  bool isSOS;       
  char msg[32];     
} Packet;

Packet incomingPkt;
unsigned long alertTimer = 0;
bool alertLocked = false;

void drawIdleScreen() {
  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, WHITE);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(15, 20);
  display.print("YOKES SMART CITY");
  display.setCursor(22, 40);
  display.print("SYSTEM MONITORING");
  display.display();
}

void onReceive(const esp_now_recv_info *info, const uint8_t *data, int len) {
  Packet temp;
  memcpy(&temp, data, sizeof(temp));

  // ========== ADDED: Serial output for Python dashboard ==========
  // Format: NODEID,VAL1,VAL2,SOS,MSG
  Serial.print(incomingPkt.nodeId);
  Serial.print(",");
  Serial.print(incomingPkt.val1);
  Serial.print(",");
  Serial.print(incomingPkt.val2);
  Serial.print(",");
  Serial.print(incomingPkt.isSOS);
  Serial.print(",");
  Serial.println(incomingPkt.msg);
  // ===============================================================


  // PRIORITY LOCK: If an SOS is active, ignore "Normal" data for 3 seconds
  if (alertLocked && !temp.isSOS && (millis() - alertTimer < 3000)) return;

  memcpy(&incomingPkt, &temp, sizeof(incomingPkt));

  if (incomingPkt.isSOS) {
    alertLocked = true;
    alertTimer = millis();
    digitalWrite(BUZZER_PIN, HIGH);
    
    display.clearDisplay();
    display.fillRect(0, 0, 128, 20, WHITE); // Header background
    display.setTextColor(BLACK);
    display.setTextSize(1);
    display.setCursor(5, 6);
    
    // Header Logic
    if (incomingPkt.nodeId == 'C') display.print("!! EARTHQUAKE !!");
    else if (incomingPkt.nodeId == 'D') display.print("!! STORM ALERT !!");
    else if (incomingPkt.nodeId == 'E') display.print("!! LANDSLIDE !!");
    else if (incomingPkt.nodeId == 'F') display.print("!! MEDICAL HELP !!");
    else display.print("!! SYSTEM ALERT !!");

    display.setTextColor(WHITE);
    display.setCursor(0, 28);
    display.print("Location: Node "); display.println(incomingPkt.nodeId);
    
    display.setCursor(0, 40);
    if (incomingPkt.nodeId == 'F') display.print("Heart Rate: ");
    else if (incomingPkt.nodeId == 'E') display.print("Soil Motion: ");
    else display.print("Sensor Val: ");
    display.print(incomingPkt.val1);

    display.drawFastHLine(0, 52, 128, WHITE);
    display.setCursor(0, 56);
    display.print("MSG: "); display.print(incomingPkt.msg);
    display.display();
  } 
  else if (!alertLocked) {
    // Normal Round-Robin display
    digitalWrite(BUZZER_PIN, LOW);
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("--- CITY BASE A ---");
    display.println("Status: Normal");
    display.print("Active: Node "); display.println(incomingPkt.nodeId);
    display.print("Data: "); display.println(incomingPkt.val1);
    display.display();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  Wire.begin(21, 22);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(onReceive);
  drawIdleScreen();
}

void loop() {
  // Clear alert after 3 seconds of showing it
  if (alertLocked && (millis() - alertTimer > 3000)) {
    alertLocked = false;
    digitalWrite(BUZZER_PIN, LOW);
    drawIdleScreen();
  }
}
