#include <SPI.h>
#include <WiFi.h>

#include <stdio.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <qrcode.h>
#include <IRremote.h>

#include "epd1in54_V2.h"
#include "epdpaint.h"

const char* deeplinkPrefix = "sdeparte://deeplink.domofit.app/?ip=";

Epd epd;
unsigned char image[40000];
Paint paint(image, 0, 0);

#define COLORED     0
#define UNCOLORED   1

const char* ssid = "SSID";
const char* password = "MDP";

int lightTimer = 0;

bool buttonState = false;
bool pirState = false;
bool lightState = false;
bool forceState = false;

const int irReceiverPin = 14;
const int relaiPin = 13;
const int buttonPin = 26;
const int pirPin = 27;
const int ledPin = 33;
const int irSenderPin = 32;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
  html {
    font-family: Arial, Helvetica, sans-serif;
    text-align: center;
  }
  h1 {
    font-size: 1.8rem;
    color: white;
  }
  h2{
    font-size: 1.5rem;
    font-weight: bold;
    color: #143642;
  }
  .topnav {
    overflow: hidden;
    background-color: #143642;
  }
  body {
    margin: 0;
  }
  .content {
    max-width: 600px;
    margin: 0 auto;
  }
  .card {
    margin-top: 50px;
    background-color: #F8F7F9;;
    box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5);
    padding-top:10px;
    padding-bottom:20px;
  }
  .button {
    padding: 15px 50px;
    font-size: 24px;
    text-align: center;
    outline: none;
    color: #fff;
    background-color: #0f8b8d;
    border: none;
    border-radius: 5px;
    -webkit-touch-callout: none;
    -webkit-user-select: none;
    -khtml-user-select: none;
    -moz-user-select: none;
    -ms-user-select: none;
    user-select: none;
    -webkit-tap-highlight-color: rgba(0,0,0,0);
   }
   .button:active {
     background-color: #0f8b8d;
     box-shadow: 2 2px #CDCDCD;
     transform: translateY(2px);
   }
   .input {
     padding: 14px 15px;
     font-size: 24px;
     outline: none;
     background-color: #fbfbfb;
     border: 1px solid #0f8b8d;
     border-radius: 5px;
     -webkit-touch-callout: none;
     -webkit-user-select: none;
     -khtml-user-select: none;
     -moz-user-select: none;
     -ms-user-select: none;
     user-select: none;
     -webkit-tap-highlight-color: rgba(0,0,0,0);
   }
   .state {
     font-size: 1.5rem;
     color:#8c8c8c;
     font-weight: bold;
   }
  </style>
<title>ESP Web Server</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:,">
</head>
<body>
  <div class="topnav">
    <h1>ESP WebSocket Server</h1>
  </div>
  <div class="content">
    <div class="card">
      <h2>LIGHT</h2>
      <hr />
      <p class="state">Always: <span id="always">%ALWAYS%</span></p>
      <p class="state">Light: <span id="light">%LIGHT%</span></p>
      <hr />
      <p>
        <button id="button-on" class="button green">ON</button>
        <button id="button-toggle" class="button">TOGGLE</button>
        <button id="button-off" class="button red">OFF</button>
      </p>
    </div>

    <div class="card">
      <h2>INFRA RED</h2>
      <hr />
      <p class="state">Received: <span id="received">%RECEIVED%</span></p>
      <hr />
      <p>
        <input id="input-send" class="input" type="text" placeholder="SAM 12345678" />
        <button id="button-send" class="button">SEND</button>
      </p>
    </div>
  </div>
<script>
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;
  window.addEventListener('load', onLoad);
  function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
  }
  function onOpen(event) {
    console.log('Connection opened');
  }
  function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 2000);
  }
  function onMessage(event) {
    var datas = event.data.split(' ');
    var type = datas[0];
    var commande = datas[1];
    if (type == 'ALW') {
      document.getElementById('always').innerHTML = commande == '1' ? 'ON' : 'OFF';
    } else if (type == 'LUM') {
      document.getElementById('light').innerHTML = commande == '1' ? 'ON' : 'OFF';
    } else {
      document.getElementById('received').innerHTML = event.data;
    }
  }
  function onLoad(event) {
    initWebSocket();
    initButtons();
  }
  function initButtons() {
    document.getElementById('button-on').addEventListener('click', lightOn);
    document.getElementById('button-toggle').addEventListener('click', lightToggle);
    document.getElementById('button-off').addEventListener('click', lightOff);

    document.getElementById('button-send').addEventListener('click', infraredSend);
  }
  function lightOn(){
    websocket.send('ALW ON');
  }
  function lightToggle(){
    websocket.send('ALW TOGGLE');
  }
  function lightOff(){
    websocket.send('ALW OFF');
  }
  function infraredSend(){
    websocket.send(document.getElementById('input-send').value);
  }
