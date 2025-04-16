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

#define LED_WIFI 2 // Blue LED on GPIO2
#define interruptPin 3
#define relayPin 19

volatile unsigned int pulseCount = 0;
const unsigned long debounceDelay = 150;  // in ms — adjust depending on your reed
unsigned long lastReportTime = 0;
volatile unsigned long lastInterruptTime = 0;

WiFiClient espClient;
PubSubClient client(espClient);

void callback(char* topic, byte* payload, unsigned int length);

Preferences prefs;
WebServer server(80);

bool inConfigMode = false;

float dayConsumption = 0.0; // Global variable to store the day consumption value
String mac = ""; // Global variable to store the MAC address
boolean debug = false; // Debug mode flag

// Configuration page HTML
const char* configPage = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="UTF-8"><title>Configure Wi-Fi</title></head>
<body>
  <h2>Configure Wi-Fi</h2>
  <form action="/save" method="POST">
    SSID: <input name="ssid"><br><br>
    Password: <input name="password" type="password"><br><br>
    <input type="submit" value="Save and Connect">
  </form>
</body>
</html>
)rawliteral";

// Saves the new Wi-Fi data and restarts
void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.end();

  server.send(200, "text/html", "<h2>Saved! Restarting...</h2>");
  delay(2000);
  ESP.restart();
}

// Main page in AP mode
void handleRoot() {
  server.send(200, "text/html", configPage);
}

// Starts the Access Point with configuration page
void startConfigPortal() {
  inConfigMode = true;
  WiFi.mode(WIFI_AP);
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

// Connects to the Wi-Fi saved in flash
bool connectToWiFi() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");
  prefs.end();

  if (ssid == "") {
    Serial.println("No saved network");
    return false;
  }

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("Connecting to: " + ssid);

  int attempts = 0;
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < 5000) {
    digitalWrite(LED_WIFI, !digitalRead(LED_WIFI));
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to Wi-Fi. Entering configuration mode.");
    startConfigPortal(); // Fallback to configuration mode
    return false;
  }

  Serial.println("Successfully connected to Wi-Fi!");
  return true;
}

// Connection to the MQTT
void connectToMQTT() {
  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  while (!client.connected()) {
    Serial.println("Connecting to the MQTT broker...");

    if (client.connect("ESP32Client", mqttUsername, mqttPassword)) {
      Serial.println("Connected to the MQTT!");

      String otaTopic = "esp/ota/";
      client.subscribe(otaTopic.c_str());
      Serial.print("Subscribed to OTA topic: ");
      Serial.println(otaTopic);

      client.subscribe(mac.c_str());
      Serial.print("Subscribed to macAddress device's topic: ");
      Serial.println(mac);

      String resetWifiTopic = mac + "/resetWifi";
      client.subscribe(resetWifiTopic.c_str());
      Serial.print("Subscribed to reset Wi-Fi topic: ");
      Serial.println(resetWifiTopic);

      Serial.print("Sending MAC address and IP address to the topic: ");
      Serial.println(mqttTopic);

      // Create a JSON payload with MAC address and IP address
      DynamicJsonDocument doc(256);
      doc["mac"] = mac;
      doc["ip"] = WiFi.localIP().toString();

      String jsonPayload;
      serializeJson(doc, jsonPayload);

      // Publish the JSON payload to the topic
      client.publish(mqttTopic, jsonPayload.c_str());
      
    } else {
      Serial.print("Failed to connect to the MQTT. rc=");
      Serial.print(client.state());
      Serial.println(" Trying again in 5 sec...");
      delay(5000);
    }
  }
}

void resetWiFiCredentials() {
  // Clear saved Wi-Fi credentials
  prefs.begin("wifi", false);
  prefs.remove("ssid");
  prefs.remove("password");
  prefs.end();

  Serial.println("Wi-Fi credentials cleared. Restarting in 5 seconds...");

  // Blink the blue LED once per second for 5 seconds
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_WIFI, HIGH);
    delay(500);
    digitalWrite(LED_WIFI, LOW);
    delay(500);
  }

  // Restart the ESP
  ESP.restart();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);

  if (topicStr == mac + "/resetWifi") {
    // Convert payload to a string
    String payloadStr;
    for (unsigned int i = 0; i < length; i++) {
        payloadStr += (char)payload[i];
    }

    // Debug: Print the received payload
    Serial.print("Received payload: ");
    Serial.println(payloadStr);

    // Check if the payload is "true" (case-insensitive)
    if (payloadStr.equalsIgnoreCase("true") || payloadStr == "1") {
      Serial.println("Reset Wi-Fi topic received. Clearing credentials...");
      
      // Blink the blue LED 3 times
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_WIFI, HIGH);
        delay(200);
        digitalWrite(LED_WIFI, LOW);
        delay(200);
      }

      resetWiFiCredentials();
    } else {
      Serial.println("Payload did not match 'true'. Ignoring.");
    }
  } else if (topicStr == WiFi.macAddress()) {
    // Convert payload to a string
    String payloadStr;
    for (unsigned int i = 0; i < length; i++) {
      payloadStr += (char)payload[i];
    }

    // Convert the payload string to a float
    dayConsumption = payloadStr.toFloat();
    Serial.print("Received dayConsumption value: ");
    Serial.println(dayConsumption);

    // Publish the dayConsumption value to a test topic for verification
    String testMqttTopic = mac + "/test";
    String dayConsumptionStr = String(dayConsumption, 2); // Convert to string with 2 decimal places
    client.publish(testMqttTopic.c_str(), dayConsumptionStr.c_str());

  } else {
    JsonDocument doc;
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
  pinMode(relayPin, OUTPUT);
  pinMode(interruptPin, INPUT);

  digitalWrite(LED_WIFI, HIGH);
  digitalWrite(relayPin, LOW); // Relay off
  attachInterrupt(digitalPinToInterrupt(interruptPin), handleInterrupt, FALLING); // RISING since LOW -> HIGH
  
  Serial.begin(115200);
  delay(1000);

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
        detachInterrupt(digitalPinToInterrupt(interruptPin)); // Disable interrupts during OTA
            })
            .onEnd([]() {
        Serial.println("\nOTA completed!");
        attachInterrupt(digitalPinToInterrupt(interruptPin), handleInterrupt, FALLING); // Re-enable interrupts
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
}

void loop() {
  if (inConfigMode) {
    server.handleClient();
    digitalWrite(LED_WIFI, !digitalRead(LED_WIFI));
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
    client.loop();
  }

  if (millis() - lastReportTime >= 60000) {
    noInterrupts();
    unsigned int liters = pulseCount;
    pulseCount = 0;
    interrupts();
  
    Serial.print("Liters in last minute: ");
    Serial.println(liters);
    dayConsumption += liters;

    // Compose MQTT topic and message
    String debugTopic = mac + "/debug";
    String payload = "Liters in last minute: " + String(liters);

    if (!client.connected()) {
      Serial.println("MQTT disconnected. Reconnecting...");
      connectToMQTT(); // reconecta se necessário
    }
    
    // Publish to MQTT
    if (client.connected()) {
      client.publish(debugTopic.c_str(), payload.c_str(), true);
    } else {
      Serial.println("MQTT not connected — skipping publish.");
    }

    lastReportTime = millis();
  }
  
}

