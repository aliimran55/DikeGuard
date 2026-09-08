#include "WlanBackend.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <LittleFS.h>
#include <vector>

#include "bleConfig.h"

// Testdaten für Cluster und Walker
#define CLUSTER_ID          "testCluster"
#define CLUSTER_LATITUDE    53.521516
#define CLUSTER_LONGITUDE   10.014294

#define DEVICE_ID           "walker_test"
#define DEVICE_LATITUDE     53.521516
#define DEVICE_LONGITUDE    10.014294

// ============================================================
// Dieses Modul ist für den WLAN-Pfad der Walker Node zuständig.
// Es sendet gespeicherte HubPackets aus LittleFS über WLAN per MQTT
// an den MQTT-Broker. Der alte Sensor-Event-Pfad bleibt vorerst erhalten.
//
// Wichtig:
// Für Tests am besten Handy-Hotspot nutzen.
// ESP32 unterstützt normalerweise nur 2.4GHz WLan das muss ggf. am Handy eingestellt werden.
// Eduroam ist für Tests ungeeignet wegen Benutzername/Zertifikat.
// ============================================================

// WLan/Hotspot Zugangsdaten HIER AENDERN
#define WIFI_SSID      "HOTSPOT-NAME"
#define WIFI_PASSWORD  "HOTSPOT-PW"


// MQTT-Konfiguration
#define MQTT_BROKER    "broker.hivemq.com"
#define MQTT_PORT      1883
#define MQTT_TOPIC     "julius/test/sensor"


// WiFiClient stellt TCP-Verbindung bereit
// PubSubClient nutzt diese Verbindung für MQTT
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static const char* HUB_PACKET_FILE = "/hubpackets.bin";


WlanBackend::WlanBackend() {
}


void WlanBackend::begin() {
  Serial.println("[WLAN-BACKEND] Starte Backend-Modul...");

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(4096);

  if (!LittleFS.begin(true)) {
    Serial.println("[WLAN-BACKEND] LittleFS konnte nicht gestartet werden.");
  }

  connectWifi();
  connectMqtt();
}


// MQTT-Verbindung aktiv halten
void WlanBackend::loop() {
  if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
    mqttClient.loop();
  }
}


// Wlan Verbindung aufbauen
bool WlanBackend::connectWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.println("[WLAN] Verbinde mit WLAN...");
  Serial.print("[WLAN] SSID: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    Serial.print(".");
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WLAN] Verbindung erfolgreich.");

    Serial.print("[WLAN] ESP32-IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("[WLAN] Gateway-IP: ");
    Serial.println(WiFi.gatewayIP());

    Serial.print("[WLAN] DNS-IP: ");
    Serial.println(WiFi.dnsIP());

    return true;
  }

  Serial.print("[WLAN] Verbindung fehlgeschlagen. WiFi.status() = ");
  Serial.println((int)WiFi.status());

  return false;
}


// MQTT-Verbindung zum Broker aufbauen
bool WlanBackend::connectMqtt() {
  if (mqttClient.connected()) {
    return true;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[MQTT] Kein WLAN, MQTT-Verbindung nicht möglich.");
    return false;
  }

  Serial.print("[MQTT] Verbinde mit Broker: ");
  Serial.println(MQTT_BROKER);

  String clientId = "esp32-wlan-node-";
  clientId += String((uint32_t)ESP.getEfuseMac(), HEX);
  clientId += "-";
  clientId += String(millis());

  Serial.print("[MQTT] Client-ID: ");
  Serial.println(clientId);

  bool ok = mqttClient.connect(clientId.c_str());

  if (ok) {
    Serial.println("[MQTT] Verbindung erfolgreich.");
    return true;
  }

  Serial.print("[MQTT] Verbindung fehlgeschlagen. Fehlercode: ");
  Serial.println(mqttClient.state());

  return false;
}
// Sensor-Event in JSON umwandeln
String WlanBackend::sensorEventToJson(const Event_t& event) {
  const SensorPayload_t& s = event.payload.sensor;

  String json;
  json.reserve(600);

  json += "{";

  json += "\"walkerNode\":true,";

  json += "\"clusterId\":\"";
  json += CLUSTER_ID;
  json += "\",";

  json += "\"clusterLatitude\":";
  json += String(CLUSTER_LATITUDE, 6);
  json += ",";

  json += "\"clusterLongitude\":";
  json += String(CLUSTER_LONGITUDE, 6);
  json += ",";

  json += "\"device_id\":\"";
  json += DEVICE_ID;
  json += "\",";

  json += "\"latitude\":";
  json += String(DEVICE_LATITUDE, 6);
  json += ",";

  json += "\"longitude\":";
  json += String(DEVICE_LONGITUDE, 6);
  json += ",";

  json += "\"temperature\":";
  json += String(s.temperature, 2);
  json += ",";

  json += "\"humidity\":";
  json += String(s.humidity, 2);

  json += "}";

  return json;
}


