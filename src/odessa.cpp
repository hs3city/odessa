#include <Arduino.h>

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h> // https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA
#include <ArduinoJson.h>                     // https://arduinojson.org/
#include <EspMQTTClient.h>                   // https://github.com/plapointe6/EspMQTTClient

#include "hack-regular-4.h"

#include "settings.h"

#define MAX_PAYLOAD 1024

#define PANEL_RES_X 128 // Number of pixels wide of each INDIVIDUAL panel module.
#define PANEL_RES_Y 64  // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1   // Total number of panels chained one to another

#ifdef ARDUINO_MH_ET_LIVE_ESP32MINIKIT
#define E_PIN 18
#else
#define E_PIN 32
#endif

// MatrixPanel_I2S_DMA dma_display;
MatrixPanel_I2S_DMA *dma_display = nullptr;

uint16_t myBLACK = dma_display->color565(0, 0, 0);
uint16_t myWHITE = dma_display->color565(255, 255, 255);
uint16_t myRED = dma_display->color565(255, 0, 0);
uint16_t myGREEN = dma_display->color565(0, 255, 0);
uint16_t myBLUE = dma_display->color565(0, 0, 255);

const bool hack_font = true;

std::vector<std::tuple<std::string, std::string, std::string>> transport_times;

EspMQTTClient client(
    ssid,
    password,
    MQTT_BROKER,
    MQTT_USER,
    MQTT_PASSWORD,
    "Odessa",
    MQTT_BROKER_PORT
);

DynamicJsonDocument doc(MAX_PAYLOAD);

void drawText()
{
  dma_display->setTextSize(1);     // size 1 == 8 pixels high
  dma_display->setTextWrap(false); // Don't wrap at end of line - will do ourselves

  if (hack_font)
  {
    dma_display->setCursor(1, 9);
  }
  else
  {
    dma_display->setCursor(0, 1);
  }

  uint8_t w = 0;
  char buffer[100];

  for (auto &element : transport_times)
  {
    dma_display->setTextColor(myGREEN);

    dma_display->setCursor(0, 9 * w + 8);
    snprintf(buffer, 100, "%2d", atoi(std::get<0>(element).c_str()));
    dma_display->println(buffer);

    dma_display->setTextColor(myBLUE);
    dma_display->setCursor(12, 9 * w + 8);
    snprintf(buffer, 100, "%-20s", std::get<1>(element).substr(0, 20).c_str());
    dma_display->println(buffer);

    dma_display->setTextColor(myRED);
    dma_display->setCursor(116, 9 * w + 8);
    snprintf(buffer, 100, "%2d", atoi(std::get<2>(element).c_str()));
    dma_display->println(buffer);
    w++;
  }
}

void onConnectionEstablished()
{
  Serial.println("Connection established");

  client.subscribe(topic, [](const String &payload)
  {
    Serial.println(payload);

    deserializeJson(doc, payload);

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
      dma_display->clearScreen();
    }
  });
}

void setup()
{
  Serial.begin(115200);

  client.setMaxPacketSize(MAX_PAYLOAD);

  transport_times.emplace_back(std::make_tuple("5", "żółty", "2"));
  transport_times.emplace_back(std::make_tuple("12", "Hackerspace", "12"));

  HUB75_I2S_CFG mxconfig(
      PANEL_RES_X,
      PANEL_RES_Y,
      PANEL_CHAIN);

  mxconfig.gpio.e = E_PIN;

  // Display Setup
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->utf8(true);
  dma_display->setBrightness8(90); // 0-255
  dma_display->clearScreen();

  if (hack_font)
  {
    dma_display->setFont(&Hack_Regular4pt8b);
  }

  dma_display->fillRect(0, 0, dma_display->width(), dma_display->height(), dma_display->color444(0, 15, 0));
  delay(500);

  dma_display->drawRect(0, 0, dma_display->width(), dma_display->height(), dma_display->color444(15, 15, 0));
  delay(500);

  dma_display->drawLine(0, 0, dma_display->width() - 1, dma_display->height() - 1, dma_display->color444(15, 0, 0));
  dma_display->drawLine(dma_display->width() - 1, 0, 0, dma_display->height() - 1, dma_display->color444(15, 0, 0));
  delay(500);

  dma_display->fillScreen(dma_display->color444(0, 0, 0));
}

void loop()
{
  client.loop();

  // animate by going through the colour wheel for the first two lines
  drawText();

  delay(20);
}
