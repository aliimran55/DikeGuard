// ============================================================
//  events.h  –  Einheitliche Event-Struktur für den ganzen Sketch
// ============================================================
//
//  Jedes Event hat:
//    type     → welcher Event-Typ (enum EventType_e)
//    payload  → variabler Body via Union – nur das relevante Feld nutzen
//
//  Neue Events hinzufügen:
//    1. EventType_e   → neuen Eintrag ergänzen
//    2. Union payload → neue Payload-Struct hinzufügen
//    3. Im Dispatcher (main.ino) → neuen case behandeln
// ============================================================

#pragma once
#include <Arduino.h>

// Definierte Typen, welche als Identifikation für die verschiedenen Gerätetypen genutzt werden
#define DEVICE_NODE 0    // aka Sensor Node
#define DEVICE_HUB  1   // aka Transmission Node
#define DEVICE_DIKERUNNER 2  //aka Walker Node aka Deichläufer


#define DEVICE_MODE DEVICE_NODE


#define MAX_NODES 10

// verfügbare Display-Screens (hier statt in displayTask.ino, damit die
// Arduino-Auto-Prototypen für Funktionen mit MenuItem_t*-Parametern
// den Typ schon kennen -- siehe MenuItem_t weiter unten)
enum Screen_t {
  SCREEN_HOME = 0,
  SCREEN_MENU,
  SCREEN_SENSORS,
  SCREEN_ERRORS,
  SCREEN_LORA,
  SCREEN_MESSAGES,

  SCREEN_DEVICE_INFO,
  SCREEN_DEVICE_NODE_ID,
  SCREEN_DEVICE_MAC,
  SCREEN_DEVICE_GPS,
  SCREEN_DEVICE_TIMESTAMP,

  SCREEN_NUM_MSG,
  SCREEN_LAST_MESSAGE,

  SCREEN_PAIRING_CHECK,
  SCREEN_PAIRING_NODE_FOUND,
  SCREEN_PAIRING_SUCCESS,
  SCREEN_COUNT,  // Hilfskonstante
  SCREEN_PAIRING_FAILED
};

// Ein Eintrag in einem Display-Menu. Mit ziel Screen
typedef struct {
  const char* label;
  Screen_t    targetScreen;
} MenuItem_t;


// hier werden alle Event typen definiert. Gerade gibt es nur diese 6 (andere können hinzugefügt werden)
typedef enum : uint8_t {
  EVENT_SENSOR_DATA = 0,
  EVENT_ERROR       = 1,
  EVENT_LORA_DATA   = 2,
  EVENT_BUTTON_PRESS = 3,  // Neuer Event-Typ für den GPIO0-Button
  EVENT_NETWORKPACKET = 4,
  EVENT_IMPORTANT_DATA = 5,
  EVENT_NODEPACKET = 6,
  EVENT_BLE_PACKET_RECEIVED = 7, // wenn Runner ein packet von Hub erhält und speichert
    // WiFi / ESP-NOW integration
  EVENT_WIFI_COMMAND = 8,
  EVENT_WIFI_STATUS  = 9,
} EventType_e;

// Sensordaten
// temperature/humidity bleiben vorne, damit loraTask sie unverändert nutzen kann.
typedef struct {
  float    temperature;   // °C   (BME280)
  float    humidity;      // %rH  (BME280)
  float    pressure;      // hPa  (BME280)
  float    light;         // lx   (BH1750)
  int16_t  moistureRaw;   // Rohwert (0..4095)
  bool     wet;           // digitaler Moisture-Schwellwert
  bool     vibration;     // Erschütterung seit letzter Messung
  float    batVolts;      // V  (Battery)
  int      seconds;       // s
  int      minutes;       // min
  int      hours;         // h (in UTC)
  double   latitude;      // Grad
  double   longitude;     // Grad
} SensorPayload_t;

// Fehlerdaten
typedef struct {
  uint8_t  errorCode;
  char     message[24];
} ErrorPayload_t;

