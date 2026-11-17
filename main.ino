#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <time.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <RCSwitch.h>
#include <ArduinoJson.h>

// --- Configuration Settings ---
// make sure the light state is persistant if the user changes it
bool lightStaysOn = false;



// --- Network Configuration ---
const char *WIFI_SSID = "WIFI-SSID";
const char *WIFI_PASSWORD = "WIFI-PASSWORD";
IPAddress sensorIP(192, 168, 1, 99);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 1, 1);
const uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
static uint32_t lastWiFiRetryMillis = 0;

// -- Web Server Settings ---
ESP8266WebServer server(80);

const char* www_username = "CONROLER-USERNAME";
const char* www_password = "CONTROLLER-PASSWORD";

// --- Radio settings --- 
#define CC1101_CS    15  // D8 (GPIO15)
#define TX_PIN       5   // D1 (GPIO5 - GDO0)

// Captured remote values
const unsigned long CODE_ON  = 0x555501; 
const unsigned long CODE_OFF = 0x555503;

const unsigned long CODE_LIGHT_INC = 0x555504;
const unsigned long CODE_LIGHT_MIN = 0x555506;

const unsigned long CODE_RST = 0x555507;
const unsigned long CODE_MODE = 0x555509;

const unsigned long CODE_SET_4H = 0x55550D;
const unsigned long CODE_SET_8H = 0x55550F;
const unsigned long CODE_SET_12H = 0x555510;
const unsigned long CODE_CLEAR = 0x555512;


// protocol specs
const int BIT_LENGTH         = 24;
const int PROTOCOL           = 1;
const int PULSE_LENGTH       = 350;

// Schedule (24-hour format) and keep them writeable
int TURN_ON_HOUR   = 8;   // 08:00 AM
int TURN_OFF_HOUR  = 20;  // 08:00 PM

RCSwitch mySwitch = RCSwitch();

// --- Radio State Conditions ---
static bool lightState = false; // default to off in case of power failure
static int lightIntensity = 10;
static int powerLevel = -15; // set to be lowest acceptable value on init can be: -30, -20, -15, -10, 0, 5, 7, 10 However we only allow up to 0
static int delayHours = 0; // the light internal timer value last set

// we want to make sure the lights are always on when they need to be, but we dont want to blast RF every second
// in order to do this we initialize an hour counter that increments down every time the loop is passed through
int hourlyCheck = 216000;

// --- Connect to WiFi Function ---
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  if (!WiFi.config(sensorIP, gateway, subnet, primaryDNS)) {
    Serial.println("Static IP configuration failed");
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    delay(250);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nSensor URL: http://%s/sensor\n",
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi connection timed out");
    WiFi.disconnect();
  }
}

// --- Authorization Function
bool isAuthorized() {
  if (!server.authenticate(www_username, www_password)) {
    server.requestAuthentication();
    server.send(401, "application/json", "{ \"error\" : \"Not Authorized\" }\n");
    return false;
  }
  return true;
}



// --- Send RF function ---
void sendRFCommand(unsigned long code) {
  ELECHOUSE_cc1101.SetTx(); // Switch CC1101 to Transmit Mode
  
  mySwitch.setProtocol(PROTOCOL);
  mySwitch.setPulseLength(PULSE_LENGTH);
  mySwitch.setRepeatTransmit(4); // Send multiple times to ensure receipt
  
  mySwitch.send(code, BIT_LENGTH);
  
  ELECHOUSE_cc1101.SetRx(); // Switch back to Receive or idle
}


// --- Set Intensity function ---
// set the intensity of the light from 1 to 9
void setLightIntensity(int setting){
  // if the light is currently set to be more than the desired value

  for(int count=0; count < 10; count++ ){
    sendRFCommand(CODE_LIGHT_MIN);
    delay(500);
  }
  for(int count=0; count <= setting; count++ ){
    sendRFCommand(CODE_LIGHT_INC);
    delay(500);
  }

  lightIntensity = setting;
}

// --- Set Light Handling
void handleLed() {
   if (!isAuthorized()) return;
  String body = server.arg("plain");
  body.toLowerCase();

  if (body.indexOf("\"state\":true") >= 0 ||
      body.indexOf("\"state\": true") >= 0) {
// make sure the light stays on reguardless of schedule
    lightStaysOn = true;
    Serial.println("Turning ON grow light.");
    sendRFCommand(CODE_ON);
    lightState=true;
  } else if (body.indexOf("\"state\":false") >= 0 ||
             body.indexOf("\"state\": false") >= 0) {
// make sure the light stays off reguardless of schedule
    lightStaysOn = true;
    Serial.println("Turning OFF grow light.");
    sendRFCommand(CODE_OFF);
    lightState=false;
  } else {
    server.send(400, "application/json",
                "{\"accepted\":false,\n\"error\":\"Expected state true or false\"}");
    return;
  }

  String response = "{\"accepted\":true,\"light\":";
  response += lightState ? "true" : "false";
  response += "}";
  server.send(200, "application/json", response);
}

