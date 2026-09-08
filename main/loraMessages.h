// ============================================================
//  loraMessages.h  –  Nachrichtenformat
// ============================================================
//
//  Definiert das On-Air-Format des Funks.

#pragma once
#include <Arduino.h>


typedef struct __attribute__((packed)) {
  uint8_t  originNodeId;  // Ursprungsknoten der Messung
  int16_t  temperature;   // °C * 100
  uint16_t humidity;      // %  * 100
  uint16_t battery;       // V  * 100
  uint16_t moisture;      // Bodenfeuchte-Rohwert 0..4095
  uint8_t  vibration;     // Erschütterung seit letzter Messung (0/1)
} LoraDataMsg_t;

typedef struct __attribute__((packed)) {
  uint8_t  originNodeId;  // wessen Nachricht bestätigt wird
  uint32_t ackedCounter;    // welcher P2P-Counter bestaetigt wird
} LoraAckMsg_t;

typedef struct __attribute__((packed)) {
  uint8_t  marker;        // LORA_POS_MARKER
  uint8_t  originNodeId;  // Knoten, dessen Position gemeldet wird
  int32_t  latitudeE6;    // Grad * 1e6
  int32_t  longitudeE6;   // Grad * 1e6
} LoraPosMsg_t;

#define LORA_POS_MARKER  0xFF

// Größe eines Datensatzes
#define DATA_SAMPLE_LEN   ((int)sizeof(LoraDataMsg_t))

static_assert(sizeof(LoraPosMsg_t) == sizeof(LoraDataMsg_t),
              "Die Positions-Message muss dieselbe Größe wie eine Daten-Message haben");

typedef union {
  LoraDataMsg_t data;
  LoraPosMsg_t  pos;
  uint8_t       bytes[sizeof(LoraDataMsg_t)];
} LoraRecord_t;

// Maximale Datensätze in einem DATA-Paket
#define DATA_MAX_SAMPLES  5

#define LORA_P2P_COUNTER_LEN  ((int)sizeof(uint32_t))
#define LORA_NONCE_LEN  (1 + LORA_P2P_COUNTER_LEN)
#define LORA_TAG_LEN    4

#define LORA_ACK_PACKET_LEN  (LORA_NONCE_LEN + LORA_TAG_LEN + (int)sizeof(LoraAckMsg_t))

// DATA-Relay-Paket: [ Header | Tag | bis zu DATA_MAX_SAMPLES Datensätze ]
#define LORA_DATA_PLAINTEXT_MAX (DATA_MAX_SAMPLES * DATA_SAMPLE_LEN)
#define LORA_DATA_PACKET_LEN    (LORA_NONCE_LEN + LORA_TAG_LEN + LORA_DATA_PLAINTEXT_MAX)

#define LORA_MAX_PACKET_LEN  LORA_DATA_PACKET_LEN

typedef enum {
  P2P_PKT_INVALID = -1,
  P2P_PKT_DATA,
  P2P_PKT_ACK,
} P2PPacketKind;

// Geparstes empfangenes P2P-Paket
typedef struct {
  uint8_t senderNodeId;  
  uint32_t counter;                                     // P2P-Replay-Schutz                                // ACK-Ziel
  uint8_t sampleCount;                                   // Anzahl Datensätze (nur DATA)
  uint8_t samples[DATA_MAX_SAMPLES * DATA_SAMPLE_LEN];   // Datensatz-Bytes
} P2PReceived_t;