typedef struct {
  char     message[128];
} HubPacketIds;

// Das ist unsere einheitliche Eventstruktur. Muss entsprechend bei neuen Events verändert werden

// Welche Art von Button-Druck wurde erkannt?
typedef enum : uint8_t {
  BUTTON_SINGLE_PRESS = 0,
  BUTTON_DOUBLE_PRESS = 1,
  BUTTON_LONG_PRESS   = 2,
} ButtonPressType_e;

// Payload für Button-Events
typedef struct {
  ButtonPressType_e pressType;     // Single, Double oder Long Press
  uint8_t gpio;                    // Welcher GPIO den Event ausgelöst hat
  uint32_t pressDurationMs;        // Dauer des Tastendrucks in Millisekunden
} ButtonPayload_t;

typedef struct {
  uint8_t nodeId;
  SensorPayload_t sensor;
} NodePacket;

typedef struct {
  uint8_t count;
  NodePacket nodes[MAX_NODES];
} NetworkPacket;

typedef enum : uint8_t {
  LORA_TRAFFIC_TTN_TX = 0,
  LORA_TRAFFIC_TTN_ACK,
  LORA_TRAFFIC_RELREQ_TX,
  LORA_TRAFFIC_RELREQ_RX,
  LORA_TRAFFIC_RELREQ_ACK_TX,
  LORA_TRAFFIC_RELREQ_ACK_RX,
  LORA_TRAFFIC_TTN_REL_TX,
} LoraTrafficKind_e;

typedef struct {
  LoraTrafficKind_e kind;
  uint8_t nodeId;
} LoraTrafficPayload_t;



// Wifi ESP Now
typedef enum : uint8_t {
  WIFI_COMMAND_SET_DATA_MODE = 0,
  WIFI_COMMAND_SET_PAIRING_MODE,
  WIFI_COMMAND_TOGGLE_PAIRING_MODE
} WifiCommandKind_e;

typedef struct {
  WifiCommandKind_e command;
} WifiCommandPayload_t;

typedef enum : uint8_t {
  WIFI_STATUS_DATA_MODE = 0,
  WIFI_STATUS_PAIRING_MODE,
  WIFI_STATUS_NODE_FOUND,
  WIFI_STATUS_PAIRING_SUCCESS,
  WIFI_STATUS_DATA_RECEIVED,
  WIFI_STATUS_DATA_ACK_SENT,
  WIFI_STATUS_ERROR
} WifiStatusKind_e;

typedef struct {
  WifiStatusKind_e status;
  uint8_t nodeId;
  uint8_t totalNodes;
  char mac[18];
  char message[32];
} WifiStatusPayload_t;



// Einheitliches Event
typedef struct {
  EventType_e type;
  uint32_t    timestamp;
  union {
    SensorPayload_t sensor;
    ErrorPayload_t  error;
    ButtonPayload_t button; //Button-Event-Daten
    NetworkPacket packet;
    NodePacket nodepacket;
    LoraTrafficPayload_t loraTraffic;
    HubPacketIds hubPacketIds;
    //WIFI
    WifiCommandPayload_t wifiCommand;
    WifiStatusPayload_t  wifiStatus;
  } payload;
} Event_t;

typedef struct {
  uint32_t message;
  //node needs to propagate nodeid to hub
  uint8_t nodeId;
  SensorPayload_t sensor;
} SensorMessage;

// Globale Queues
extern QueueHandle_t mainEventQueue;
extern QueueHandle_t displayInbox;
extern QueueHandle_t loraInbox;
extern QueueHandle_t wlanInbox;
extern QueueHandle_t bleInbox;
extern QueueHandle_t applikationInbox;

// Tasks
void sensorTask(void* parameter);
void buttonTask(void* parameter);
void displayTask(void* parameter);
void loraTask(void* parameter);
void wlanTask(void* parameter);
void bleTask(void* parameter);
void applikation(void* parameter);
void loraInjectTask(void* parameter);