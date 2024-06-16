#include <Arduino.h>

#include <LittleFS.h>

#include <PubSubClient.h>
#include <WifiManager.h>

#include <ArduinoJson.h> // https://arduinojson.org/
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h> // https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA
#include <ezTime.h>

#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

#include "hack-regular-4.h"

#include "settings.h"

#define MAX_PAYLOAD 1024

#define PANEL_RES_X                                                            \
  128                  // Number of pixels wide of each INDIVIDUAL panel module.
#define PANEL_RES_Y 64 // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1  // Total number of panels chained one to another

#ifdef ARDUINO_MH_ET_LIVE_ESP32MINIKIT
#define E_PIN 18
#else
#define E_PIN 32
#endif

MatrixPanel_I2S_DMA *dma_display = nullptr;

uint16_t myBLACK = dma_display->color565(0, 0, 0);
uint16_t myWHITE = dma_display->color565(255, 255, 255);
uint16_t myRED = dma_display->color565(255, 0, 0);
uint16_t myGREEN = dma_display->color565(0, 255, 0);
uint16_t myBLUE = dma_display->color565(0, 0, 255);

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

void onOTAStart() { Serial.println("OTA update started!"); }

void onOTAProgress(size_t current, size_t final) {
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current,
                  final);
  }
}

void onOTAEnd(bool success) {
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
}

void drawText() {
  if (PowerSwitch::on == powerSwitch) {
    dma_display->setTextSize(1); // size 1 == 8 pixels high
    dma_display->setTextWrap(
        false); // Don't wrap at end of line - will do ourselves

    if (hack_font) {
      dma_display->setCursor(1, 9);
    } else {
      dma_display->setCursor(0, 1);
    }

    dma_display->clearScreen();

    dma_display->setTextColor(myWHITE);

    dma_display->setCursor(0, 8);
    dma_display->println(tz.dateTime("y-M-d"));

    dma_display->setCursor(86, 8);
    dma_display->println(tz.dateTime("H:i:s"));

    uint8_t w = 1;
    char buffer[100];

    for (auto &element : transport_times) {
      dma_display->setTextColor(myGREEN);

      dma_display->setCursor(0, 9 * w + 8);
      snprintf(buffer, 100, "%2d", atoi(std::get<0>(element).c_str()));
      dma_display->println(buffer);

      dma_display->setTextColor(myBLUE);
      dma_display->setCursor(12, 9 * w + 8);
      snprintf(buffer, 100, "%-20s",
               std::get<1>(element).substr(0, 20).c_str());
      dma_display->println(buffer);

      dma_display->setTextColor(myRED);
      dma_display->setCursor(116, 9 * w + 8);
      snprintf(buffer, 100, "%2d", atoi(std::get<2>(element).c_str()));
      dma_display->println(buffer);
      w++;
    }
    dma_display->flipDMABuffer();
  }
}

void sendDisplayState() {
  Serial.println("Sending display state message");
  if (PowerSwitch::on == powerSwitch)
  {
    client.publish(switchStateTopic, on_state);
  } else {
    client.publish(switchStateTopic, off_state);
  }
}

void turnDisplayOff() {
  Serial.println("Turn display off\n");
  powerSwitch = PowerSwitch::off;
  dma_display->clearScreen();
  dma_display->flipDMABuffer();
}

void turnDisplayOn() {
  Serial.println("Turn display on\n");
  powerSwitch = PowerSwitch::on;
  drawText();
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
    Serial.print("Number: ");
    Serial.println(number.c_str());
    Serial.print("Direction: ");
    Serial.println(direction.c_str());
    Serial.print("Time: ");
    Serial.println(time.c_str());
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
      [](const char *messageTopic, byte *payload, unsigned int length) {
        Serial.printf("Got a message in topic %s\n", messageTopic);
        Serial.printf("Payload:\n%.*s\n", length, payload);
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

void setupDisplay() {
  HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);

  mxconfig.gpio.e = E_PIN;
  mxconfig.double_buff = true;

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->utf8(true);
  dma_display->setBrightness8(128); // 0-255
  dma_display->clearScreen();

  if (hack_font) {
    dma_display->setFont(&Hack_Regular4pt8b);
  }

  dma_display->fillRect(0, 0, dma_display->width(), dma_display->height(),
                        dma_display->color444(0, 15, 0));
  dma_display->flipDMABuffer();
  delay(500);

  dma_display->drawRect(0, 0, dma_display->width(), dma_display->height(),
                        dma_display->color444(15, 15, 0));
  dma_display->flipDMABuffer();
  delay(500);

  dma_display->drawLine(0, 0, dma_display->width() - 1,
                        dma_display->height() - 1,
                        dma_display->color444(15, 0, 0));
  dma_display->drawLine(dma_display->width() - 1, 0, 0,
                        dma_display->height() - 1,
                        dma_display->color444(15, 0, 0));
  dma_display->flipDMABuffer();
  delay(500);

  dma_display->fillScreen(dma_display->color444(0, 0, 0));
  dma_display->flipDMABuffer();
}

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

  // Setup placeholders
  transport_times.emplace_back(std::make_tuple("5", "żółty", "2"));
  transport_times.emplace_back(std::make_tuple("12", "Hackerspace", "12"));

  turnDisplayOn();
}

bool timerIsClean() {return mqtt_reconnection_progress_millis == 0;}
bool timerIsTimedOut() {return (mqtt_reconnection_progress_millis > 0 && millis() - mqtt_reconnection_progress_millis > five_seconds);}

void reconnectMqtt() {
  if (timerIsClean() || timerIsTimedOut())
  {
    Serial.println("Connecting to MQTT broker using supplied credentials");
    bool connected = client.connect(client_id, MQTT_USER, MQTT_PASSWORD);

    if (connected) {
      mqtt_reconnection_progress_millis = 0;
      onConnectionEstablished();
    } else {
      Serial.printf("MQTT connection failed, rc=%d, retry in 5 seconds\n",
                    client.state());
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