// --- Set Light Intensity Handling
void handleLightIntensity() {
  if (!isAuthorized()) return;
  String body = server.arg("plain");
  body.toLowerCase();
  JsonDocument bodyJson;
  deserializeJson(bodyJson,body);
  int intensityValue = bodyJson["value"];
  Serial.println(intensityValue);
  String response = "{\n \"result\":";
  // if within acceptable range then set the light intensity to be requested value
  if(bodyJson["value"] >= 1 || bodyJson["value"] <= 10){
      setLightIntensity(bodyJson["value"]);
      response+="\"success\",\n";
      response+=" \"value\": "+String(bodyJson["value"])+"\n}\n";
  } else {
      response+="\"fail,\"\n";
      response+="\"value: "+String(lightIntensity)+",\n";
      response+="\"error\": \"Expected 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 \"\n}\n";
  }
  server.send(200, "application/json", response);
}


// --- Set Radio Power Level Handling
void handlePowerLevel() {
  if (!isAuthorized()) return;
  String body = server.arg("plain");
  body.toLowerCase();
  JsonDocument bodyJson;
  deserializeJson(bodyJson,body);
  int intensityValue = bodyJson["value"];
  Serial.println(intensityValue);
  String response = "{\n \"result\":";
  // if within acceptable range then set the light intensity to be requested value
  if(bodyJson["value"] >= -30 || bodyJson["value"] <= 0){
      powerLevel = bodyJson["value"];
      ELECHOUSE_cc1101.setPA(powerLevel);
      response+="\"success\",\n";
      response+=" \"value\": "+String(bodyJson["value"])+"\n}\n";
  } else {
      response+="\"fail,\"\n";
      response+=" \"value\": "+String(powerLevel)+",\n";// return last power level
      response+=" \"error\": \"Expected -30, -20, -15, -10, 0 \"\n}\n";// return accepted power levels
  }
  server.send(200, "application/json", response);
}



// --- Set Timer Handling
void handleDelay() {
   if (!isAuthorized()) return;
  String body = server.arg("plain");
  body.toLowerCase();
  JsonDocument bodyJson;
  deserializeJson(bodyJson,body);
  int timerHours = bodyJson["value"];
  String response = "{\n \"result\":";
  // if within acceptable range then set the active hours of the light
  if(timerHours == 4 || timerHours == 8 || timerHours == 12 ){
      powerLevel = bodyJson["value"];
      if(timerHours==4){
        sendRFCommand(CODE_SET_4H);
        delayHours=4;
      } else if(timerHours==8){
        sendRFCommand(CODE_SET_8H);
        delayHours=8;
      } else if(timerHours==12){
        sendRFCommand(CODE_SET_12H);
        delayHours=12;
      }
      response+="\"success\",\n";
      response+=" \"value\": "+String(timerHours)+"\n}\n";
  } else {
      response+="\"fail\",\n";
      response+=" \"error\": \"Expected 4, 8, 12 \"}\n";// return last power level
  }
  server.send(200, "application/json", response);
}

void handleSetSchedule(){
  if (!isAuthorized()) return;
  String body = server.arg("plain");
  body.toLowerCase();
  JsonDocument bodyJson;
  deserializeJson(bodyJson,body);
  int startHour = bodyJson["onhour"];
  int endHour = bodyJson["offhour"];
  String response = "{\n \"result\":";
  // if within acceptable range then set the active hours of the light
  if(startHour <= 24 && endHour <= 24 && startHour >= 1 && endHour >= 1 ){
      TURN_ON_HOUR = startHour;
      TURN_OFF_HOUR = endHour;
      response+="\"success\",\n";
      response+=" \"value\": { \"start\" : "+String(startHour)+" , \"end\" : "+String(endHour)+" }\n}\n";
  } else {
      response+="\"fail,\"\n";
      response+=" \"value\": { \"start\" : "+String(TURN_ON_HOUR)+", \"end\" : "+String(TURN_OFF_HOUR)+" },\n";// return the current schedule
      response+=" \"error\": \"Expected onhour with value of 0-24 and offhour with values from 0-24 \"}\n";
  }
  server.send(200, "application/json", response);
  
}

// --- Teapot request :3 ---
void handleBrew(){
    server.send(418, "application/json",  "{ \"error\" : \"I'm a teapot\" }\n" );
}


