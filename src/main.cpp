#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SensirionI2cScd4x.h>
#include <Adafruit_BME280.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

#include <HardwareSerial.h>

// =====================================================
// PMS5003
// =====================================================

HardwareSerial pmsSerial(2);

// =====================================================
// WiFi
// =====================================================

const char *ssid = "TP-Link_AD47";
const char *password = "24463478";

// =====================================================
// MQTT
// =====================================================

const char *mqtt_server = "192.168.0.109";

WiFiClient espClient;
PubSubClient client(espClient);

// =====================================================
// SCD41
// =====================================================

SensirionI2cScd4x scd4x;

// =====================================================
// BME280
// =====================================================

Adafruit_BME280 bme;

// =====================================================
// ST7735
// =====================================================

#define TFT_CS 46
#define TFT_DC 45
#define TFT_RST 42

#define TFT_MOSI 41
#define TFT_SCLK 40

Adafruit_ST7735 tft =
    Adafruit_ST7735(&SPI, TFT_CS, TFT_DC, TFT_RST);

// =====================================================
// RYG LED
// =====================================================

#define LED_RED 17
#define LED_YELLOW 18
#define LED_GREEN 15

// =====================================================
// Buzzer
// =====================================================

#define BUZZER_PIN 6

// =====================================================
// Global
// =====================================================

unsigned long lastMsg = 0;

// =====================================================
// WiFi
// =====================================================

void setup_wifi()
{
  Serial.println("Connecting WiFi...");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());
}

// =====================================================
// MQTT
// =====================================================

void reconnect()
{
  while (!client.connected())
  {
    Serial.print("Connecting MQTT...");

    if (client.connect("ESP32Client"))
    {
      Serial.println("connected!");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retry...");

      delay(2000);
    }
  }
}

// =====================================================
// RGB LED
// =====================================================

void setLED(bool r, bool y, bool g)
{
  digitalWrite(LED_RED, r);
  digitalWrite(LED_YELLOW, y);
  digitalWrite(LED_GREEN, g);
}

void updateAirQualityLED(int co2, int pm2_5)
{
  // GOOD
  if (co2 < 800 && pm2_5 < 15)
  {
    setLED(LOW, LOW, HIGH);
  }

  // WARNING
  else if (co2 < 1200 && pm2_5 < 35)
  {
    setLED(LOW, HIGH, LOW);
  }

  // BAD
  else
  {
    setLED(HIGH, LOW, LOW);
  }
}

// =====================================================
// Buzzer
// =====================================================

void updateBuzzer(int co2, int pm2_5)
{
  // =====================================================
  // BAD AIR
  // =====================================================

  if (co2 >= 1200 || pm2_5 >= 35)
  {
    // High frequency continuous alarm
    ledcWriteTone(0, 3500);
  }

  // =====================================================
  // WARNING
  // =====================================================

  else if (co2 >= 800 || pm2_5 >= 15)
  {
    // Short warning beep
    ledcWriteTone(0, 2000);

    delay(120);

    ledcWriteTone(0, 0);
  }

  // =====================================================
  // GOOD
  // =====================================================

  else
  {
    ledcWriteTone(0, 0);
  }
}
// =====================================================
// TFT
// =====================================================

void initDisplay()
{
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.initR(INITR_BLACKTAB);

  // 0~3
  tft.setRotation(2);

  tft.fillScreen(ST77XX_BLACK);

  tft.setTextColor(ST77XX_GREEN);

  tft.setTextSize(2);

  // ===== Centered Hello AirBox =====

  tft.setCursor(28, 38);
  tft.println("Hello");

  tft.setCursor(20, 68);
  tft.println("AirBox");

  Serial.println("TFT OK");

  delay(2000);
}

void drawValues(
    float temp,
    float hum,
    float pressure,
    int co2,
    int pm1,
    int pm2_5,
    int pm10)
{
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(1);

  // =====================================================
  // CO2 Color
  // =====================================================

  uint16_t co2Color = ST77XX_GREEN;

  if (co2 >= 1200)
  {
    co2Color = ST77XX_RED;
  }
  else if (co2 >= 800)
  {
    co2Color = ST77XX_YELLOW;
  }

  // =====================================================
  // PM2.5 Color
  // =====================================================

  uint16_t pm25Color = ST77XX_GREEN;

  if (pm2_5 >= 35)
  {
    pm25Color = ST77XX_RED;
  }
  else if (pm2_5 >= 15)
  {
    pm25Color = ST77XX_YELLOW;
  }

  // =====================================================
  // PM10 Color
  // =====================================================

  uint16_t pm10Color = ST77XX_GREEN;

  if (pm10 >= 75)
  {
    pm10Color = ST77XX_RED;
  }
  else if (pm10 >= 35)
  {
    pm10Color = ST77XX_YELLOW;
  }

  // =====================================================
  // Temperature
  // =====================================================

  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(0, 0);
  tft.print("Temp : ");
  tft.print(temp);
  tft.println(" \xF7"
              "C");

  // =====================================================
  // Humidity
  // =====================================================

  tft.setCursor(0, 15);
  tft.print("Hum  : ");
  tft.print(hum);
  tft.println(" %");

  // =====================================================
  // Pressure
  // =====================================================

  tft.setCursor(0, 30);
  tft.print("Pres : ");
  tft.print(pressure);
  tft.println(" hPa");

  // =====================================================
  // CO2
  // =====================================================

  tft.setTextColor(co2Color);

  tft.setCursor(0, 45);
  tft.print("CO2  : ");
  tft.print(co2);
  tft.println(" ppm");

  // =====================================================
  // PM1.0
  // =====================================================

  tft.setTextColor(ST77XX_WHITE);

  tft.setCursor(0, 60);
  tft.print("PM1  : ");
  tft.print(pm1);
  tft.println(" ug/m3");

  // =====================================================
  // PM2.5
  // =====================================================

  tft.setTextColor(pm25Color);

  tft.setCursor(0, 75);
  tft.print("PM2.5: ");
  tft.print(pm2_5);
  tft.println(" ug/m3");

  // =====================================================
  // PM10
  // =====================================================

  tft.setTextColor(pm10Color);

  tft.setCursor(0, 90);
  tft.print("PM10 : ");
  tft.print(pm10);
  tft.println(" ug/m3");
}