// Sensor-Event per MQTT an den Broker senden
bool WlanBackend::publishSensorEvent(const Event_t& event) {
  if (!connectWifi()) {
    Serial.println("[WLAN-BACKEND] Publish abgebrochen: WLAN nicht verbunden.");
    return false;
  }

  if (!connectMqtt()) {
    Serial.println("[WLAN-BACKEND] Publish abgebrochen: MQTT nicht verbunden.");
    return false;
  }

  String payload = sensorEventToJson(event);

  Serial.println("[MQTT] Sende Payload:");
  Serial.println(payload);

  bool ok = mqttClient.publish(MQTT_TOPIC, payload.c_str());

  if (ok) {
    Serial.println("[MQTT] Publish erfolgreich.");
  } else {
    Serial.println("[MQTT] Publish fehlgeschlagen.");
    Serial.print("[MQTT] Fehlercode nach Publish: ");
    Serial.println(mqttClient.state());
  }

  return ok;
}


static String hubPacketToJson(const HubPacket& packet) {
  String json;
  json.reserve(1200);

  json += "{";

  json += "\"walkerNode\":true,";
  json += "\"clusterId\":\"";
  json += CLUSTER_ID;
  json += "\",";
  json += "\"device_id\":\"";
  json += DEVICE_ID;
  json += "\",";

  json += "\"hub_id\":";
  json += String(packet.hub_id);
  json += ",";

  json += "\"dl_counter\":";
  json += String(packet.dl_counter);
  json += ",";

  json += "\"timestamp\":";
  json += String(packet.timestamp);
  json += ",";

  json += "\"readings\":[";

  for (int i = 0; i < MAX_READINGS; i++) {
    if (i > 0) {
      json += ",";
    }

    json += "{";
    json += "\"temperature\":";
    json += String(packet.readings[i].temperature);
    json += ",";

    json += "\"humidity\":";
    json += String(packet.readings[i].humidity);
    json += ",";

    json += "\"battery\":";
    json += String(packet.readings[i].battery);
    json += ",";

    json += "\"moisture\":";
    json += String(packet.readings[i].moisture);
    json += ",";

    json += "\"latitude\":";
    json += String(packet.readings[i].latitude, 6);
    json += ",";

    json += "\"longitude\":";
    json += String(packet.readings[i].longitude, 6);

    json += "}";
  }

  json += "]";
  json += "}";

  return json;
}

bool WlanBackend::publishStoredHubPackets() {
  if (!LittleFS.exists(HUB_PACKET_FILE)) {
    return true;
  }

  File file = LittleFS.open(HUB_PACKET_FILE, FILE_READ);
  if (!file) {
    Serial.println("[WLAN-BACKEND] hubpackets.bin konnte nicht gelesen werden.");
    return false;
  }

  std::vector<HubPacket> packets;

  while (file.available() >= sizeof(HubPacket)) {
    HubPacket packet;
    size_t bytesRead = file.read((uint8_t*)&packet, sizeof(HubPacket));

    if (bytesRead == sizeof(HubPacket)) {
      packets.push_back(packet);
    }
  }

  file.close();

  if (packets.empty()) {
    LittleFS.remove(HUB_PACKET_FILE);
    return true;
  }

  if (!connectWifi()) {
    Serial.println("[WLAN-BACKEND] Kein WLAN, Datei bleibt gespeichert.");
    return false;
  }

  if (!connectMqtt()) {
    Serial.println("[WLAN-BACKEND] Kein MQTT, Datei bleibt gespeichert.");
    return false;
  }

  for (const HubPacket& packet : packets) {
    String payload = hubPacketToJson(packet);

    Serial.println("[MQTT] Sende gespeichertes HubPacket:");
    Serial.println(payload);

    if (!mqttClient.publish(MQTT_TOPIC, payload.c_str())) {
      Serial.println("[MQTT] Senden fehlgeschlagen, Datei bleibt gespeichert.");
      return false;
    }

    mqttClient.loop();
  }

  LittleFS.remove(HUB_PACKET_FILE);
  Serial.println("[WLAN-BACKEND] Gespeicherte HubPackets gesendet und geloescht.");

  return true;
}
