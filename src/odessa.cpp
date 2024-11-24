#include <Arduino.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <WifiManager.h>
#include <ArduinoJson.h>
#include <DMD_RGB.h>
#include <ezTime.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include "hack-regular-4.h"
#include "settings.h"

#define MAX_PAYLOAD 1024
// #define PANEL_RES_X 128  // Pixels wide for each panel module
// #define PANEL_RES_Y 64   // Pixels tall for each panel module
// #define PANEL_CHAIN 1    // Number of panels chained

// #ifdef ARDUINO_MH_ET_LIVE_ESP32MINIKIT
// #define E_PIN 18
// #else
// #define E_PIN 32
// #endif

#define DMD_PIN_nOE  15   // Pin nOE (enable)
#define DMD_PIN_SCLK 14   // Pin SCLK (serial clock)
#define DMD_PIN_SDIN 13   // Pin SDIN (serial data input)
#define DMD_PIN_LAT  12   // Pin LAT (latch)
#define DMD_PIN_A    6    // Pin A (row select)
#define DMD_PIN_B    7    // Pin B (row select)
#define DMD_PIN_C    8    // Pin C (row select)
#define DMD_PIN_D    9    // Pin D (row select)
#define DMD_PIN_E    10   // Pin E (row select)
#define DMD_PIN_F    11   // Pin F (row select)
uint8_t mux_list[] = { DMD_PIN_A, DMD_PIN_B, DMD_PIN_C, DMD_PIN_D, DMD_PIN_E };

// Parametry ekranu
#define DISPLAYS_ACROSS 2    // 2 displays across
#define DISPLAYS_DOWN  1     // 1 display down

// custom RGB pins
uint8_t custom_rgbpins[] = { 3, 4, 5 };

DMD_RGB <RGB64x64plainS32, COLOR_4BITS> dmd(mux_list, DMD_PIN_nOE, DMD_PIN_SCLK, custom_rgbpins, DISPLAYS_ACROSS, DISPLAYS_DOWN);

//DMD *display = nullptr;  // This will be a pointer to your DMD display

uint16_t myBLACK = 0x0000;
uint16_t myWHITE = 0xFFFF;
uint16_t myRED = 0xF800;
uint16_t myGREEN = 0x07E0;
uint16_t myBLUE = 0x001F;

const bool hack_font = true;

std::vector<std::tuple<std::string, std::string, std::string>> transport_times;

enum PowerSwitch { off = 0, on = 1, unknown = -1 };

const char* on_state = "ON";
const char* off_state = "OFF";

PowerSwitch powerSwitch = PowerSwitch::unknown;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

DynamicJsonDocument doc(MAX_PAYLOAD);

AsyncWebServer server(80);

Timezone tz;

unsigned long ota_progress_millis = 0;
unsigned long mqtt_reconnection_progress_millis = 0;
unsigned long last_gratuitious_message_send_time = 0;
unsigned long one_minute = 60 * 1000;
unsigned long five_seconds = 5 * 1000;

const char* homeassistant_discovery_topic_prefix = "homeassistant/switch";

// Display initialization
DMD *display;

void setupDisplay() {
  // int width = PANEL_RES_X;
  // int height = PANEL_RES_Y;
  // int chain = PANEL_CHAIN;

  // Initialize the display object from the DMD library
  display = new DMD_RGB<1, 64, 64, 1, 1, COLOR_4BITS>(mux_list, DMD_PIN_nOE, DMD_PIN_SCLK, custom_rgbpins, DISPLAYS_ACROSS, DISPLAYS_DOWN);


  // Set up your panel's GPIO pins, as required by your configuration
  display->init();
  
  // Set up any other initial parameters, such as brightness, clear screen, etc.
  display->clearScreen(0);
  display->setBrightness(128);  // Set to a reasonable value for your display
}

void drawText() {
  if (PowerSwitch::on == powerSwitch) {
    display->clearScreen(0);  // Clear the display

    display->setTextSize(1);  // Set text size
    display->setTextColor(myWHITE);  // Set text color

    if (hack_font) {
      display->setCursor(1, 9);  // Adjust for font size and positioning
    } else {
      display->setCursor(0, 1);
    }

    display->println(tz.dateTime("y-M-d"));  // Print current date
    display->setCursor(86, 8);
    display->println(tz.dateTime("H:i:s"));  // Print current time

    uint8_t w = 1;
    char buffer[100];

    for (auto &element : transport_times) {
      display->setTextColor(myGREEN);  // Set color for number
      display->setCursor(0, 9 * w + 8);
      snprintf(buffer, 100, "%2d", atoi(std::get<0>(element).c_str()));
      display->println(buffer);

      display->setTextColor(myBLUE);  // Set color for direction
      display->setCursor(12, 9 * w + 8);
      snprintf(buffer, 100, "%-20s", std::get<1>(element).substr(0, 20).c_str());
      display->println(buffer);

      display->setTextColor(myRED);  // Set color for time
      display->setCursor(116, 9 * w + 8);
      snprintf(buffer, 100, "%2d", atoi(std::get<2>(element).c_str()));
      display->println(buffer);
      w++;
    }

    // display->updateScreen();  // Update the display after drawing text
  }
}

