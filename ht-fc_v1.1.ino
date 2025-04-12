#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include "secrets.h"


WiFiClient espClient;
PubSubClient client(espClient);


#define LED_WIFI 2 // LED azul no GPIO2

Preferences prefs;
WebServer server(80);

bool inConfigMode = false;

// Página de configuração HTML
const char* configPage = R"rawliteral(
<!DOCTYPE html><html>
<head><meta charset="UTF-8"><title>Configurar Wi-Fi</title></head>
<body>
  <h2>Configurar Wi-Fi</h2>
  <form action="/save" method="POST">
    SSID: <input name="ssid"><br><br>
    Senha: <input name="password" type="password"><br><br>
    <input type="submit" value="Salvar e Conectar">
  </form>
</body>
</html>
)rawliteral";

// Salva os novos dados de Wi-Fi e reinicia
void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.end();

  server.send(200, "text/html", "<h2>Salvo! Reiniciando...</h2>");
  delay(2000);
  ESP.restart();
}

// Página principal no modo AP
void handleRoot() {
  server.send(200, "text/html", configPage);
}

// Inicia o Access Point com página de configuração
void startConfigPortal() {
  inConfigMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("HTFC - 10.0.0.1");

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();
  Serial.println("Modo configuração iniciado em http://10.0.0.1");
}

// Conecta-se ao Wi-Fi salvo na flash
bool connectToWiFi() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");
  prefs.end();

  if (ssid == "") {
    Serial.println("Nenhuma rede salva");
    return false;
  }

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.println("Conectando-se a: " + ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 50) {
    digitalWrite(LED_WIFI, !digitalRead(LED_WIFI)); // pisca rápido
    delay(100);
    attempts++;
  }

  return WiFi.status() == WL_CONNECTED;
}


// Connection to the MQTT
void connectToMQTT() {
  client.setServer(mqttServer, mqttPort);
  
  while (!client.connected()) {
    Serial.println("Connecting to the MQTT broker...");

    if (client.connect("ESP32Client", mqttUsername, mqttPassword)) {
      Serial.println("Connected to the MQTT!");

      String mac = WiFi.macAddress();
      Serial.println("Sending MACAdress to the topic:");
      Serial.println(mac);

      client.publish(mqttTopic, mac.c_str());
    } else {
      Serial.print("Failed to connect to the MQTT. rc=");
      Serial.print(client.state());
      Serial.println(" Trying again in 5 sec...");
      delay(5000);
    }
  }
}

void setup() {
  pinMode(LED_WIFI, OUTPUT);
  digitalWrite(LED_WIFI, HIGH);
  Serial.begin(115200);
  delay(1000);

  if (!connectToWiFi()) {
    Serial.println("Falha na conexão. Entrando em modo de configuração.");
    startConfigPortal();
  } else {
    Serial.println("Conectado com sucesso!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    digitalWrite(LED_WIFI, LOW); // LED azul fixo
    connectToMQTT(); // <<<<< conecta e publica a MAC

    // OTA Setup
    ArduinoOTA.setHostname("ESP-C3");

    ArduinoOTA
      .onStart([]() {
        Serial.println("Iniciando OTA...");
      })
      .onEnd([]() {
        Serial.println("\nOTA finalizado!");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progresso: %u%%\r", (progress * 100) / total);
      })
      .onError([](ota_error_t error) {
        Serial.printf("Erro OTA [%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Falha na autenticação");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Falha no início");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Falha na conexão");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Falha ao receber");
        else if (error == OTA_END_ERROR) Serial.println("Falha no fim");
      });

    ArduinoOTA.begin();
    Serial.println("OTA pronto! Use a IDE para enviar novos códigos via Wi-Fi.");

  }
}

void loop() {
  if (inConfigMode) {
    server.handleClient();
    // Pisca LED rapidamente
    digitalWrite(LED_WIFI, !digitalRead(LED_WIFI));
    delay(100); // 10 vezes por segundo
  }

  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();          // ✅ Keep OTA alive
    client.loop();                // ✅ Keep MQTT alive
  }
}