// --- Get information handler ---
void handleGetInfo() {
  // get the current time

  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  configTime(-7 * 3600, 0, "time.nist.gov"); // Example: UTC-7
  char timeBuffer[80];
  strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  // form the response
  String response = "{\n \"result\":";
  response+=" \"success\",\n";
  response+=" \"currentTime\": \""+String(timeBuffer)+"\",\n";
  response+=" \"internalDelay\": "+String(delayHours)+",\n";
  response+=" \"turnOnHour\": "+String(TURN_ON_HOUR)+",\n";
  response+=" \"turnOffHour\": "+String(TURN_OFF_HOUR)+",\n";
  response+=" \"hourlyOnCounter\": "+String(hourlyCheck)+",\n";
  response+=" \"radioPowerLevel\": "+String(powerLevel)+",\n";
  response+=" \"lightIntensity\": "+String(lightIntensity)+",\n";
  response+=" \"lightState\": "+String(lightState)+"\n}\n";
  server.send(200, "application/json", response);
}

// --- Setup and Loop Functions -- 
void setup() {
  Serial.begin(115200);
  
  // Initialize CC1101
  ELECHOUSE_cc1101.setSpiPin(14, 12, 13, CC1101_CS); // SCK=D5, MISO=D6, MOSI=D7, CS=D8
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setMHZ(433.92);
  // Set TX power in dBm. Options: -30, -20, -15, -10, 0, 5, 7, 10
  // we keep this to the minimum possible value so that it does not interfere in close quarters
  // but allow it to be set via the API
  ELECHOUSE_cc1101.setPA(powerLevel);
  // Initialize RCSwitch on GDO0
  mySwitch.enableTransmit(TX_PIN);
  // Connect to Wi-Fi for NTP Time Sync
  connectWiFi();
  // server endpoints
// test with: curl -u USERNAME:PASSWORD-X POST 192.168.40.99/light -d "{\"state\":true}"; echo
  server.on("/light", HTTP_POST, handleLed);
// test with: curl -u USERNAME:PASSWORD -X POST 192.168.40.99/intensity -d "{\"value\": "10" }"; echo
  server.on("/intensity", HTTP_POST, handleLightIntensity);
// test with: curl -u USERNAME:PASSWORD -X POST 192.168.40.99/powerlevel -d "{\"value\": "0" }"; echo
  server.on("/powerlevel", HTTP_POST, handlePowerLevel);
// test with: curl -u USERNAME:PASSWORD 192.168.40.99/info
  server.on("/info", HTTP_GET, handleGetInfo);
// test with: curl -X POST 192.168.40.99/setdelay -d "{\"value\": 8 }"; echo
  server.on("/setdelay", HTTP_POST, handleDelay);
// test with: curl -u USERNAME:PASSWORD -X POST 192.168.40.99/schedule -d "{\"onhour\": 8, \"offhour\": 20 }"
  server.on("/setschedule", HTTP_POST, handleSetSchedule);
// test with: curl 192.168.40.99/brew"
  server.on("/brew", HTTP_GET, handleBrew);


  server.onNotFound([]() {
    server.send(404, "application/json", "{ \"error\" : \"Not Found\" }\n");
  });
  server.begin();

  // Sync internal clock via NTP (Configure offset for your time zone in seconds)
  configTime(-7 * 3600, 0, "time.nist.gov"); // Example: UTC-7
}

void loop() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  server.handleClient();
  if (WiFi.status() != WL_CONNECTED &&
      now - lastWiFiRetryMillis >= WIFI_RETRY_INTERVAL_MS) {
    lastWiFiRetryMillis = now;
    connectWiFi();
  }



  if (timeinfo->tm_year > 70) { // Verify NTP time is synchronized
    int currentHour = timeinfo->tm_hour;
    // Check schedule: should light be active?
    bool shouldBeOn = (currentHour >= TURN_ON_HOUR && currentHour < TURN_OFF_HOUR);
    if( shouldBeOn && hourlyCheck <= 0 && !lightStaysOn){
      Serial.println("Schedule Trigger: Turning ON grow light.");
      sendRFCommand(CODE_ON);
      lightState = true;
      hourlyCheck = 216000;
    }
    if (shouldBeOn && !lightState ) {
      Serial.println("Schedule Trigger: Turning ON grow light.");
      sendRFCommand(CODE_ON);
      lightState = true;
    } else if (!shouldBeOn && lightState  && !lightStaysOn) {
      Serial.println("Schedule Trigger: Turning OFF grow light.");
      sendRFCommand(CODE_OFF);
      lightState = false;
    }
  }
  hourlyCheck = hourlyCheck - 1;
  delay(1000); // Check schedule every second

}
