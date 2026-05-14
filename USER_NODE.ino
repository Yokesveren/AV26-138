#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "MAX30100_PulseOximeter.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SOS_BUTTON_PIN 13
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

PulseOximeter pox;
WiFiServer server(80);
uint32_t tsLastReport = 0;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct {
  char nodeId;
  float val1;
  float val2;
  bool isSOS;
  char msg[32];
} Packet;

Packet myData;
bool webCritical = false;
char webMsg[32] = "Stable";

// --- HTML with Continuous Web Speech API ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { font-family: 'Segoe UI', Arial; text-align: center; background: #1a1a1a; color: white; padding: 20px; }
  .btn { padding: 15px; width: 85%; margin: 10px; font-size: 18px; border-radius: 12px; border: none; font-weight: bold; cursor: pointer; }
  .mic-btn { background: #3498db; } .sos-btn { background: #e74c3c; } .safe-btn { background: #2ecc71; }
  #transcript { width: 80%; padding: 12px; border-radius: 8px; border: none; margin: 10px; font-size: 16px; }
  #status { color: #f1c40f; margin-top: 10px; }
</style></head>
<body>
  <h2>YOKES NODE F</h2>
  <button class="btn mic-btn" id="micBtn" onclick="toggleDictation()">🎤 START LISTENING</button>
  <div id="status">Microphone Ready</div>
  <form action="/get">
    <input type="text" name="m" id="transcript" placeholder="Voice text here..."><br>
    <button type="submit" name="s" value="1" class="btn sos-btn">SEND AS CRITICAL</button>
    <button type="submit" name="s" value="0" class="btn safe-btn">SEND AS NORMAL</button>
  </form>

  <script>
    var recognition;
    var isListening = false;
    if (window.hasOwnProperty('webkitSpeechRecognition')) {
      recognition = new webkitSpeechRecognition();
      recognition.continuous = true;
      recognition.interimResults = true;
      recognition.lang = "en-US";

      recognition.onresult = function(e) {
        var finalText = "";
        for (var i = e.resultIndex; i < e.results.length; ++i) {
          if (e.results[i].isFinal) finalText += e.results[i][0].transcript;
        }
        if(finalText != "") document.getElementById('transcript').value = finalText;
      };

      recognition.onend = function() {
        isListening = false;
        document.getElementById('micBtn').innerHTML = "🎤 START LISTENING";
        document.getElementById('status').innerHTML = "Mic Off";
      };
    }

    function toggleDictation() {
      if (isListening) {
        recognition.stop();
      } else {
        document.getElementById('transcript').value = "";
        recognition.start();
        isListening = true;
        document.getElementById('micBtn').innerHTML = "🛑 STOP LISTENING";
        document.getElementById('status').innerHTML = "Listening for sentences...";
      }
    }
  </script>
</body></html>)rawliteral";

void setup() {
    Serial.begin(115200);
    pinMode(SOS_BUTTON_PIN, INPUT_PULLUP);

    // 1. POWER DELAY (Prevents OLED Blackout)
    delay(1500); 
    Wire.begin(21, 22);
    
    if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        display.clearDisplay();
        display.setTextColor(WHITE);
        display.setCursor(0, 20);
        display.println("OLED: ONLINE");
        display.println("WAITING FOR WIFI...");
        display.display();
    }

    // 2. STAGGERED WIFI BOOT
    delay(2000); 
    WiFi.softAP("YOKES_NODE_F", "12345678");
    server.begin();
    
    // 3. SENSOR BOOT
    delay(500);
    if(pox.begin()){
        pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
    }

    // 4. ESP-NOW
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);
    if (esp_now_init() == ESP_OK) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, broadcastAddress, 6);
        peer.channel = 1;
        esp_now_add_peer(&peer);
    }
    myData.nodeId = 'F';
}

void loop() {
    pox.update();

    WiFiClient client = server.available();
    if (client) {
        String req = client.readStringUntil('\r');
        if (req.indexOf("GET /get?") != -1) {
            int msgPos = req.indexOf("m=");
            int endPos = req.indexOf("&s=");
            if (msgPos != -1 && endPos != -1) {
                String rawMsg = req.substring(msgPos + 2, endPos);
                rawMsg.replace("+", " ");
                rawMsg.replace("%20", " ");
                strncpy(webMsg, rawMsg.c_str(), 31);
                webCritical = (req.substring(endPos + 3, endPos + 4) == "1");
            }
        }
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n");
        client.print(index_html);
        client.stop();
    }

    if (millis() - tsLastReport > 1000) {
        float bpm = pox.getHeartRate();
        bool buttonPressed = (digitalRead(SOS_BUTTON_PIN) == LOW);

        if (buttonPressed) {
            myData.isSOS = true;
            strcpy(myData.msg, "HELP!!! (BUTTON)");
        } else if (webCritical) {
            myData.isSOS = true;
            strncpy(myData.msg, webMsg, 31);
        } else {
            myData.isSOS = (bpm >= 130);
            strcpy(myData.msg, myData.isSOS ? "HIGH BPM!" : "Stable");
        }

        myData.val1 = bpm;
        myData.val2 = pox.getSpO2();

        display.clearDisplay();
        display.setCursor(0, 0);
        display.print("YOKES | ID: 138");
        display.drawFastHLine(0, 10, 128, WHITE);
        display.setCursor(0, 20);
        display.print("BPM: "); display.print((int)bpm);
        display.setCursor(65, 20);
        display.print("SpO2: "); display.print((int)myData.val2);
        
        display.setCursor(0, 45);
        display.print("MSG: "); display.print(myData.msg);
        display.display();

        esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
        tsLastReport = millis();
    }
}