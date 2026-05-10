#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SensirionI2cScd4x.h>
#include <Adafruit_BME280.h>

// ===== PMS5003 =====
#include <HardwareSerial.h>
HardwareSerial pmsSerial(2);
uint8_t pmsBuffer[32];

// ===== WiFi =====
const char *ssid = "TP-Link_AD47";
const char *password = "24463478";

// ===== MQTT =====
const char *mqtt_server = "192.168.0.109";

WiFiClient espClient;
PubSubClient client(espClient);

// ===== SCD41 =====
SensirionI2cScd4x scd4x;

// ===== BME280 =====
Adafruit_BME280 bme;

unsigned long lastMsg = 0;

// ===== WiFi =====
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

// ===== MQTT =====
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

void setup()
{
  Serial.begin(115200);

  delay(1000);

  // ===== I2C =====
  Wire.begin(8, 9);

  // ===== SCD41 =====
  scd4x.begin(Wire, 0x62);

  scd4x.stopPeriodicMeasurement();
  delay(500);
  scd4x.startPeriodicMeasurement();

  Serial.println("SCD41 ready");

  // ===== BME280 =====
  if (!bme.begin(0x76))
  {
    Serial.println("BME280 not found!");
  }
  else
  {
    Serial.println("BME280 ready");
  }

  // ===== PMS5003 =====
  pmsSerial.begin(9600, SERIAL_8N1, 16, -1);

  Serial.println("PMS5003 ready");

  // ===== WiFi + MQTT =====
  setup_wifi();

  client.setServer(mqtt_server, 1883);
}

void loop()
{
  if (!client.connected())
    reconnect();

  client.loop();

  // ===== 每5秒讀一次 =====
  if (millis() - lastMsg > 5000)
  {
    lastMsg = millis();

    // =========================
    // SCD41
    // =========================

    uint16_t co2;
    float scdTemp;
    float scdHum;

    uint16_t error = scd4x.readMeasurement(co2, scdTemp, scdHum);

    if (error || co2 == 0)
    {
      Serial.println("SCD41 not ready");
      return;
    }

    // =========================
    // BME280
    // =========================

    float temp = bme.readTemperature();
    float hum = bme.readHumidity();
    float pressure = bme.readPressure() / 100.0F;

    // =========================
    // PMS5003
    // =========================

    int pm1 = -1;
    int pm2_5 = -1;
    int pm10 = -1;

    while (pmsSerial.available() >= 32)
    {
      // 尋找 frame header
      if (pmsSerial.read() == 0x42)
      {
        if (pmsSerial.peek() == 0x4D)
        {
          pmsSerial.read(); // 讀掉 0x4D

          uint8_t buffer[30];

          if (pmsSerial.readBytes(buffer, 30) == 30)
          {
            // checksum 計算
            uint16_t sum = 0x42 + 0x4D;

            for (int i = 0; i < 28; i++)
            {
              sum += buffer[i];
            }

            uint16_t receivedChecksum =
                (buffer[28] << 8) | buffer[29];

            if (sum == receivedChecksum)
            {
              pm1 = (buffer[4] << 8) | buffer[5];
              pm2_5 = (buffer[6] << 8) | buffer[7];
              pm10 = (buffer[8] << 8) | buffer[9];

              break;
            }
            else
            {
              Serial.println("PMS checksum error");
            }
          }
        }
      }
    }

    // PMS 無效值過濾
    if (pm1 < 0)
      pm1 = 0;
    if (pm2_5 < 0)
      pm2_5 = 0;
    if (pm10 < 0)
      pm10 = 0;

    // =========================
    // Serial Monitor
    // =========================

    Serial.printf(
        "Temp:%.1f°C Hum:%.1f%% Pressure:%.1fhPa CO2:%dppm PM1.0:%d PM2.5:%d PM10:%d\n",
        temp,
        hum,
        pressure,
        co2,
        pm1,
        pm2_5,
        pm10);

    // =========================
    // MQTT JSON
    // =========================

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

    // MQTT publish
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