// =====================================================
// Setup
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  // =====================================================
  // RGB LED
  // =====================================================

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  setLED(LOW, LOW, LOW);

  // =====================================================
  // Buzzer
  // =====================================================

  pinMode(BUZZER_PIN, OUTPUT);

  ledcSetup(0, 2000, 8);
  ledcAttachPin(BUZZER_PIN, 0);

  // =====================================================
  // I2C
  // =====================================================

  Wire.begin(4, 5);

  // =====================================================
  // SCD41
  // =====================================================

  scd4x.begin(Wire, 0x62);

  scd4x.stopPeriodicMeasurement();

  delay(500);

  scd4x.startPeriodicMeasurement();

  Serial.println("SCD41 ready");

  // =====================================================
  // BME280
  // =====================================================

  if (!bme.begin(0x76))
  {
    Serial.println("BME280 not found!");
  }
  else
  {
    Serial.println("BME280 ready");
  }

  // =====================================================
  // PMS5003
  // =====================================================

  pmsSerial.begin(
      9600,
      SERIAL_8N1,
      16,
      -1);

  Serial.println("PMS5003 ready");

  // =====================================================
  // TFT
  // =====================================================

  initDisplay();

  // =====================================================
  // WiFi
  // =====================================================

  setup_wifi();

  // =====================================================
  // MQTT
  // =====================================================

  client.setServer(mqtt_server, 1883);
}

// =====================================================
// Loop
// =====================================================

void loop()
{
  if (!client.connected())
    reconnect();

  client.loop();

  if (millis() - lastMsg > 5000)
  {
    lastMsg = millis();

    // =====================================================
    // SCD41
    // =====================================================

    uint16_t co2;
    float scdTemp;
    float scdHum;

    uint16_t error =
        scd4x.readMeasurement(
            co2,
            scdTemp,
            scdHum);

    if (error || co2 == 0)
    {
      Serial.println("SCD41 not ready");
      return;
    }

    // =====================================================
    // BME280
    // =====================================================

    float temp =
        bme.readTemperature();

    float hum =
        bme.readHumidity();

    float pressure =
        bme.readPressure() / 100.0F;

    // =====================================================
    // PMS5003
    // =====================================================

    int pm1 = 0;
    int pm2_5 = 0;
    int pm10 = 0;

    while (pmsSerial.available() >= 32)
    {
      if (pmsSerial.read() == 0x42)
      {
        if (pmsSerial.peek() == 0x4D)
        {
          pmsSerial.read();

          uint8_t buffer[30];

          if (pmsSerial.readBytes(buffer, 30) == 30)
          {
            uint16_t sum = 0x42 + 0x4D;

            for (int i = 0; i < 28; i++)
            {
              sum += buffer[i];
            }

            uint16_t receivedChecksum =
                (buffer[28] << 8) | buffer[29];

            if (sum == receivedChecksum)
            {
              pm1 =
                  (buffer[4] << 8) | buffer[5];

              pm2_5 =
                  (buffer[6] << 8) | buffer[7];

              pm10 =
                  (buffer[8] << 8) | buffer[9];

              break;
            }
          }
        }
      }
    }

    // =====================================================
    // RGB LED Status
    // =====================================================

    updateAirQualityLED(co2, pm2_5);

    // =====================================================
    // Buzzer
    // =====================================================

    updateBuzzer(co2, pm2_5);

    // =====================================================
    // Serial Monitor
    // =====================================================

    Serial.printf(
        "Temp: %.1f °C  Hum: %.1f %%  Pressure: %.1f hPa  CO2: %d ppm  PM1: %d ug/m3  PM2.5: %d ug/m3  PM10: %d ug/m3\n",
        temp,
        hum,
        pressure,
        co2,
        pm1,
        pm2_5,
        pm10);

    // =====================================================
    // TFT
    // =====================================================

    drawValues(
        temp,
        hum,
        pressure,
        co2,
        pm1,
        pm2_5,
        pm10);

    // =====================================================
    // MQTT JSON
    // =====================================================

    char msg[200];

    sprintf(
        msg,
        "{\"temp\":%.1f,\"hum\":%.1f,\"pressure\":%.1f,\"co2\":%d,\"pm1\":%d,\"pm2_5\":%d,\"pm10\":%d}",
        temp,
        hum,
        pressure,
        co2,
        pm1,
        pm2_5,
        pm10);

    // =====================================================
    // MQTT Publish
    // =====================================================

    if (client.publish("airbox/data", msg))
    {
      Serial.println("MQTT Send OK");
    }
    else
    {
      Serial.println("MQTT Send Failed");
    }
  }
}