// loraTask, sendet Daten aus der lorainbox über LoRa
//
// Normalfall: Die Sensordaten gehen direkt an TTN.
//
// Relay-Weg: Erreicht ein Knoten TTN nicht (OTAA-Join schlägt fehl oder die Nachricht wird nicht bestätigt),
// broadcasted er die Daten verschlüsselt an seine Nachbarn. Ein Nachbar, der TTN erreicht,
// leitet die Daten an TTN weiter und bestätigt die Weiterleitung. Die weitergeleitete Nachricht enthält
// die IDs des Ursprungs- und Relay-Knotens, damit die Zuordnung im Dashboard später weiterhin funktioniert.

#include "events.h"
#include "loraConfig.h"
#include "loraMessages.h"

#include <RadioLib.h>
#include <SPI.h>
#include "mbedtls/gcm.h"
#include <Preferences.h>

static P2PPacketKind classifyP2PPacket(P2PReceived_t& received);

static LoraRecord_t serializeSample(uint8_t originNodeId, const SensorPayload_t& sensor);
static LoraRecord_t serializePos(uint8_t originNodeId, const SensorPayload_t& sensor);

static void outboxNoteDropped(const LoraRecord_t& rec);
static void outboxEnqueue(const LoraRecord_t& rec);
static void outboxPromoteToFront(const LoraRecord_t& rec);

// Funk-Objekte
class SX1262Heltec : public SX1262 {
public:
  SX1262Heltec(Module* mod) : SX1262(mod) {}
  void applyRxFix() {
    uint8_t v = 0;
    readRegister(0x08B5, &v, 1);
    v |= 0x01;
    writeRegister(0x08B5, &v, 1);
    setRxBoostedGainMode(true);
  }
};

static SX1262Heltec radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
static const LoRaWANBand_t LoRaWANRegion = LORAWAN_REGION;          // EU868
static LoRaWANNode node(&radio, &LoRaWANRegion, LORAWAN_SUBBAND);

static const uint32_t loraRfSwitchPins[] = {
  LORA_PA_MODE, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
};
static const Module::RfSwitchMode_t loraRfSwitchTable[] = {
  { Module::MODE_IDLE, { LOW,  LOW, LOW, LOW, LOW } },
  { Module::MODE_RX,   { LOW,  LOW, LOW, LOW, LOW } },
  { Module::MODE_TX,   { HIGH, LOW, LOW, LOW, LOW } },
  END_OF_MODE_TABLE,
};

// Zustand
static volatile bool loraPacketReceived = false;   // vom RX-Interrupt gesetzt
static bool          ttnJoined          = false;   // besteht OTAA-Session (darf senden)
static bool          ttnReachable       = false;   // antwortet TTN-Backend
static unsigned long lastJoinAttempt    = 0;
static Preferences   loraStore;


// P2P-Replay-Schutz
static uint32_t p2pTxCounter = 0;
static bool     p2pTxCounterLoaded = false;

#define P2P_COUNTER_REPLAY     0
#define P2P_COUNTER_DUPLICATE  1
#define P2P_COUNTER_FRESH      2

static void makeP2PRxKey(uint8_t senderNodeId, char* key, size_t keyLen) {
  snprintf(key, keyLen, "rx%u", senderNodeId);
}

static void loadP2PTxCounter() {
  if (p2pTxCounterLoaded) return;

  loraStore.begin("lorap2p", false);
  p2pTxCounter = loraStore.getUInt("txCtr", 0);
  loraStore.end();

  p2pTxCounterLoaded = true;
  Serial.printf("[LORA] P2P-TX-Counter geladen: %lu\n", (unsigned long)p2pTxCounter);
}

static uint32_t nextP2PCounter() {
  loadP2PTxCounter();

  if (p2pTxCounter == UINT32_MAX) {
    Serial.println("[LORA] P2P-TX-Counter am Maximum, Senden abgebrochen.");
    return 0;
  }

  p2pTxCounter++;

  loraStore.begin("lorap2p", false);
  size_t written = loraStore.putUInt("txCtr", p2pTxCounter);
  loraStore.end();

  if (written == 0) {
    Serial.println("[LORA] P2P-TX-Counter konnte nicht gespeichert werden.");
    return 0;
  }

  return p2pTxCounter;
}

