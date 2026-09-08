// ============================================================
//  loraConfig.h  –  Zentrale Konfiguration für Funk + TTN + Verschlüsselung
// ============================================================
//
//  Hier liegen alle Board-, Funk-, TTN- und Verschlüsselungs-Einstellungen.
//
//  ‼  PRO GERÄT anzupassen:  NODE_ID, TTN Keys, Board Version
// ============================================================

#pragma once
#include <Arduino.h>

// Pinbelegung
#define LORA_NSS     8     // Chip Select
#define LORA_DIO1    14    // Interrupt (TX/RX done)
#define LORA_RST     12    // Reset
#define LORA_BUSY    13    // Busy
#define LORA_SCK     9     // SPI Clock
#define LORA_MISO    11    // SPI Data In
#define LORA_MOSI    10    // SPI Data Out

// Version des Boards: 42 = V4.2, 43 = V4.3
#define HELTEC_V4_REV  43

#define LORA_PA_POWER  7
#define LORA_PA_EN 2

#if HELTEC_V4_REV == 42
  #define LORA_PA_MODE 46
#else
  #define LORA_PA_MODE 5
#endif

// Knoten-Identität
// 
// Eindeutige ID dieses Geräts. Steht in jeder Payload, damit das
// TTN-Dashboard die Daten dem richtigen Knoten zuordnen kann, 
// auch wenn sie über einen Nachbarknoten weitergeleitet wurden.
// Pro GERÄT eindeutig setzen!  (1..254)
#define NODE_ID  1

// Testmodus ohne TTN-Gateway
//
// Wenn 1: KEIN echter LoRaWAN-Join/Uplink. Der TTN-Uplink wird stattdessen
// nur simuliert (Serial-Log). Damit lässt sich der komplette Relay-Weg
// (Senden -> Relay-Nachbar -> Uplink) auch ohne Gateway testen.
// Für den Echtbetrieb auf 0 setzen.
#define P2P_TEST_MODE  0

// Nur im Testmodus: Erreicht dieses Board das (simulierte) Backend?
// 1 = Uplink "funktioniert" -> Daten werden weitergegeben.
// 0 = Uplink "fällt aus" -> eigene Daten gehen über den Relay-Weg, fremde Daten
//     werden nicht weitergeleitet. So können wir pro Board einen Ausfall simulieren.
#define SIM_UPLINK_OK  0

// Ob Testdaten eingespeißt werden
#define LORA_TEST_INJECT 0

#define LORAWAN_REGION   EU868
#define LORAWAN_SUBBAND  0

// JoinEUI/AppEUI
static const uint64_t LORAWAN_JOIN_EUI = 0x0000000000000000ULL;

// DevEUI
static const uint64_t LORAWAN_DEV_EUI  = 0x70B3D57ED007871DULL;

// AppKey
static const uint8_t LORAWAN_APP_KEY[16] = {
  0x23, 0x9C, 0x40, 0xFD, 0x54, 0x50, 0xDD, 0x45, 0x9E, 0x53, 0xF2, 0x47, 0x5E, 0x7A, 0x92, 0x5A
};

// TTN-Sendeverhalten
#define TTN_FPORT               1
#define TTN_CONFIRM_RETRIES     1       // bestätigte Uplink-Versuche, bevor auf Relay-Weg gewechselt wird
#define TTN_JOIN_RETRY_MS       60000   // Zeit, nach der der nächste TTN Join versucht wird
#define TTN_START_DATARATE      3       // 0 = SF12, ..., 3 = SF9, ..., 5 = SF7

// Jeder N-te Uplink wird confirmed gesendet (also ein ACK angefordert).
#define TTN_CONFIRM_EVERY_N     15

// TTN Downlink-Empfangsfenster (RX1/RX2) in ms. 
#define TTN_RX_SCAN_GUARD_MS    50

// Nach wie vielen bestätigten Uplinks in Folge ohne ACK die gespeicherte TTN-Session verworfen und neu gejoint wird.
#define TTN_SESSION_RESET_AFTER 5

// P2P-Funk Sendeverhalten
#define LORA_FREQUENCY        869.525   // MHz
#define LORA_BANDWIDTH        125.0     // kHz
#define LORA_SPREADING_FACTOR 12        // 7...12
#define LORA_CODING_RATE      5         // 5...8
#define LORA_SYNC_WORD        0x12
#define LORA_TX_POWER         22        // dBm
#define LORA_PREAMBLE_LEN     8
#define LORA_TCXO_VOLTAGE     1.8

#define RELAY_RETRIES        2       // P2P-Sendeversuche an die Nachbarn, bevor verworfen wird
#define RELAY_ACK_TIMEOUT_MS 5000    // Wartezeit auf Relay-ACK je Versuch

// Größe des Sendepuffers
#define OUTBOX_CAPACITY   16

// Wie lange auf weitere Datensätze gewartet wird, bevor ein noch nicht volles DATA-Paket trotzdem rausgeht
#define OUTBOX_LINGER_MS  5000UL

// Mindestabstand zwischen zwei Uplinks
#define LORA_MIN_UPLINK_INTERVAL_MS  60000UL

// Verschlüsselungs-Key: Muss auf allen Geräten gleich sein!
static const uint8_t LORA_AES_KEY[16] = {
  0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
  0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF
};

