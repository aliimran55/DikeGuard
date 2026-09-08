#pragma once
#include <Arduino.h>
#include <esp_now.h>
#include "events.h"
// Header-Datei für die Applikationsschicht des Transmission Node (Hub)
// Enthält alle Structs und die Klassendefinition für WIFI_HubApplikation
// Structs die für das Senden benötigt werden sind hier definiert damit der Hub-Code übersichtlich bleibt


//For update-check
//a node sends every 5 seconds, then we set 15 seconds as a timeout 
//after a timeout withot new messages, the data is marked as outdated
const uint32_t DATA_TIMEOUT = 15;


//der Hub sammelt Daten maximal zehn Minuten
const unsigned long SEND_INTERVAL = 10UL * 60UL * 1000UL;

//jede neue Node verkürzt den Timer um eine Minute
const unsigned long TIMER_REDUCTION = 1UL * 60UL * 1000UL;


// Memory of different sensors
typedef struct {
  uint8_t mac[6];
  uint8_t lmk[16];
  bool active;
} Node;



// Ein eigenes Struct um die Daten unabhängig vom Gerät zu speichern also Gerät ist noch da aber Fokus auf Data
typedef struct{
  uint8_t nodeId;
  uint32_t timestamp;
  Event_t event;
} SensorEntry;



class WIFI_HubApplikation {
private:
 // int maxNodes;
  // Ein Array mit den jeweiligen Entries
  //SensorEntry entries [MAX_NODES *10]; //Array mit den Einträgen. Größer als das Array in den die Knoten gespeichert werden, weil ein Knoten mehrmal senden kann
  SensorEntry entries [MAX_NODES]; //one entry per one node
  int entryCount;  //Anzahl der Einträge 
  unsigned long timerStart;  // Zeitpunkt wann Timer gestartet wurde
  bool timerActive;          // true wenn Timer läuft
  unsigned long timerDeadline;
public:
WIFI_HubApplikation();

  void applikationtask(void* parameter);

  //Funktion um die einzelnen Data zu speichern um später aus allen Data ein Paket zu machen
  void speichere(uint8_t nodeId, Event_t event);

  //Funktion überpüft ob Data aktuell ist
  bool istAktuell(uint8_t nodeId, Event_t newEvent);

  //Funktion schaut ob Data von Erschütterung da sind und ruft SendImportantData auf wenn ja
  bool senseImportantData(uint8_t nodeId, Event_t event);
  
  //Funktion löscht wenn Data gesendet wurden
  void loesche();

  //Aus allen Data wird ein einzelnes Packet erstellt um dieses nach dem Timer loszusenden
  NetworkPacket createPacket();

  
  //Timer wird gestartet wenn erstet Paket empfangen und gespeichert wird
  void startTimer();

  
  //true wenn abgelaufen sonst false
  bool timerAbgelaufen();


  //Funktion sendet Paket wenn Timer fertig ist
  void sendNetworkPacket();

  // void sendToLoRa(uint8_t nodeId, Event_t event);

  //Funktion sendet die Data sofort wenn ErschütterungSensor Data da ist
  void sendImportantData(uint8_t nodeId, Event_t event);

  //Anzahl der Nodes, von denen aktuell die Daten gesammelt wurden
  int getReceivedNodeCount();

  unsigned long getRemainingTime();

  //leitet Events aus der Application an das Display weiter
  bool sendToDisplay(const Event_t& event);
  
};