static uint8_t getP2PCounterState(uint8_t senderNodeId, uint32_t counter) {
  if (counter == 0) return P2P_COUNTER_REPLAY;

  char key[8];
  makeP2PRxKey(senderNodeId, key, sizeof(key));

  loraStore.begin("lorap2p", false);
  uint32_t lastCounter = loraStore.getUInt(key, 0);
  loraStore.end();

  if (counter > lastCounter) return P2P_COUNTER_FRESH;
  if (counter == lastCounter) return P2P_COUNTER_DUPLICATE;
  return P2P_COUNTER_REPLAY;
}

static void rememberP2PCounter(uint8_t senderNodeId, uint32_t counter) {
  char key[8];
  makeP2PRxKey(senderNodeId, key, sizeof(key));

  loraStore.begin("lorap2p", false);
  loraStore.putUInt(key, counter);
  loraStore.end();
}


// Versendequeue von Daten- und Positions-Messages
static LoraRecord_t  outbox[OUTBOX_CAPACITY];
static uint8_t       outboxCount      = 0;
static uint32_t      outboxOldestMs   = 0;
static uint32_t      lastUplinkMs     = 0;

// Merkt sich anhand der Node_ID, ob die Positionsdaten des Knotens schon verschickt wurden.
static uint8_t posSentBitmap[256 / 8];

static bool posAlreadySent(uint8_t nodeId) {
  return (posSentBitmap[nodeId / 8] >> (nodeId % 8)) & 1;
}


static void markPosSent(uint8_t nodeId, bool sent) {
  if (sent) posSentBitmap[nodeId / 8] |=  (1 << (nodeId % 8));
  else      posSentBitmap[nodeId / 8] &= ~(1 << (nodeId % 8));
}


IRAM_ATTR void onLoraReceive() {
  loraPacketReceived = true;
}


// Meldet LoRa-Ereignisse für das Display. 
static void notifyLoraTraffic(LoraTrafficKind_e kind, uint8_t nodeId) {
  Event_t ev;
  ev.type      = EVENT_LORA_DATA;
  ev.timestamp = millis();
  ev.payload.loraTraffic.kind   = kind;
  ev.payload.loraTraffic.nodeId = nodeId;
  xQueueSend(mainEventQueue, &ev, 0);   // nicht blockieren, Anzeige ist unkritisch
}


// Verschlüsselt Klartext. 
static void encryptBytes(uint32_t counter, const uint8_t* plain, size_t plainLen, uint8_t* out, size_t* outLen) {
  uint8_t* nonce      = out;
  uint8_t* tag        = out + LORA_NONCE_LEN;
  uint8_t* ciphertext = out + LORA_NONCE_LEN + LORA_TAG_LEN;

  nonce[0] = (uint8_t)NODE_ID;
  memcpy(&nonce[1], &counter, LORA_P2P_COUNTER_LEN);

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, LORA_AES_KEY, 128);
  mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, plainLen,
                            nonce, LORA_NONCE_LEN, nullptr, 0,
                            plain, ciphertext, LORA_TAG_LEN, tag);
  mbedtls_gcm_free(&ctx);

  *outLen = LORA_NONCE_LEN + LORA_TAG_LEN + plainLen;
}


// Entschlüsselt ein Paket und prüft dessen Echtheit.
// false = Länge passt nicht oder Tag ungültig.
static bool decryptBytes(const uint8_t* packet, size_t packetLen, uint8_t* plainOut, size_t plainLen) {
  if (packetLen != (size_t)(LORA_NONCE_LEN + LORA_TAG_LEN + plainLen)) return false;
  const uint8_t* nonce      = packet;
  const uint8_t* tag        = packet + LORA_NONCE_LEN;
  const uint8_t* ciphertext = packet + LORA_NONCE_LEN + LORA_TAG_LEN;

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);
  int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, LORA_AES_KEY, 128);
  if (rc == 0) {
    rc = mbedtls_gcm_auth_decrypt(&ctx, plainLen,
                                  nonce, LORA_NONCE_LEN, nullptr, 0,
                                  tag, LORA_TAG_LEN,
                                  ciphertext, plainOut);
  }
  mbedtls_gcm_free(&ctx);
  return rc == 0;
}


