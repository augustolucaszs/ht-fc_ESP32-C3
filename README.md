
# 💧 ht-fc v1.1 – Flow Control with ESP32-C3

Arduino-based project for controlling a solenoid valve using an ESP32-C3, featuring:

- Flow measurement (pulse-based sensor)
- Manual/automatic mode via button
- Wi-Fi connection with fallback AP mode
- MQTT communication with backend
- Remote OTA updates

## 📦 Main Files

- `ht-fc_v1.1.ino`: Main ESP-C3 logic
- `secrets.h`: Credentials file (not included in Git)
- `.gitignore`: Ignores sensitive and temporary files

## 🚀 How to Use

1. Clone this repository:
   ```bash
   git clone https://github.com/augustolucaszs/ht-fc_v1.1.git
   ```
2. Add a `secrets.h` file with the following content:
   ```cpp
   const char* ssid = "YOUR_WIFI";
   const char* password = "YOUR_PASSWORD";
   const char* mqttServer = "MQTT_SERVER_IP";
   const int   mqttPort = 1883;
   const char* mqttUser = "MQTT_USER";
   const char* mqttPassword = "MQTT_PASSWORD";
   ```

3. Compile and upload to your **ESP32-C3** board using the Arduino IDE.

## 🛠️ Dependencies

- WiFi / WiFiManager libraries
- PubSubClient
- ArduinoJson
- HTTPUpdate (for OTA)

## 📡 MQTT

The device publishes data in the following format:

```json
{
  "Vazao": 3.45,
  "VDia": 12.3,
  "Modo": 1,
  "Valv": 0,
  "Limt": 20.0
}
```

## 🧪 OTA

Upon receiving an MQTT command with a valid URL on the `esp32/ota` topic, the ESP32-C3 performs a remote update.

## ⚠️ Security

> The `secrets.h` file **must not be versioned**. It is listed in `.gitignore`.

## 🧑‍💻 Author

- Lucas Augusto – [@augustolucaszs](https://github.com/augustolucaszs)
