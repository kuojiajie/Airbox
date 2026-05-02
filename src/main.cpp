#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SensirionI2cScd4x.h>

// WiFi
const char *ssid = "TP-Link_AD47";
const char *password = "24463478";

// MQTT
const char *mqtt_server = "192.168.0.109";

WiFiClient espClient;
PubSubClient client(espClient);
SensirionI2cScd4x scd4x;

unsigned long lastMsg = 0;

// WiFi
void setup_wifi()
{
  Serial.println("Connecting WiFi...");
  WiFi.begin(ssid, password);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    retry++;
    if (retry > 40)
      ESP.restart(); // 防卡死
  }

  Serial.println("\nWiFi Connected!");
  Serial.println(WiFi.localIP());
}

// MQTT
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

  // ✅ I2C（你確認可用 G8/G9）
  Wire.begin(8, 9);

  // ✅ 初始化 SCD41（正確順序）
  scd4x.begin(Wire, 0x62);

  scd4x.stopPeriodicMeasurement();
  delay(500);

  uint16_t error = scd4x.startPeriodicMeasurement();
  if (error)
  {
    Serial.print("Start measurement failed: ");
    Serial.println(error);
  }

  // WiFi & MQTT
  setup_wifi();
  client.setServer(mqtt_server, 1883);
}

void loop()
{
  if (!client.connected())
    reconnect();
  client.loop();

  // 每5秒讀一次
  if (millis() - lastMsg > 5000)
  {
    lastMsg = millis();

    uint16_t co2;
    float temp;
    float hum;

    uint16_t error = scd4x.readMeasurement(co2, temp, hum);

    if (error)
    {
      Serial.print("Read error: ");
      Serial.println(error);
      return;
    }

    // ❗ 過濾無效值（SCD41 很重要）
    if (co2 == 0)
    {
      Serial.println("Waiting for valid data...");
      return;
    }

    Serial.printf("CO2: %d ppm | Temp: %.2f | Hum: %.2f\n", co2, temp, hum);

    // JSON
    char msg[120];
    sprintf(msg, "{\"temp\":%.2f,\"hum\":%.2f,\"co2\":%d}", temp, hum, co2);

    // MQTT 發送
    if (!client.publish("airbox/data", msg))
    {
      Serial.println("MQTT publish failed");
    }
    else
    {
      Serial.println("Send OK");
    }
  }
}