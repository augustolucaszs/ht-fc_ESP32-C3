#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "secrets.h"
#include <ArduinoJson.h>

#define LED_WIFI 2
#define interruptPin 3
#define relayPin 19

volatile unsigned int pulseCount = 0;
const unsigned long debounceDelay = 300;
unsigned long lastReportTime = 0;
volatile unsigned long lastInterruptTime = 0;

unsigned long wifiBlinkTime = 0;
unsigned long mqttReconnectTime = 0;

WiFiClient espClient;
PubSubClient client(espClient);

void callback(char* topic, byte* payload, unsigned int length);

Preferences prefs;
WebServer server(80);

bool inConfigMode = false;

float dayConsumption = 0.0;
String mac = "";
boolean debug = false; 

bool nonBlockingDelay(unsigned long interval, unsigned long &lastTime) {
  unsigned long currentTime = millis();
  if (currentTime - lastTime >= interval) {
    lastTime = currentTime;
    return true; 
  }
  return false; 
}

const char* configPage = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="UTF-8"><title>Configure Wi-Fi</title></head>
<body>
  <h2>Configure Wi-Fi</h2>
  <form action="/save" method="POST">
    CPF: <input name="cpf"><br><br>
    SSID: <input name="ssid"><br><br>
    Password: <input name="password" type="password"><br><br>
    <input type="submit" value="Save and Connect">
  </form>
</body>
</html>
)rawliteral";

void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String cpf = server.arg("cpf");

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.end();

  prefs.begin("user", false);
  prefs.putString("cpf", cpf);
  prefs.end();

  server.send(200, "text/html", "<h2>Saved! Restarting...</h2>");
  delay(2000);
  ESP.restart();
}

void handleRoot() {
  server.send(200, "text/html", configPage);
}

void startConfigPortal() {
  inConfigMode = true;
  IPAddress local_IP(10, 0, 0, 1);
  IPAddress gateway(10, 0, 0, 1);
  IPAddress subnet(255, 255, 255, 0);

  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Failed to configure AP IP address");
  }

  WiFi.softAP("HTFC - 10.0.0.1");

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("Configuration mode started at http://10.0.0.1");
}

bool connectToWiFi() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");
  prefs.end();

  if (ssid == "") {
    Serial.println("No saved network");
    return false;
  }

  WiFi.mode(WIFI_AP_STA);

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("Connecting to: " + ssid);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < 5000) {
    if (nonBlockingDelay(100, wifiBlinkTime)) {
      digitalWrite(LED_WIFI, !digitalRead(LED_WIFI)); 
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to Wi-Fi.");
  } 
  else {
    Serial.println("Successfully connected to Wi-Fi!");
    Serial.print("STA IP: ");
    Serial.println(WiFi.localIP());
  }

  IPAddress local_IP(10, 0, 0, 1);
  IPAddress gateway(10, 0, 0, 1);
  IPAddress subnet(255, 255, 255, 0);

  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Failed to configure AP IP address");
  }

  WiFi.softAP("HTFC - 10.0.0.1");

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();

  Serial.print("AP started. Access it at: ");
  Serial.println(WiFi.softAPIP());

  return WiFi.status() == WL_CONNECTED;
}

void connectToMQTT() {
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  while (!client.connected()) {
    Serial.println("Connecting to the MQTT broker...");

    if (client.connect("ESP32Client", mqttUsername, mqttPassword, "device/status", 1, true, "offline")) {
      Serial.println("Connected to the MQTT!");

      client.publish("device/status", "online", true);

      client.subscribe("esp/ota/");
      client.subscribe(mac.c_str());
      client.subscribe((mac + "/resetWifi").c_str());

      DynamicJsonDocument doc(256);
      doc["mac"] = mac;
      doc["ip"] = WiFi.localIP().toString();
      prefs.begin("user", true);
      doc["cpf"] = prefs.getString("cpf", "");
      prefs.end();

      String jsonPayload;
      serializeJson(doc, jsonPayload);

      client.publish(mqttTopic, jsonPayload.c_str());

    } 
    else {
      Serial.print("Failed to connect to the MQTT. rc=");
      Serial.print(client.state());
      Serial.println(" Trying again in 5 sec...");

      while (!nonBlockingDelay(5000, mqttReconnectTime)) {
        client.loop();
        ArduinoOTA.handle();
      }
    }
  }
}