// Klassifiziert ein empfangenes Paket anhand der Länge als Relay-Request oder ACK.
static P2PPacketKind classifyP2PPacket(P2PReceived_t& received) {
  uint8_t rawPacket[LORA_MAX_PACKET_LEN];
  size_t packetLen = radio.getPacketLength();
  int state = radio.readData(rawPacket, packetLen > sizeof(rawPacket) ? sizeof(rawPacket) : packetLen);

  if (state != RADIOLIB_ERR_NONE) return P2P_PKT_INVALID;

  // ACK
  if (packetLen == (size_t)LORA_ACK_PACKET_LEN) {
    LoraAckMsg_t ack;
    if (decryptBytes(rawPacket, packetLen, (uint8_t*)&ack, sizeof(LoraAckMsg_t))) {
      received.senderNodeId = ack.originNodeId;
      received.counter      = ack.ackedCounter;
      return P2P_PKT_ACK;
    }
    return P2P_PKT_INVALID;
  }

  // Relay Request
  size_t plaintextLen = (packetLen > (size_t)(LORA_NONCE_LEN + LORA_TAG_LEN))
                      ? packetLen - LORA_NONCE_LEN - LORA_TAG_LEN : 0;
  if (plaintextLen >= (size_t)DATA_SAMPLE_LEN && (plaintextLen % DATA_SAMPLE_LEN) == 0) {
    uint8_t sampleCount = (uint8_t)(plaintextLen / DATA_SAMPLE_LEN);
    if (sampleCount >= 1 && sampleCount <= DATA_MAX_SAMPLES) {
      uint8_t plaintext[LORA_DATA_PLAINTEXT_MAX];
      if (decryptBytes(rawPacket, packetLen, plaintext, plaintextLen)) {
        received.senderNodeId = rawPacket[0];
        memcpy(&received.counter, &rawPacket[1], sizeof(received.counter));
        received.sampleCount  = sampleCount;
        memcpy(received.samples, plaintext, plaintextLen);
        return P2P_PKT_DATA;
      }
    }
  }
  return P2P_PKT_INVALID;   // fremdes/kaputtes/manipuliertes Paket
}


// Broadcasted den übergebenen Klartext verschlüsselt.
static void p2pTransmit(uint32_t counter, const void* plaintext, size_t plaintextLen) {
  uint8_t packet[LORA_MAX_PACKET_LEN];
  size_t  packetLen;
  encryptBytes(counter, (const uint8_t*)plaintext, plaintextLen, packet, &packetLen);
  radio.transmit(packet, packetLen);
  loraPacketReceived = false;
}


// Sendet eine Empfangsbestätigung (ACK) an den Ursprungsknoten.
static void p2pSendAck(uint8_t originNodeId, uint32_t ackedCounter) {
  uint32_t counter = nextP2PCounter();
  if (counter == 0) return;

  LoraAckMsg_t ack;
  ack.originNodeId = originNodeId;
  ack.ackedCounter = ackedCounter;
  p2pTransmit(counter, &ack, sizeof(ack));
}


// Bringt das Radio in den P2P-Modus.
static void enterP2PMode() {
  int state = radio.begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                          LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER,
                          LORA_PREAMBLE_LEN, LORA_TCXO_VOLTAGE);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LORA] P2P-Mode begin-Fehler, code=%d\n", state);
  }
  radio.setDio2AsRfSwitch(true);
  radio.applyRxFix();
  radio.setPacketReceivedAction(onLoraReceive);
  loraPacketReceived = false;            // evtl. durch LoRaWAN-Rx gesetztes Flag verwerfen
  radio.startReceive();
}


