// some defines for the BLE communication, like the data types and packet structs

#pragma once

#define HUB_SCAN_TIME 200 // scantime in miliseconds
#define HUB_SEND_TIME 600 // sendtime in miliseconds

#define RUNNER_SEND_TIME 500  // sendtime in miliseconds
#define RUNNER_SCAN_TIME 1000 // scantime in miliseconds

#define RUNNER_ID 1
#define HUB_ID 1

#define MESSAGE_DELAY 500

// AES CCM Tag and IV Length (the tag is the integrity check, IV is the random number)
#define CCM_TAG_LEN 8 // must be even (4, 6, 8, 10, 12, 14, or 16)
#define CCM_IV_LEN 12 // must be 7-13

// define the sensor readings (we should receive sensor readings like that, more efficient than array)
typedef struct __attribute__((packed)) {
  // uint8_t originNodeId; // original id of messuering node;
  int16_t  temperature; // °C * 100
  uint16_t humidity;    // %  * 100
  uint16_t battery;     // V  * 100
  uint16_t moisture;    // soil moisture raw data 0..4095
  float    latitude;    // degrees
  float    longitude;   // degrees
} SensorReading;

#define MAX_READINGS 10 // defines how many readings we can hold

// define the hub packet (we should bundle the sensor readings in hub packets, more efficient than array)
typedef struct __attribute__((packed)) {
  uint8_t       hub_id;
  uint32_t      dl_counter;
  uint32_t      timestamp;
  SensorReading readings[MAX_READINGS]; // would fit up to 20 sensors at current layout
} HubPacket;

typedef struct WakeUpPacket {
  uint8_t  dl_ID;
  uint32_t counter;
} WakeUpPacket;