void resetWiFiCredentials() {
  prefs.begin("wifi", false);
  prefs.remove("ssid");
  prefs.remove("password");
  prefs.end();

  Serial.println("Wi-Fi credentials cleared. Restarting in 5 seconds...");

  unsigned long resetBlinkTime = 0;
  for (int i = 0; i < 5; i++) {
    if (nonBlockingDelay(500, resetBlinkTime)) {
      digitalWrite(LED_WIFI, !digitalRead(LED_WIFI));
    }
  }

  ESP.restart();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);

  if (topicStr == mac + "/resetWifi") {
    String payloadStr;
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += (char)payload[i];
    }

    Serial.print("Received payload: ");
    Serial.println(payloadStr);

    if (payloadStr.equalsIgnoreCase("true") || payloadStr == "1") {
      Serial.println("Reset Wi-Fi topic received. Clearing credentials...");

      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_WIFI, HIGH);
        delay(200);
        digitalWrite(LED_WIFI, LOW);
        delay(200);
      }

      resetWiFiCredentials();
    } 
    else {
      Serial.println("Payload did not match 'true'. Ignoring.");
    }
  } 
  else if (topicStr == WiFi.macAddress()) {
    String payloadStr;
    for (unsigned int i = 0; i < length; i++) {
      payloadStr += (char)payload[i];
    }

    dayConsumption = payloadStr.toFloat();
    Serial.print("Received dayConsumption value: ");
    Serial.println(dayConsumption);

    String testMqttTopic = mac + "/test";
    String dayConsumptionStr = String(dayConsumption, 2);
    client.publish(testMqttTopic.c_str(), dayConsumptionStr.c_str());

  } 
  else {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error) {
      Serial.print("Failed to parse OTA message: ");
      Serial.println(error.c_str());
      return;
    }

    String targetMac = doc["mac"];
    String firmwareURL = doc["url"];

    String localMac = WiFi.macAddress();

    if (targetMac != localMac) {
      Serial.print("OTA message not for this device (");
      Serial.print(localMac);
      Serial.println("). Ignoring.");
      return;
    }

    Serial.println("OTA message is for me. Starting update from:");
    Serial.println(firmwareURL);

    WiFiClient httpClient;
    t_httpUpdate_return ret = httpUpdate.update(httpClient, firmwareURL);

    switch (ret) {
    case HTTP_UPDATE_OK:
      Serial.println("OTA via MQTT succeeded!");
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("No update available.");
      break;
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA via MQTT failed. Error (%d): %s\n",
      httpUpdate.getLastError(),
      httpUpdate.getLastErrorString().c_str());
      break;
    }
  }
}

void IRAM_ATTR handleInterrupt() {
  unsigned long now = millis();
  if (now - lastInterruptTime > debounceDelay) {
    pulseCount++;
    lastInterruptTime = now;
  }
}

void setup() {
  pinMode(LED_WIFI, OUTPUT);
  delay(100);

  if (!connectToWiFi()) {
    Serial.println("Connection failed. Entering configuration mode.");
    startConfigPortal();
  } else {
    Serial.println("Successfully connected!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    digitalWrite(LED_WIFI, LOW);

    mac = WiFi.macAddress();
    connectToMQTT();

    ArduinoOTA.setHostname(("ESP_" + mac).c_str());
    ArduinoOTA.setPassword(otaPassword);

    ArduinoOTA
      .onStart([]() {
        Serial.println("Starting OTA...");
        detachInterrupt(digitalPinToInterrupt(interruptPin));
            })
            .onEnd([]() {
        Serial.println("\nOTA completed!");
        attachInterrupt(digitalPinToInterrupt(interruptPin), handleInterrupt, RISING);
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress * 100) / total);
      })
      .onError([](ota_error_t error) {
        Serial.printf("OTA Error [%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Authentication failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connection failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive failed");
        else if (error == OTA_END_ERROR) Serial.println("End failed");
            });

          ArduinoOTA.begin();
          Serial.println("OTA ready! Use the IDE to upload new code via Wi-Fi.");
  }

  pinMode(interruptPin, INPUT);
  delay(100);
  pinMode(relayPin, OUTPUT);
  delay(100);
  digitalWrite(relayPin, LOW);
  delay(100);

  attachInterrupt(digitalPinToInterrupt(interruptPin), handleInterrupt, RISING); // RISING since LOW -> HIGH
  delay(100);
}

void REconnectToMQTT(){
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP_";
    clientId += mac;

    if (client.connect(clientId.c_str(), mqttUsername, mqttPassword, "device/status", 1, true, "offline")) {
      client.subscribe("esp/ota/");
      client.subscribe(mac.c_str());
      client.subscribe((mac + "/resetWifi").c_str());

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void loop() {
  
  if (inConfigMode) {
    server.handleClient();
  
    if (nonBlockingDelay(100, wifiBlinkTime)) {
        digitalWrite(LED_WIFI, !digitalRead(LED_WIFI));
      }
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected. Reconnecting...");
    connectToWiFi();
    return; 
  }

  ArduinoOTA.handle();

  if (!client.connected()) {
    REconnectToMQTT();
  }
  client.loop();

  if (millis() - lastReportTime >= 60000) {
    Serial.println("Sending data to MQTT...");
    digitalWrite(relayPin, LOW);
    unsigned int liters = pulseCount;
    pulseCount = 0;
  
    Serial.print("Liters in last minute: ");
    Serial.println(liters);
    dayConsumption += liters;

    // Compose MQTT topic and message
    String debugTopic = mac + "/debug";
    String payload = "Liters in last minute: " + String(liters);
    
    client.publish(debugTopic.c_str(), payload.c_str(), true);
    delay(100);

    lastReportTime = millis();
  }
  delay(100);

  }

