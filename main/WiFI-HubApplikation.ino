#include "WIFI_HubApplikation.h"
#include "events.h"

extern WIFI_HubApplikation hubApplication;

WIFI_HubApplikation::WIFI_HubApplikation()
{
  entryCount = 0;
  timerStart = 0;
  timerDeadline = 0;
  timerActive = false;
}

int WIFI_HubApplikation::getReceivedNodeCount()
{
  return entryCount;
}

//verbleibene Zeit bis zum Senden
unsigned long WIFI_HubApplikation::getRemainingTime()
{
  if(!timerActive)
  {
    return 0;
  }
  unsigned long now = millis();
  if((long)(now - timerDeadline) >= 0)
  {
    return 0;
  }
  return timerDeadline - now;
}

//Funktion um die einzelnen Data zu speichern um später aus allen Data ein Paket zu machen
//Zuerst wird geschaut ob Platz da ist und die Daten aktuell sind dann wird gespeichert
void WIFI_HubApplikation::speichere(uint8_t nodeId, Event_t event) {

  int existIndex = -1;
  //prüft, ob der Node schon etwas gesendet hat
  for(int i =0; i < entryCount; i++)
{
  if(entries[i].nodeId == nodeId)
  {
    existIndex = i;
    break;
  }
}
  event.timestamp = millis()/1000;

//wenn dieselbe Node noch mal sendet, wird nur ihr Datensatz aktualisiert
  if(existIndex != -1)
  {
    entries[existIndex].event = event;
    entries[existIndex].nodeId = nodeId;
    entries[existIndex].timestamp = event.timestamp;
    return;
  }

  if(entryCount >= MAX_NODES) {
    Serial.println("Speicher ist voll");
    return;
  }
  if(istAktuell(nodeId, event))
  {
    printf("Daten sind aktuell");
    return;
  }
  entries[entryCount].nodeId = nodeId;
  entries[entryCount].event = event; 
  entries[entryCount].timestamp = event.timestamp;
  entryCount++;

  if(!timerActive)
  {
    startTimer();
  }
  else
  {
  //wenn eine Node Daten schickt, verkürzt sich der Timer um eine Minute
  if(timerDeadline <= millis() + TIMER_REDUCTION)  
  {                                                   
    timerDeadline = millis();                         
  }                                                   
  else                                                
  {                                                   
    timerDeadline -= TIMER_REDUCTION;              
  } 
  }
}

//Funktion überpüft ob Data aktuell ist
//Dafür wird geschaut ob MAC gespeichert wurde und die Timestamp wird verglichen 
//Dann wird geschaut ob sich die Data verändert hat
bool WIFI_HubApplikation::istAktuell(uint8_t nodeId, Event_t newEvent) {
  for(int i = 0; i < entryCount; i++) {
    if(entries[i].nodeId == nodeId) {
      // Timestamp kommt jetzt aus Event_t
      if((millis() / 1000) - entries[i].event.timestamp > DATA_TIMEOUT) {
        return false;
      }
      // Daten geändert?
      if(entries[i].event.payload.sensor.temperature != newEvent.payload.sensor.temperature ||
         entries[i].event.payload.sensor.humidity != newEvent.payload.sensor.humidity) {
        return false;
      }
      return true;
    }
  }
  return false;
}

// Funktion prüft ob Erschütterungsdaten vorhanden sind
// Gibt true zurück wenn Vibration erkannt wurde, sonst false
// Der Aufrufer entscheidet dann ob sendImportantData aufgerufen wird
bool WIFI_HubApplikation::senseImportantData(uint8_t nodeId, Event_t event){
  if(event.payload.nodepacket.sensor.vibration == false){
    return false;
  }
  return true;
}


//Funktion löscht wenn Data gesendet wurden
void WIFI_HubApplikation::loesche(){
  entryCount = 0;
  timerStart = 0;
  timerDeadline = 0;
  timerActive = false;
}

