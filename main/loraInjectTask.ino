// ============================================================
// loraInjectTask.ino
// Test-Werkzeug: speist fingierte Fremdknoten-Datensätze "von außen"
// in die mainEventQueue ein, um den loraTask isoliert zu testen –
// ohne echten ESP-NOW-Empfang.
//
// Weg: mainEventQueue -> Dispatcher -> loraInbox -> loraTask
// (identisch zum echten, vom Hub empfangenen Datensatz).
//
// Aktivierung über LORA_TEST_INJECT in loraConfig.h.
// ============================================================

#include "events.h"
#include "loraConfig.h"

#if LORA_TEST_INJECT

// Baut einen EVENT_NODEPACKET mit plausiblen Zufallswerten und legt ihn in die
// mainEventQueue. nodeId ist eine fremde ID (1..254), verschieden vom eigenen NODE_ID.
static void injectFakeNodePacket() {
  Event_t ev = {};
  ev.type      = EVENT_NODEPACKET;
  ev.timestamp = millis() / 1000;

  SensorPayload_t& s = ev.payload.nodepacket.sensor;
  // Fingierte Fremd-Node-ID (1..254), verschieden vom eigenen NODE_ID.
  do {
    ev.payload.nodepacket.nodeId = (uint8_t)random(1, 255);
  } while (ev.payload.nodepacket.nodeId == NODE_ID);

  s.temperature = 15.0f + random(0, 200) / 10.0f;   // 15.0 .. 34.9 °C
  s.humidity    = 40.0f + random(0, 500) / 10.0f;   // 40.0 .. 89.9 %rH
  s.pressure    = 990.0f + random(0, 400) / 10.0f;  // 990.0 .. 1029.9 hPa
  s.light       = random(0, 20000);                 // 0 .. 20000 lx
  s.moistureRaw = random(0, 4096);                  // 0 .. 4095
  s.wet         = s.moistureRaw > 2048;
  s.vibration   = random(0, 10) == 0;               // ~10 % Erschütterung
  s.batVolts    = 3.3f + random(0, 90) / 100.0f;    // 3.30 .. 4.19 V
  s.hours       = random(0, 24);
  s.minutes     = random(0, 60);
  s.seconds     = random(0, 60);
  s.latitude    = 53.5503 + random(-50, 50) / 10000.0;   // Hamburger Innenstadt (Rathaus ± ~500 m)
  s.longitude   = 9.9920  + random(-50, 50) / 10000.0;

  if (xQueueSend(mainEventQueue, &ev, pdMS_TO_TICKS(10)) != pdTRUE)
    Serial.println("[LORA-INJECT] mainEventQueue voll, Fake-Datensatz verworfen.");
  else
    Serial.printf("[LORA-INJECT] Fake NodePacket (node=%u, %.1f C, %.1f %%rH) eingespeist.\n",
                  ev.payload.nodepacket.nodeId, s.temperature, s.humidity);
}

// Eigenständiger Task: alle 15 s einen fingierten Datensatz einspeisen.
void loraInjectTask(void* parameter) {
  (void)parameter;
  Serial.println("[LORA-INJECT] Test-Einspeisung aktiv (alle 15 s).");
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(15000));
    injectFakeNodePacket();
  }
}

#endif  // LORA_TEST_INJECT
