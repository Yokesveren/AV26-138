#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =====================================================
// CONFIG
// =====================================================

// RECEIVER MAC ADDRESS
uint8_t broadcastAddress[] = {
  0x88, 0x57, 0x21, 0xAD, 0x77, 0xC8
};

// OLED
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// WEB SERVER
WebServer server(80);

// =====================================================
// ESP-NOW CALLBACK
// =====================================================

void OnDataSent(const wifi_tx_info_t *tx_info,
                esp_now_send_status_t status) {

  Serial.print("ESP-NOW Status: ");

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("SUCCESS");
  }
  else {
    Serial.println("FAILED");
  }
}

// =====================================================
// HTML PAGE
// =====================================================

const char index_html[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html>

<head>

<title>ESP Voice Gateway</title>

<meta name="viewport"
content="width=device-width, initial-scale=1">

<style>

body{
  margin:0;
  padding:30px;
  font-family:Arial;
  background:#0f172a;
  color:white;
  text-align:center;
}

h1{
  margin-bottom:20px;
}

#priority{
  padding:12px;
  border-radius:10px;
  border:none;
  font-size:18px;
}

#btn{
  margin-top:30px;
  width:130px;
  height:130px;
  border-radius:50%;
  border:none;
  background:#38bdf8;
  font-size:50px;
  cursor:pointer;
}

#btn:active{
  transform:scale(0.95);
}

#status{
  margin-top:30px;
  font-size:20px;
  word-wrap:break-word;
}

</style>

</head>

<body>

<h1>ESP Voice Gateway</h1>

<select id="priority">

  <option value="NORMAL">
    NORMAL
  </option>

  <option value="CRITICAL">
    CRITICAL
  </option>

</select>

<br>

<button id="btn">🎤</button>

<p id="status">
Tap microphone to speak
</p>

<script>

const btn =
document.getElementById('btn');

const status =
document.getElementById('status');

const priority =
document.getElementById('priority');

const SpeechRecognition =
window.SpeechRecognition ||
window.webkitSpeechRecognition;

if(SpeechRecognition){

  const recognition =
  new SpeechRecognition();

  recognition.continuous = false;
  recognition.interimResults = false;
  recognition.maxAlternatives = 1;
  recognition.lang = 'en-US';

// FORCE LOCAL PROCESSING
  recognition.serviceURI = 'local';
   
  // SPEED OPTIMIZATION
  recognition.continuous = false;
  recognition.interimResults = false;
  recognition.maxAlternatives = 1;

  recognition.lang = 'en-US';

  btn.onclick = () => {

    status.innerHTML =
    "Listening...";

    recognition.start();
  };

  recognition.onresult = (event) => {

    const text =
    event.results[0][0].transcript;

    const level =
    priority.value;

    status.innerHTML =
    "[" + level + "] " + text;

    fetch(
      `/send?msg=${encodeURIComponent(text)}&level=${level}`
    )
    .then(response => response.text())
    .then(data => {
      console.log(data);
    });
  };

  recognition.onerror = (event) => {

    status.innerHTML =
    "Error: " + event.error;
  };
}
else{

  status.innerHTML =
  "Speech Recognition Not Supported";
}

</script>

</body>
</html>

)rawliteral";

// =====================================================
// WEB HANDLERS
// =====================================================

void handleRoot() {

  server.send_P(
    200,
    "text/html",
    index_html
  );
}

void handleSend() {

  if (server.hasArg("msg") &&
      server.hasArg("level")) {

    String msg =
    server.arg("msg");

    String level =
    server.arg("level");

    // FINAL MESSAGE FORMAT
    // Example:
    // CRITICAL|Fire detected

    String finalMessage =
    level + "|" + msg;

    // SEND VIA ESP-NOW
    esp_err_t result =
    esp_now_send(
      broadcastAddress,
      (uint8_t *)finalMessage.c_str(),
      finalMessage.length() + 1
    );

    // SERIAL DEBUG
    Serial.println("--------------------------------");

    Serial.print("Priority: ");
    Serial.println(level);

    Serial.print("Message : ");
    Serial.println(msg);

    Serial.print("Packet  : ");
    Serial.println(finalMessage);

    Serial.println("--------------------------------");

    // OLED DISPLAY
    display.clearDisplay();

    display.setTextSize(1);

    display.setCursor(0,0);

    display.println("VOICE SENT");

    display.println("");

    display.print("TYPE: ");
    display.println(level);

    display.println("");

    display.println(msg);

    display.display();

    if(result == ESP_OK){

      server.send(
        200,
        "text/plain",
        "Message Sent"
      );
    }
    else{

      server.send(
        500,
        "text/plain",
        "ESP-NOW Send Failed"
      );
    }
  }
  else{

    server.send(
      400,
      "text/plain",
      "Invalid Request"
    );
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(500);

  // =================================================
  // OLED INIT
  // =================================================

  Wire.begin(21, 22);

  if(!display.begin(
      SSD1306_SWITCHCAPVCC,
      0x3C)) {

    Serial.println("OLED Failed");

    while(true);
  }

  display.clearDisplay();

  display.setTextColor(WHITE);

  display.setTextSize(1);

  display.setCursor(0,0);

  display.println("BOOTING...");

  display.display();

  // =================================================
  // FAST WIFI AP MODE
  // =================================================

  WiFi.mode(WIFI_AP);

  WiFi.setSleep(false);

  // FAST FIXED CHANNEL AP
  WiFi.softAP(
    "ESP-Voice-Link",
    "12345678",
    1,   // CHANNEL
    0,   // HIDDEN
    4    // MAX CLIENTS
  );

  delay(100);

  Serial.println("");
  Serial.println("================================");
  Serial.println("ACCESS POINT STARTED");
  Serial.println("================================");

  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  // =================================================
  // FIX CHANNEL FOR ESP-NOW
  // =================================================

  esp_wifi_set_channel(
    1,
    WIFI_SECOND_CHAN_NONE
  );

  // =================================================
  // ESP-NOW INIT
  // =================================================

  if (esp_now_init() != ESP_OK) {

    Serial.println("ESP-NOW INIT FAILED");

    display.clearDisplay();

    display.setCursor(0,0);

    display.println("ESP-NOW FAILED");

    display.display();

    return;
  }

  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};

  memcpy(
    peerInfo.peer_addr,
    broadcastAddress,
    6
  );

  peerInfo.channel = 1;

  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo)
      != ESP_OK) {

    Serial.println("PEER ADD FAILED");
  }
  else {

    Serial.println("PEER ADDED");
  }

  // =================================================
  // WEB SERVER
  // =================================================

  server.on("/", handleRoot);

  server.on("/send", handleSend);

  server.begin();

  Serial.println("WEB SERVER STARTED");

  // =================================================
  // READY SCREEN
  // =================================================

  display.clearDisplay();

  display.setCursor(0,0);

  display.println("SYSTEM READY");

  display.println("");

  display.println("WiFi Name:");

  display.println("ESP-Voice-Link");

  display.println("");

  display.println("Password:");

  display.println("12345678");

  display.display();

  Serial.println("SYSTEM READY");
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  server.handleClient();
}