void sendDisplayState() {
  Serial.println("Sending display state message");
  if (PowerSwitch::on == powerSwitch) {
    client.publish(switchStateTopic, on_state);
  } else {
    client.publish(switchStateTopic, off_state);
  }
}

void turnDisplayOff() {
  Serial.println("Turn display off\n");
  powerSwitch = PowerSwitch::off;
  display->clearScreen(0);  // Clear the display to show it is off
  //display->updateScreen(); -> because of no method updateScreen available
}

void turnDisplayOn() {
  Serial.println("Turn display on\n");
  powerSwitch = PowerSwitch::on;
  drawText();  // Redraw the screen with the necessary content
}

void handleFeedUpdate(byte *payload, unsigned int length) {
  Serial.println("Handling feed update");
  deserializeJson(doc, payload, length);

  JsonArray ztm = doc["ztm"];
  transport_times.clear();

  for (JsonObject transport : ztm) {
    std::string number = transport["n"];
    std::string direction = transport["d"];
    std::string time = transport["t"];
    transport_times.emplace_back(make_tuple(number, direction, time));
  }
}

void handleSwitchStateUpdate(byte *payload, unsigned int length) {
  Serial.println("Handling switch update");
  if (strncmp(on_state, (const char *)payload, length) == 0) {
    turnDisplayOn();
    sendDisplayState();
  }
  if (strncmp(off_state, (const char *)payload, length) == 0) {
    turnDisplayOff();
    sendDisplayState();
  }
}

void sendHassDiscoveryMessage() {
  char homeassistant_discovery_topic[256];
  char serialized_json[MAX_PAYLOAD];

  Serial.println("Sending Discovery message");
  snprintf(homeassistant_discovery_topic, 256, "%s/%s/config", homeassistant_discovery_topic_prefix, client_id);
  doc.clear();

  doc["name"] = "Tramwajomat";
  doc["device_class"] = "switch";
  doc["state_topic"] = switchStateTopic;
  doc["command_topic"] = switchSetTopic;
  doc["availability_topic"] = availabilityTopic;
  doc["unique_id"] = client_id;

  serializeJson(doc, serialized_json);

  client.publish(homeassistant_discovery_topic, (const uint8_t*)serialized_json, strlen(serialized_json), true);
  client.publish(availabilityTopic, "online");
}

void sendGratuitiousMessages() {
  sendHassDiscoveryMessage();
  sendDisplayState();
}

void onConnectionEstablished() {
  Serial.println("Connection established");
  client.subscribe(feedTopic);
  client.subscribe(switchSetTopic);
  client.setCallback(
      [](char* messageTopic, uint8_t* payload, unsigned int length) {
        if (strcmp(messageTopic, feedTopic) == 0) {
            handleFeedUpdate(payload, length);
        }
        if (strcmp(messageTopic, switchSetTopic) == 0) {
            handleSwitchStateUpdate(payload, length);
        }
      });

  sendGratuitiousMessages();
}

void setupSerial() { Serial.begin(115200); }

void setupFilesystem() { LittleFS.begin(); }

void setupWifi() {
  bool connected = WifiManager.connectToWifi();
  if (!connected) {
    WifiManager.startManagementServer("Tramwajomat");
  }
}

void setupMqtt() {
  client.setServer(MQTT_BROKER, MQTT_BROKER_PORT);
  client.setBufferSize(MAX_PAYLOAD);
}

void onOTAStart() {
    Serial.println("OTA Update Started");
}

void onOTAProgress(unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\n", (progress * 100) / total);
}

void onOTAEnd(bool success) {
    if (success) {
        Serial.println("OTA Update Completed Successfully");
    } else {
        Serial.println("OTA Update Failed");
    }
}


void setupOta() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Go away! Nothing to see here!");
  });

  ElegantOTA.begin(&server);
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);

  server.begin();
}

void setup() {
  setupSerial();
  setupFilesystem();
  setupDisplay();
  setupWifi();
  setupMqtt();
  setupOta();

  tz.setLocation("Europe/Warsaw");
  waitForSync();

  transport_times.emplace_back(std::make_tuple("5", "żółty", "2"));
  transport_times.emplace_back(std::make_tuple("12", "Hackerspace", "12"));

  turnDisplayOn();
}

bool timerIsClean() {return mqtt_reconnection_progress_millis == 0;}
bool timerIsTimedOut() {return (mqtt_reconnection_progress_millis > 0 && millis() - mqtt_reconnection_progress_millis > five_seconds);}

void reconnectMqtt() {
  if (timerIsClean() || timerIsTimedOut()) {
    Serial.println("Connecting to MQTT broker using supplied credentials");
    bool connected = client.connect(client_id, MQTT_USER, MQTT_PASSWORD);

    if (connected) {
      mqtt_reconnection_progress_millis = 0;
      onConnectionEstablished();
    } else {
      Serial.printf("MQTT connection failed, rc=%d, retry in 5 seconds\n", client.state());
      mqtt_reconnection_progress_millis = millis();
    }
  }
}

void mqttLoop() {
  if (millis() - last_gratuitious_message_send_time > one_minute) {
    sendGratuitiousMessages();
    last_gratuitious_message_send_time = millis();
  }
  client.loop();
}

void loop() {
  WifiManager.check();
  ElegantOTA.loop();

  if (!client.connected()) {
    reconnectMqtt();
  } else {
    mqttLoop();
  }

  drawText();
  delay(50);
}