// Wartet auf ein ACK für die eigene NODE_ID.
static bool waitForAck(uint8_t expectedNodeId, uint32_t expectedCounter, uint32_t timeoutMs) {
  radio.startReceive();
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (loraPacketReceived) {
      loraPacketReceived = false;
      P2PReceived_t received;
      if (classifyP2PPacket(received) == P2P_PKT_ACK && received.senderNodeId == expectedNodeId && received.counter == expectedCounter) {
        return true;
      }
      radio.startReceive(); // re-arm, weiter warten
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}


// Versucht den OTAA-Join bei TTN. Falls erfolgreich, wird ttnJoined gesetzt.
static bool ttnJoin() {
  Serial.println("[LORA] TTN OTAA-Join start.");
  int state = node.beginOTAA(LORAWAN_JOIN_EUI, LORAWAN_DEV_EUI, nullptr, LORAWAN_APP_KEY);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LORA] beginOTAA-Fehler, code=%d\n", state);
    return false;
  }

  node.scanGuard = TTN_RX_SCAN_GUARD_MS;

  // TTN-Nonce wiederherstellen.
  loraStore.begin("lorawan", false);
  if (loraStore.getBytesLength("nonces") == RADIOLIB_LORAWAN_NONCES_BUF_SIZE) {
    uint8_t noncesBuf[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
    loraStore.getBytes("nonces", noncesBuf, sizeof(noncesBuf));
    if (node.setBufferNonces(noncesBuf) == RADIOLIB_ERR_NONE) {
      Serial.println("[LORA] TTN-Nonce aus Flash wiederhergestellt.");
    }
  }

  // TTN-Session wiederherstellen. Gelingt das, setzt activateOTAA() die alte Session ohne neuen Join-Funkverkehr fort.
  if (loraStore.getBytesLength("session") == RADIOLIB_LORAWAN_SESSION_BUF_SIZE) {
    uint8_t sessionBuf[RADIOLIB_LORAWAN_SESSION_BUF_SIZE];
    loraStore.getBytes("session", sessionBuf, sizeof(sessionBuf));
    if (node.setBufferSession(sessionBuf) == RADIOLIB_ERR_NONE) {
      Serial.println("[LORA] TTN-Session aus Flash wiederhergestellt.");
    }
  }

  state = node.activateOTAA();

  // Erhöhte TTN-Nonce sichern.
  loraStore.putBytes("nonces", node.getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);

  bool active = (state == RADIOLIB_LORAWAN_NEW_SESSION || state == RADIOLIB_LORAWAN_SESSION_RESTORED);
  if (active) {
    loraStore.putBytes("session", node.getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
  }
  loraStore.end();

  if (active) {
    ttnJoined    = true;
    ttnReachable = true;
    radio.applyRxFix();
    node.setADR(false);
    node.setDatarate(TTN_START_DATARATE);
    Serial.println(state == RADIOLIB_LORAWAN_SESSION_RESTORED
                     ? "[LORA] TTN-Session fortgesetzt, kein neuer Join nötig."
                     : "[LORA] TTN-Join erfolgreich.");
    return true;
  }
  Serial.printf("[LORA] TTN-Join fehlgeschlagen, code=%d\n", state);
  return false;
}


// Sichert den TTN-Sessionzustand in den Flash.
static void ttnSaveSession() {
  loraStore.begin("lorawan", false);
  loraStore.putBytes("session", node.getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);
  loraStore.end();
}


// Verwirft die gespeicherte TTN-Session und erzwingt einen neuen OTAA-Join.
static void ttnDiscardSession() {
  Serial.println("[LORA] Verwerfe TTN-Session, erzwinge neuen Join.");
  loraStore.begin("lorawan", false);
  loraStore.remove("session");
  loraStore.end();
  ttnJoined = false;
}


// Stellt sicher, ob eine TTN-Session besteht.
static bool ttnEnsureJoined() {
#if P2P_TEST_MODE
  return SIM_UPLINK_OK;   // Testmodus: Backend erreichbar = dieses Board darf direkt publishen
#else
  if (ttnJoined) return true;
  static bool firstAttempt = true;
  unsigned long now = millis();
  if (!firstAttempt && now - lastJoinAttempt < TTN_JOIN_RETRY_MS) return false;
  firstAttempt    = false;
  lastJoinAttempt = now;
  return ttnJoin();
#endif
}


// Wandelt eine Sensor-Messung in das On-Air Format um.
static LoraRecord_t serializeSample(uint8_t originNodeId, const SensorPayload_t& sensor) {
  LoraRecord_t rec;
  rec.data.originNodeId = originNodeId;
  rec.data.temperature  = (int16_t) lroundf(sensor.temperature * 100.0f);
  rec.data.humidity     = (uint16_t)lroundf(sensor.humidity    * 100.0f);
  rec.data.battery      = (uint16_t)lroundf(sensor.batVolts    * 100.0f);
  rec.data.moisture     = (uint16_t)sensor.moistureRaw;
  rec.data.vibration    = sensor.vibration ? 1 : 0;
  return rec;
}


// Wandelt eine Positions-Meldung in das On-Air Format um.
static LoraRecord_t serializePos(uint8_t originNodeId, const SensorPayload_t& sensor) {
  LoraRecord_t rec;
  rec.pos.marker       = LORA_POS_MARKER;
  rec.pos.originNodeId = originNodeId;
  rec.pos.latitudeE6   = (int32_t)llround(sensor.latitude  * 1e6);
  rec.pos.longitudeE6  = (int32_t)llround(sensor.longitude * 1e6);
  return rec;
}


static void outboxNoteDropped(const LoraRecord_t& rec) {
  if (rec.pos.marker == LORA_POS_MARKER) markPosSent(rec.pos.originNodeId, false);
}


// Checkt, ob für den Knoten des Pakets die Position geschickt werden muss.
static bool needPosRecord(const NodePacket& np) {
  if (posAlreadySent(np.nodeId)) return false;
  return np.sensor.latitude != 0.0 || np.sensor.longitude != 0.0;
}


// Packt einen Datensatz an das Ende der Queue. Ist sie voll, wird der 1. und somit älteste Datensatz verworfen.
static void outboxEnqueue(const LoraRecord_t& rec) {
  if (outboxCount >= OUTBOX_CAPACITY) {
    outboxNoteDropped(outbox[0]);
    memmove(&outbox[0], &outbox[1], (OUTBOX_CAPACITY - 1) * sizeof(LoraRecord_t));
    outboxCount = OUTBOX_CAPACITY - 1;
    Serial.println("[LORA] Queue voll, vordersten Datensatz verworfen.");
  }
  if (outboxCount == 0) outboxOldestMs = millis();
  outbox[outboxCount++] = rec;
}


// Holt einen bereits gepufferten Datensatz an den Anfang der Queue.
static void outboxPromoteToFront(const LoraRecord_t& rec) {
  for (int i = outboxCount - 1; i >= 0; i--) {
    if (memcmp(outbox[i].bytes, rec.bytes, sizeof(rec.bytes)) == 0) {
      LoraRecord_t tmp = outbox[i];
      memmove(&outbox[1], &outbox[0], (size_t)i * sizeof(LoraRecord_t));
      outbox[0] = tmp;
      return;
    }
  }
}


static bool outboxShouldFlush() {
  if (outboxCount == 0) return false;
  if (outboxCount >= DATA_MAX_SAMPLES) return true;
  if (millis() - outboxOldestMs >= OUTBOX_LINGER_MS) return true;
  return false;
}


// Versucht, die Daten an TTN zu schicken.
// confirm=true : bestätigter Uplink, Rückgabe true = Downlink-ACK erhalten
// confirm=false: unbestätigter Uplink, Rückgabe true = ausgesendet
static bool ttnSendData(uint8_t* samples, uint8_t sampleCount, bool confirm) {
  size_t payloadLen = (size_t)sampleCount * DATA_SAMPLE_LEN;
#if P2P_TEST_MODE
  if (!SIM_UPLINK_OK) {
    Serial.printf("[LORA] (TEST) TTN Ausfall simuliert: %u Datensätze\n", sampleCount);
    return false;
  }
  Serial.printf("[LORA] (TEST) TTN Upload simuliert: %u Datensätze\n", sampleCount);
  return true;
#else
  if (!confirm) {
    int state = node.sendReceive(samples, payloadLen, TTN_FPORT, false);
    ttnSaveSession();
    if (state >= RADIOLIB_ERR_NONE) {
      Serial.printf("[LORA] TTN Upload (unconfirmed): %u Datensätze.\n", sampleCount);
      return true;
    }
    Serial.printf("[LORA] TTN Upload-Fehler (unconfirmed), code=%d\n", state);
    return false;
  }

  bool acked = false;
  for (int attempt = 0; attempt < TTN_CONFIRM_RETRIES && !acked; attempt++) {
    LoRaWANEvent_t downEvent = {};
    int state = node.sendReceive(samples, payloadLen, TTN_FPORT, true, nullptr, &downEvent);
    if (state > 0 && downEvent.confirming) {
      acked = true;
      Serial.printf("[LORA] TTN Upload bestätigt: %u Datensätze (Versuch %d).\n",
                    sampleCount, attempt + 1);
    }
  }
  ttnSaveSession();
  // Fehlerfall wird vom Relay-Weg gemeldet (siehe sendData), daher hier kein Misserfolg-Print.
  return acked;
#endif
}


// Verschickt Sensordaten.
// Versucht erst, selbst an TTN zu schicken und wechselt im Fehlerfall auf den Relay-Weg.
static bool sendData(uint8_t* samples, uint8_t sampleCount) {
  // Primärweg: direkt an TTN
  if (ttnEnsureJoined()) {
    static uint32_t sinceProbe = 0;
    static uint8_t  confirmFailStreak = 0;
    bool confirm;
    if (!ttnReachable) {
      confirm = true;
    } else {
      confirm = (++sinceProbe >= TTN_CONFIRM_EVERY_N);
    }

    notifyLoraTraffic(LORA_TRAFFIC_TTN_TX, 0);
    bool ok = ttnSendData(samples, sampleCount, confirm);

    if (confirm) {
      sinceProbe   = 0;
      ttnReachable = ok;
      if (ok) {
        confirmFailStreak = 0;
      } else if (++confirmFailStreak >= TTN_SESSION_RESET_AFTER) {
        confirmFailStreak = 0;
        ttnDiscardSession();
      }
    }

    if (ok) {
      notifyLoraTraffic(LORA_TRAFFIC_TTN_ACK, 0);
      enterP2PMode();
      return true;
    }
  }

  // Relay-Weg: an Nachbarn senden, auf Relay-ACK warten
  Serial.println("[LORA] TTN nicht erreichbar, sende Relay-Request");
  enterP2PMode();

  uint32_t p2pCounter = nextP2PCounter();
  if (p2pCounter == 0) {
    enterP2PMode();
    return false;
  }

  size_t plaintextLen = (size_t)sampleCount * DATA_SAMPLE_LEN;

  for (int attempt = 0; attempt < RELAY_RETRIES; attempt++) {
    notifyLoraTraffic(LORA_TRAFFIC_RELREQ_TX, 0);
    p2pTransmit(p2pCounter, samples, plaintextLen);
    if (waitForAck(NODE_ID, p2pCounter, RELAY_ACK_TIMEOUT_MS)) {
      notifyLoraTraffic(LORA_TRAFFIC_RELREQ_ACK_RX, 0);
      Serial.printf("[LORA] Relay-ACK erhalten (Versuch %d).\n", attempt + 1);
      enterP2PMode();
      return true;
    }
    Serial.printf("[LORA] Relay-Versuch %d: kein ACK.\n", attempt + 1);
  }
  enterP2PMode();
  return false;
}


// Packt bis zu DATA_MAX_SAMPLES Datensätze in 1 DATA-Paket und verschickt es.
// Bei Erfolg werden sie entfernt, sonst bleiben sie gebuffert.
static void flushOutbox() {
  if (outboxCount == 0) return;

  uint8_t sampleCount = (outboxCount < DATA_MAX_SAMPLES) ? outboxCount : DATA_MAX_SAMPLES;

  uint8_t samples[DATA_MAX_SAMPLES * DATA_SAMPLE_LEN];
  memcpy(samples, outbox, (size_t)sampleCount * DATA_SAMPLE_LEN);

  bool ok = sendData(samples, sampleCount);
  lastUplinkMs = millis();

  if (!ok) {
    Serial.printf("[LORA] Upload fehlgeschlagen, %u Datensätze bleiben gepuffert.\n", outboxCount);
    return;
  }

  uint8_t remaining = outboxCount - sampleCount;
  if (remaining) memmove(&outbox[0], &outbox[sampleCount], remaining * sizeof(LoraRecord_t));
  outboxCount    = remaining;
  outboxOldestMs = millis();
  Serial.printf("[LORA] Upload erfolgreich: %u Datensätze, %u verbleiben in der Queue.\n", sampleCount, outboxCount);
}


// Verarbeitet ein empfangenes P2P-Paket. Relay-Request-Pakete werden an TTN weitergereicht und (bei Erfolg) ACK'ed. ACK-/Fremd-Pakete werden ignoriert.
static void handleP2PPacket() {
  P2PReceived_t received;
  P2PPacketKind type = classifyP2PPacket(received);

  if (type == P2P_PKT_DATA) {
    uint8_t counterState = getP2PCounterState(received.senderNodeId, received.counter);

    if (counterState == P2P_COUNTER_DUPLICATE) {
      Serial.printf("[LORA] P2P-Duplikat von Knoten %u (Counter=%lu), ACK erneut gesendet.\n",
                    received.senderNodeId, (unsigned long)received.counter);
      p2pSendAck(received.senderNodeId, received.counter);
      notifyLoraTraffic(LORA_TRAFFIC_RELREQ_ACK_TX, received.senderNodeId);
      radio.startReceive();
      return;
    }

    if (counterState == P2P_COUNTER_REPLAY) {
      Serial.printf("[LORA] P2P-Replay von Knoten %u verworfen (Counter=%lu).\n",
                    received.senderNodeId, (unsigned long)received.counter);
      radio.startReceive();
      return;
    }

    notifyLoraTraffic(LORA_TRAFFIC_RELREQ_RX, received.senderNodeId);
    Serial.printf("[LORA] Relay-Request von Knoten %u: %u Datensaetze. (RSSI=%.1f dBm, SNR=%.1f dB)\n",
                  received.senderNodeId, received.sampleCount, radio.getRSSI(), radio.getSNR());

#if P2P_TEST_MODE
    if (!SIM_UPLINK_OK) {
      Serial.println("[LORA] (TEST) Uplink-Ausfall, leite DATA-Paket nicht weiter.");
      radio.startReceive();
      return;
    }
#else
    if (!ttnJoined) {
      Serial.println("[LORA] selbst nicht mit TTN verbunden, kann DATA-Paket nicht weiterleiten.");
      radio.startReceive();
      return;
    }
#endif

    notifyLoraTraffic(LORA_TRAFFIC_TTN_REL_TX, 0);
    bool ok = ttnSendData(received.samples, received.sampleCount, true);
    enterP2PMode();

    if (ok) {
      rememberP2PCounter(received.senderNodeId, received.counter);
      p2pSendAck(received.senderNodeId, received.counter);
      notifyLoraTraffic(LORA_TRAFFIC_RELREQ_ACK_TX, received.senderNodeId);
      radio.startReceive();
      Serial.printf("[LORA] Relay-Request von Knoten %u an TTN weitergeleitet, ACK an Ursprung.\n",
                    received.senderNodeId);
    } else {
      Serial.printf("[LORA] Relay-Upload von Knoten %u fehlgeschlagen, kein ACK zurueck.\n",
                    received.senderNodeId);
    }

    return;
  }

  radio.startReceive();
}


// Radio-Grundinitialisierung (einmalig).
static void loraInitRadio() {
  pinMode(LORA_PA_POWER, OUTPUT); digitalWrite(LORA_PA_POWER, HIGH);
  pinMode(LORA_PA_EN,    OUTPUT); digitalWrite(LORA_PA_EN,    HIGH);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  radio.begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
              LORA_CODING_RATE, LORA_SYNC_WORD, LORA_TX_POWER,
              LORA_PREAMBLE_LEN, LORA_TCXO_VOLTAGE);
  radio.setRfSwitchTable(loraRfSwitchPins, loraRfSwitchTable);
  radio.setDio2AsRfSwitch(true);
  radio.applyRxFix();
  Serial.println("[LORA] bereit.");
}


