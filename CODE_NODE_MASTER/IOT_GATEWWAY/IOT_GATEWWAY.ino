#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#define LED 2
#define WIFI_SSID "XXX"
#define WIFI_PASSWORD "XXXXXX"
#define TOKEN "XXXXXXXX"
#define THINGSBOARD_SERVER "mqtt.thingsboard.cloud"

String GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/XXXX/exec";

WiFiClient espClient;
PubSubClient client(espClient);
HardwareSerial STM32_Serial(2);

void setup_wifi() {
  pinMode(LED, OUTPUT);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  digitalWrite(LED, HIGH);
}

void reconnect_mqtt() {
  while (!client.connected()) {
    if (client.connect("ESP32_Bus_Bridge", TOKEN, NULL)) {
    } else {
      delay(3000);
    }
  }
}

String sendToGoogle(String rfid) {
  if (WiFi.status() != WL_CONNECTED) return "ERROR_WIFI";

  WiFiClientSecure clientSecure;
  clientSecure.setInsecure();

  HTTPClient http;
  String url = GOOGLE_SCRIPT_URL + "?id=" + rfid;

  http.begin(clientSecure, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  String payload = "ERROR";

  if (httpCode > 0) {
    payload = http.getString();
  }

  http.end();
  payload.trim();

  return payload;
}

void setup() {
  Serial.begin(115200);
  STM32_Serial.begin(115200, SERIAL_8N1, 16, 17);
  setup_wifi();
  client.setServer(THINGSBOARD_SERVER, 1883);
}

void loop() {
  if (!client.connected()) {
    reconnect_mqtt();
  }

  client.loop();

  if (STM32_Serial.available()) {
    String input = STM32_Serial.readStringUntil('\n');
    input.trim();

    Serial.println("RAW tu STM32: " + input);

    if (input.length() > 0) {

      if (input.startsWith("RFID:")) {
        String rfidCode = input.substring(5);
        String response = sendToGoogle(rfidCode);
        STM32_Serial.println("GS:" + response);
        Serial.println(response);
      }

      else if (input.startsWith("TB:")) {
        String jsonPayload = input.substring(3);
        int lastBrace = jsonPayload.lastIndexOf('}');

        if (lastBrace != -1) {
          String cutJson = jsonPayload.substring(0, lastBrace);

          float fakeTemp = 25.0 + (random(0, 11) / 10.0);
          int fakeHum = random(50, 55);

          if (cutJson.length() > 1) {
            cutJson += ",";
          }

          cutJson += "\"temperature\":" + String(fakeTemp, 1) +
                     ",\"humidity\":" + String(fakeHum) + "}";

          jsonPayload = cutJson;
        }

        Serial.println("Du lieu thuc te day len TB: " + jsonPayload);
        client.publish("v1/devices/me/telemetry", (char*)jsonPayload.c_str());
      }

      else if (input.equalsIgnoreCase("sos")) {
        Serial.println("goi dien thoai canh bao");
      }
    }
  }
}