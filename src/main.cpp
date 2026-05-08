#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SensirionI2cScd4x.h>

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

  scd4x.begin(Wire, 0x62);
  scd4x.stopPeriodicMeasurement();
  delay(500);
  scd4x.startPeriodicMeasurement();

  // ===== PMS5003 UART =====
  pmsSerial.begin(9600, SERIAL_8N1, 16, 17);

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

    // ===== SCD41 =====
    uint16_t co2;
    float temp;
    float hum;

    uint16_t error = scd4x.readMeasurement(co2, temp, hum);

    if (error || co2 == 0)
    {
      Serial.println("SCD41 not ready");
      return;
    }

    // ===== PMS5003 =====
    int pm1 = -1, pm2_5 = -1, pm10 = -1;

    if (pmsSerial.available() >= 32)
    {
      if (pmsSerial.read() == 0x42)
      {
        if (pmsSerial.read() == 0x4D)
        {
          pmsSerial.readBytes(pmsBuffer, 30);

          pm1 = (pmsBuffer[4] << 8) | pmsBuffer[5];
          pm2_5 = (pmsBuffer[6] << 8) | pmsBuffer[7];
          pm10 = (pmsBuffer[8] << 8) | pmsBuffer[9];
        }
      }

      // 清空殘留資料
      while (pmsSerial.available())
        pmsSerial.read();
    }

    Serial.printf("Temp:%.1f Hum:%.1f CO2:%d PM1.0:%d PM2.5:%d PM10:%d\n",
                  temp, hum, co2, pm1, pm2_5, pm10);

    // ===== JSON 整合 =====
    char msg[150];
    if (pm1 < 0 || pm2_5 < 0 || pm10 < 0)
    {
      Serial.println("Skip invalid PMS data");
      return;
    }
    sprintf(msg,
            "{\"temp\":%.1f,\"hum\":%.1f,\"co2\":%d,\"pm1\":%d,\"pm2_5\":%d,\"pm10\":%d}",
            temp, hum, co2, pm1, pm2_5, pm10);

    client.publish("airbox/data", msg);
  }
}