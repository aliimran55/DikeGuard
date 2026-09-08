#pragma once

#include <Arduino.h>
#include "events.h"

// ============================================================
// Wlan-Schnittstelle des Walker Nodes
// Die Klasse wird von der WLAN-Task benutzt, um Sensor-Events über WLAN 
// an einen MQTT-Broker zu senden.
// ============================================================

class WlanBackend {
public:
  WlanBackend();

  void begin();

  void loop();

  bool publishSensorEvent(const Event_t& event);
  bool publishStoredHubPackets();

private:
  bool connectWifi();
  bool connectMqtt();

  String sensorEventToJson(const Event_t& event);
};