</script>
</body>
</html>
)rawliteral";

void notifyClients(String message) {
  ws.textAll(message);
}

void setLightState(bool state){
  lightState = state;

  notifyLightState();
}

void notifyLightState(){
  if (lightState) {
    notifyClients("LUM 1");
  } else {
    notifyClients("LUM 0");
  }
}

void toggleForceState(){
  forceState = !forceState;

  notifyForceState();
}

void setForceState(bool state){
  forceState = state;

  notifyForceState();
}

void notifyForceState(){
  if (forceState) {
    notifyClients("ALW 1");
  } else {
    notifyClients("ALW 0");
  }
}

void decodeIR(){
  if (IrReceiver.decode()) {
    String protocol = "UNK";
    String command = String(IrReceiver.decodedIRData.command, DEC);
    String address = String(IrReceiver.decodedIRData.address, DEC);
    
    switch(IrReceiver.decodedIRData.protocol) {
      case NEC:
        protocol = "NEC";
        break;
      
      case SAMSUNG:
        protocol = "SAM";
        break;
      
      case SONY:
        protocol = "SON";
        break;
      
      case PANASONIC:
        protocol = "PAN";
        break;
        
      case DENON:
        protocol = "DEN";
        break;
      
      case SHARP:
        protocol = "SHA";
        break;
      
      case LG:
        protocol = "LG.";
        break;
      
      case JVC:
        protocol = "JVC";
        break;
        
      case RC5:
        protocol = "RC5";
        break;
      
      case RC6:
        protocol = "RC6";
        break;
      
      case ONKYO:
        protocol = "ONK";
        break;
      
      case APPLE:
        protocol = "APL";
        break;
    }

    String message = protocol + " " + address + " " + command;

    Serial.println(message);
    notifyClients(message);

    IrReceiver.resume();
  }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*) arg;
  
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;

    String readString = String((char*) data);
    Serial.println(readString);

    if (readString.length() > 5) {
      String command = getValue(readString, ' ', 2);
      String address = getValue(readString, ' ', 1);
      String protocol = getValue(readString, ' ', 0);
      
      if(protocol == "ALW"){
        if(address == "TOGGLE"){
          toggleForceState();
        }

        if(address == "ON"){
          setForceState(true);
        }
        
        if(address == "OFF"){
          setForceState(false);
        }
        
        if(address == "STATE"){
          notifyForceState();
        }
      }
      
      if(protocol == "LUM"){
        if(address == "STATE"){
          notifyLightState();
        }
      }
      
      if(protocol == "NEC"){
        IrSender.sendNEC(strToLong(address), strToLong(command), 0, false);
      }
      
      if(protocol == "SAM"){
        IrSender.sendSamsung(strToLong(address), strToLong(command), 0, false);
      }
      
      if(protocol == "SON"){
        IrSender.sendSony(strToLong(address), strToLong(command), 2);
      }
      
      if(protocol == "PAN"){
        IrSender.sendPanasonic(strToLong(address), strToLong(command), 0);
      }
  
      if(protocol == "DEN"){
        IrSender.sendDenon(strToLong(address), strToLong(command), 0);
      }
  
      if(protocol == "SHA"){
        IrSender.sendSharp(strToLong(address), strToLong(command), 0);
      }
      
      if(protocol == "LG."){
        IrSender.sendLG(strToLong(address), strToLong(command), 0, false);
      }
      
      if(protocol == "JVC"){
        IrSender.sendJVC((uint8_t) strToLong(address), (uint8_t) strToLong(command), 0);
      }
      
      if(protocol == "RC5"){
        IrSender.sendRC5(strToLong(address), strToLong(command), 0, true);
      }
      
      if(protocol == "RC6"){
        IrSender.sendRC6(strToLong(address), strToLong(command), 0, true);
      }
      
      if(protocol == "ONK"){
        IrSender.sendOnkyo(strToLong(address), strToLong(command), 0, false);
      }
      
      if(protocol == "APL"){
        IrSender.sendApple(strToLong(address), strToLong(command), 0, false);
      }
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

String processor(const String& var){
  if (var == "ALWAYS") {
    if (forceState) {
      return "ON";
    } else {
      return "OFF";
    }
  }

  if (var == "LIGHT") {
    if (lightState) {
      return "ON";
    } else {
      return "OFF";
    }
  }

  if (var == "RECEIVED") {
    return "-";
  }
}

void setup()
{
  Serial.begin(115200);
  epd.LDirInit();
  epd.Clear();
  
  drawString("Connecting ...");
  
  pinMode(buttonPin, INPUT_PULLDOWN);
  pinMode(pirPin, INPUT_PULLDOWN);
  
  pinMode(relaiPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  
  digitalWrite(relaiPin, LOW);
  digitalWrite(ledPin, LOW);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
  }
  
  drawString("Start server ...");

  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  // Start server
  server.begin();
  
  drawQRCode(WiFi.localIP().toString().c_str());

  IrSender.begin(irSenderPin, ENABLE_LED_FEEDBACK);
  IrReceiver.begin(irReceiverPin, ENABLE_LED_FEEDBACK);
  IrReceiver.start();
}

void loop()
{
  ws.cleanupClients();

  if (forceState || lightTimer > 0) {
    lightTimer = lightTimer - 1;

    if (!lightState) {
      setLightState(true);
    }
  } else {
    if (lightState) {
      setLightState(false);
    }
  }

  digitalWrite(relaiPin, lightState);
  digitalWrite(ledPin, forceState);

  bool pirInstantState = digitalRead(pirPin);

  if (pirInstantState && pirInstantState != pirState) {
    lightTimer = 200;
  }

  pirState = pirInstantState;

  bool buttonInstantState = digitalRead(buttonPin);

  if (buttonInstantState && buttonInstantState != buttonState) {
    toggleForceState();
  }

  buttonState = buttonInstantState;

  decodeIR();

  delay(50);
}

void drawString(const char* string) {
  Serial.println(string);
  paint.SetWidth(200);
  paint.SetHeight(24);

  paint.Clear(UNCOLORED);
  paint.DrawStringAt(10, 4, string, &Font16, COLORED);
  epd.SetFrameMemoryPartial(paint.GetImage(), 0, 0, paint.GetWidth(), paint.GetHeight());
  epd.DisplayPartFrame();
}

void drawQRCode(const char* ipAdresse) {
  char ipMessage[100];
  strcpy(ipMessage, "Ip: ");
  strcat(ipMessage, WiFi.localIP().toString().c_str());

  Serial.println(ipMessage);
  
  char deeplink[100];
  strcpy(deeplink, deeplinkPrefix);
  strcat(deeplink, ipAdresse);
  
  byte box_x = 16;
  byte box_y = 33;
  byte box_s = 5;
  byte init_x = box_x;
  
  paint.SetWidth(200);
  paint.SetHeight(200);
  paint.Clear(UNCOLORED);
  paint.DrawRectangle(box_x-1, box_y-1, 182, 199, COLORED);
  paint.DrawStringAt(10, 4, WiFi.localIP().toString().c_str(), &Font16, COLORED);
  
  // Create the QR code
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(4)];
  qrcode_initText(&qrcode, qrcodeData, 4, 0, deeplink);
  
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        paint.DrawFilledRectangle(box_x, box_y, box_x+box_s, box_y+box_s, COLORED);
      } else {
        paint.DrawFilledRectangle(box_x, box_y, box_x+box_s, box_y+box_s, UNCOLORED);
      }
      
      box_x = box_x + box_s;
    }

    box_y = box_y + box_s;
    box_x = init_x;
  }
  
  epd.SetFrameMemory(paint.GetImage(), 0, 0, paint.GetWidth(), paint.GetHeight());
  epd.DisplayFrame();
}

unsigned long strToLong(String str) {
  unsigned int bufSize = str.length() + 1; //String length + null terminator
  char* ret = new char[bufSize];
  str.toCharArray(ret, bufSize);
  return atol(ret);
}

int getMinByteSize(unsigned long val) {
    unsigned long long value = val;
    int length = 1;
    
    if ((value & 0xFFFFFFFF00000000) != 0){
        length += 4;
        value = value >> 32;
    }
    
    if ((value & 0xFFFF0000) != 0){
        length += 2;
        value = value >> 16;
    }
    
    if ((value & 0xFF00) != 0){
        length += 1;
        value = value >> 8;
    }

    return length;
}

String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length()-1;

  for (int i=0; i<=maxIndex && found <= index; i++) {
    if (data.charAt(i)==separator || i == maxIndex) {
        found++;
        strIndex[0] = strIndex[1]+1;
        strIndex[1] = (i == maxIndex) ? i+1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}
