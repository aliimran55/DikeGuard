// ============================================================
// main.ino
// Hauptdatei: erstellt Queues, startet Tasks und dispatched Events
// ============================================================

#include "events.h"
#include "loraMessages.h"
#include "loraConfig.h"
#include "bleConfig.h"
#include "WIFI_HubApplikation.h"

// keine Sesnor Queue hier, weil Sender, kein Empfänger, kann aber geändert werden
QueueHandle_t mainEventQueue;   
QueueHandle_t displayInbox; 
QueueHandle_t loraInbox;
QueueHandle_t wlanInbox;
QueueHandle_t bleInbox;
QueueHandle_t applikationInbox;

WIFI_HubApplikation hubApplication;

// Der Event-Dispatcher liest aus der mainEventQueue und verteilt Events weiter.
// Wichtig: Ein Event soll nicht direkt von Sensor zu LoRa/WLAN gehen,
// sondern immer über den Dispatcher.
void dispatcherTask(void* parameter) {
  Event_t event;

  for (;;) {
    // Blockiert, bis ein Event kommt.
    if (xQueueReceive(mainEventQueue, &event, portMAX_DELAY) == pdTRUE) {

      Serial.printf("[DISPATCHER] Event type=%d  ts=%lu ms\n",
                    event.type, event.timestamp);

      switch (event.type) {

        case EVENT_SENSOR_DATA:
            hubApplication.sendToDisplay(event);
            if (wlanInbox)    xQueueSend(wlanInbox,    &event, pdMS_TO_TICKS(10));
            if (bleInbox)     xQueueSend(bleInbox,     &event, pdMS_TO_TICKS(10));
            //hubApplication.speichere(1, event);
            break;

        case EVENT_LORA_DATA:
          // LoRa-Daten werden aktuell nur angezeigt
          hubApplication.sendToDisplay(event);
          break;

        case EVENT_BUTTON_PRESS:
          // Button-Events sind für die WLAN/ESP-Gruppe gedacht.
          // Die WLAN-Gruppe entscheidet selbst, ob z.B. Long Press den Pairing-Mode startet.
          xQueueSend(wlanInbox, &event, pdMS_TO_TICKS(10));
          hubApplication.sendToDisplay(event);
          break;

        case EVENT_ERROR:
          Serial.printf("[DISPATCHER] FEHLER: Code %d – %s\n",
                        event.payload.error.errorCode,
                        event.payload.error.message);
          hubApplication.sendToDisplay(event);
          break;

          case EVENT_NETWORKPACKET:
            if(bleInbox){
               xQueueSend(bleInbox, &event, pdMS_TO_TICKS(10));
            }
          break;

          case EVENT_IMPORTANT_DATA:
          xQueueSend(loraInbox, &event, pdMS_TO_TICKS(10));
          //hubApplication.sendToDisplay(event);
          if(bleInbox){
               xQueueSend(bleInbox, &event, pdMS_TO_TICKS(10));
            }
          break;

          case EVENT_NODEPACKET:
            if (loraInbox) {
              xQueueSend(loraInbox, &event, pdMS_TO_TICKS(10));
            }

            //hubApplication.sendToDisplay(event);
            if (applikationInbox) {
              xQueueSend(applikationInbox, &event, pdMS_TO_TICKS(10));
            }
            break;

          case EVENT_BLE_PACKET_RECEIVED:
          if (wlanInbox) {
            xQueueSend(wlanInbox, &event, pdMS_TO_TICKS(10));
          }
          break;

          //WIFI to display
          case EVENT_WIFI_COMMAND:
            if (wlanInbox) {
              xQueueSend(wlanInbox, &event, pdMS_TO_TICKS(10));
            }
            hubApplication.sendToDisplay(event);
            break;

          case EVENT_WIFI_STATUS:
            hubApplication.sendToDisplay(event);
            break;
        default:
          Serial.printf("[DISPATCHER] Unbekannter Event-Typ: %d\n", event.type);
          break;
      }
    }
  }
}


// Vext schaltet die externe Versorgung (OLED) ein.
// Heltec-Board: aktiv LOW.
static void VextON() {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
}


void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[MAIN] System startet...");

  VextON();
  delay(100);

  // Queues anlegen
  mainEventQueue = xQueueCreate(20, sizeof(Event_t));
  displayInbox   = xQueueCreate(10, sizeof(Event_t));
  loraInbox      = xQueueCreate(10, sizeof(Event_t));

  wlanInbox      = xQueueCreate(10, sizeof(Event_t));
  applikationInbox    = xQueueCreate(10, sizeof(Event_t));

  // nur wenn es ein laeufer oder hub ist, braucht ble eine queue
  if (DEVICE_MODE == DEVICE_DIKERUNNER || DEVICE_MODE == DEVICE_HUB) {
    bleInbox      = xQueueCreate(10, sizeof(Event_t));
    if (!bleInbox) {
      Serial.println("[MAIN] Error beim Initialiseren");
      while (true) delay(1000);
    }
  }       

  if (!mainEventQueue || !displayInbox || !loraInbox || !wlanInbox) {
    Serial.println("[MAIN] Error beim Initialiseren");

    while (true) delay(1000);
  }

  // Tasks starten
  xTaskCreatePinnedToCore(dispatcherTask, "Dispatcher", 4096,  NULL, 2, NULL, 1);

  // Nur der Hub sendet über LoRa, nur Sensor-Nodes haben Sensoren.
  if (DEVICE_MODE == DEVICE_HUB) {
    xTaskCreatePinnedToCore(loraTask,    "LoRa",    16384, NULL, 3, NULL, 0);
  }
  if (DEVICE_MODE == DEVICE_NODE) {
    xTaskCreatePinnedToCore(sensorTask,  "Sensor",  4096,  NULL, 1, NULL, 1);
  } else {
    xTaskCreatePinnedToCore(buttonTask,  "Button",  2048,  NULL, 1, NULL, 1);
  }
  xTaskCreatePinnedToCore(displayTask, "Display", 8192,  NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(wlanTask,       "Wlan",       8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(applikationtask, "WIFI_HubApplikation", 4096, NULL, 1, NULL, 0);
  delay(15000); // BLE stört den Wlan-Verbindungsaufbau daher etwas delay bevor BLE startet
  

  // BLE task 
  if (DEVICE_MODE == DEVICE_DIKERUNNER || DEVICE_MODE == DEVICE_HUB) {
    xTaskCreatePinnedToCore(bleTask,        "BLE",        8192, NULL, 1, NULL, 1);
  } 

  if (LORA_TEST_INJECT) {
    xTaskCreatePinnedToCore(loraInjectTask, "LoraInject", 4096,  NULL, 1, NULL, 1);  
  }
    
  Serial.println("[MAIN] Alle Tasks gestartet.");
}


// loop() bleibt leer. Logik läuft in Tasks.
void loop() {
  vTaskDelay(portMAX_DELAY);
}