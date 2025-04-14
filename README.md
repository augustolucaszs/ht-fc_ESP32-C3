
# 💧 HT-FC – Flow and Consumption Control with ESP32-C3 (PlatformIO)

This is a PlatformIO project for controlling a solenoid valve and monitoring water flow and daily consumption using an ESP32-C3. It supports Wi-Fi configuration, MQTT integration, and OTA updates.

## ✨ Features

- ✅ **Wi-Fi configuration portal** when no credentials are saved
- ✅ **MAC address identification** and MQTT-based registration
- ✅ **Daily consumption value** received dynamically via MQTT
- ✅ **MQTT-based OTA updates** with automatic verification of MAC address - not tested
- ✅ **Persistent storage** of Wi-Fi credentials using `Preferences.h`
- ✅ **JSON payload support** with `ArduinoJson`
- ✅ **PlatformIO-compatible** structure for easier development

## 📁 Project Structure

```
HT-FC/
├── include/           # For header files like secrets.h
├── lib/               # Optional libraries
├── src/               # Main source code (main.cpp)
├── test/              # Unit tests (optional)
├── .vscode/           # VSCode settings
├── platformio.ini     # PlatformIO configuration file
└── .gitignore         # Files to be ignored by Git
```

## 🔐 `secrets.h` file

This file is not included in the repo. Create it inside the `include/` folder:

```cpp
const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";
const char* mqttServer = "MQTT_SERVER_IP";
const int   mqttPort = 1883;
const char* mqttUsername = "MQTT_USERNAME";
const char* mqttPassword = "MQTT_PASSWORD";
const char* otaPassword = "OTA_PASSWORD";
```

## 🚀 How to Use

1. Clone the repo:
   ```bash
   git clone https://github.com/augustolucaszs/ht-fc_ESP32-C3.git
   ```
2. Create the `secrets.h` file as shown above.
3. Compile and upload with:
   ```bash
   platformio run --target upload
   ```

## 🔁 Behavior Summary

- If Wi-Fi credentials are not found, it starts an **Access Point** (`HTFC - 10.0.0.1`) with a configuration page.
- Once connected, it:
  - Sends the device MAC address to the MQTT `accessRequest/` topic
  - Subscribes to:
    - `esp/ota/` for OTA updates
    - Its own MAC address topic to receive consumption updates
- Parses JSON payloads to:
  - Update firmware via OTA if the MAC matches - not tested.
  - Save and publish received daily consumption every minute.

## 🛠️ Dependencies

- `WiFi`
- `WebServer`
- `Preferences`
- `PubSubClient`
- `ArduinoOTA`
- `ArduinoJson`
- `HTTPClient`, `HTTPUpdate`

## ⚠️ Security

> Do **NOT** version the `secrets.h` file. It's listed in `.gitignore`.

## 🧑‍💻 Author

- Lucas Augusto – [@augustolucaszs](https://github.com/augustolucaszs)