//Aus allen Data wird ein einzelnes Packet erstellt um dieses nach dem Timer loszusenden
NetworkPacket WIFI_HubApplikation::createPacket() {
  NetworkPacket packet;
  packet.count = 0;
  for(int i = 0; i < entryCount; i++) {
    if(packet.count >= MAX_NODES) //Grenzen check
    {
    break;
    }
    NodePacket np;
    np.nodeId = entries[i].nodeId;
   // np.sensor = entries[i].event.payload.sensor;  // NEU
    np.sensor = entries[i].event.payload.nodepacket.sensor; 
    packet.nodes[packet.count] = np;
    packet.count++;
  }
  return packet;
}


//Timer wird gestartet wenn erstet Paket empfangen und gespeichert wird
void WIFI_HubApplikation::startTimer() {
  timerStart = millis();
  timerDeadline = timerStart + SEND_INTERVAL;
  timerActive = true;
}

//true wenn abgelaufen sonst false
bool WIFI_HubApplikation::timerAbgelaufen() {
  if(!timerActive) return false;
  if((long)(millis() - timerDeadline) >= 0) {
    timerActive = false;
    return true;
  }
  return false;
}

//Funktion erstellt eine Packet mit allen aktuellen Daten und sendet an BLE 
void WIFI_HubApplikation::sendNetworkPacket(){
  if (timerAbgelaufen()) {
    NetworkPacket packet = createPacket();
    loesche();

    Event_t event = {};
    event.type = EVENT_NETWORKPACKET;
    event.timestamp = millis() / 1000;
    event.payload.packet = packet;

    sendToDisplay(event);             
    xQueueSend(mainEventQueue, &event, pdMS_TO_TICKS(10));   
  }
}


// //Wenn Daten kommen ohne zu speichern zu LoRa senden
// void WIFI_HubApplikation::sendToLoRa(uint8_t nodeId, Event_t event) {
//   Event_t e;
//   e.type = EVENT_NODEPACKET;
//   e.timestamp = millis() / 1000;
//   e.payload.nodepacket.nodeId = nodeId;
//   e.payload.nodepacket.sensor = event.payload.sensor;
//   xQueueSend(mainEventQueue, &e, pdMS_TO_TICKS(10));
// }


// TODO Wenn Daten vom Erschütterung kommen diese Sofort senden
void WIFI_HubApplikation::sendImportantData(uint8_t nodeId, Event_t event){
  Event_t importantevent = {};
  importantevent.type = EVENT_IMPORTANT_DATA;
  importantevent.timestamp = millis() / 1000;
  importantevent.payload.nodepacket.sensor = event.payload.nodepacket.sensor;
  importantevent.payload.nodepacket.nodeId = nodeId;
  sendToDisplay(importantevent);
  xQueueSend(mainEventQueue, &importantevent, pdMS_TO_TICKS(10));
  
}

bool WIFI_HubApplikation::sendToDisplay(const Event_t& event)
{
  if(displayInbox == nullptr)
  {
    return false;
  }

  switch(event.type)
  {
    case EVENT_SENSOR_DATA:
    case EVENT_LORA_DATA:
    case EVENT_BUTTON_PRESS:
    case EVENT_ERROR:
    case EVENT_WIFI_STATUS:
    case EVENT_NODEPACKET:
    case EVENT_BLE_PACKET_RECEIVED:
    case EVENT_NETWORKPACKET:
      return xQueueSend(displayInbox, &event, pdMS_TO_TICKS(10)) ==pdTRUE;
    case EVENT_IMPORTANT_DATA:
      return xQueueSend(displayInbox, &event, pdMS_TO_TICKS(10)) == pdTRUE;

    default: 
      return false;
  }
}

void applikationtask(void* parameter){
  Event_t event;
  for (;;) {
    if (xQueueReceive(applikationInbox, &event, pdMS_TO_TICKS(10)) == pdTRUE) {
      uint8_t nodeId = event.payload.nodepacket.nodeId;

      if (hubApplication.senseImportantData(nodeId, event)) {
        hubApplication.sendImportantData(nodeId, event);
      }
      else
      {
            hubApplication.sendToDisplay(event);
      }
      hubApplication.speichere(nodeId, event);
    }
    hubApplication.sendNetworkPacket();   // prüft selbst, ob Timer abgelaufen ist

  }
}