void loraTask(void* parameter) {
  Event_t event;

  loraInitRadio();
  ttnEnsureJoined();   // erster Join-Versuch beim Start
  enterP2PMode();      // Standardzustand: auf Relay-Pakete der Nachbarn lauschen

  for (;;) {
    // Eingehendes P2P-Paket
    if (loraPacketReceived) {
      loraPacketReceived = false;
      handleP2PPacket();
    }

    // Events aus der loraInbox verarbeiten
    if (xQueueReceive(loraInbox, &event, pdMS_TO_TICKS(20)) == pdTRUE) {
      switch (event.type) {
        case EVENT_NODEPACKET: {
          const NodePacket& np = event.payload.nodepacket;
          if (needPosRecord(np)) {
            outboxEnqueue(serializePos(np.nodeId, np.sensor));
            markPosSent(np.nodeId, true);
          }
          outboxEnqueue(serializeSample(np.nodeId, np.sensor));
          break;
        }

        case EVENT_IMPORTANT_DATA: {
          const NodePacket& np = event.payload.nodepacket;
          // Datensatz liegt schon in der Outbox, nur nach vorne holen.
          outboxPromoteToFront(serializeSample(np.nodeId, np.sensor));
          outboxPromoteToFront(serializePos(np.nodeId, np.sensor));
          flushOutbox();
          break;
        }

        default:
          break;
      }
    }

    if (outboxShouldFlush() && (millis() - lastUplinkMs >= LORA_MIN_UPLINK_INTERVAL_MS)) {
      flushOutbox();
    }
  }
}
