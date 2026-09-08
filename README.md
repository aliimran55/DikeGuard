# getting started
1. Repository Clonen (https://git.informatik.uni-hamburg.de/iss/stud/bp/bachelorprojekt-ss2026/freertos
)
2. in der Arduino IDE "File -> open" klicken und die main.ino datei auswählen
3. In evnets.h den gewünschten DEVICE_MODE auswählen 
4. Board verbinden und flashen

- Die für die Projektpräsentation verwendete stabile Version befindet sich im Branch `main`.
- Der Prototyp des Light-Sleep-Modus befindet sich im Branch `LIGHT_SLEEP`.



**Zur Inbetriebnahme sind verschiedene Vorkehrungen erforderlich:**

**Boards Manager:**
Für den Heltec WiFi LoRa 32 V4 muss in der Arduino-IDE der Eintrag `Heltec ESP32` (https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series) eingebunden und installiert werden.


**Benötigte Bibliotheken für die Sensorik:**

- `Heltec ESP32` (https://github.com/HelTecAutomation/Heltec_ESP32/tree/master) -- board-spezifische Funktionen, u.\,a. Display und Stromversorgung.
- `Adafruit Unified Sensor` (https://github.com/adafruit/Adafruit_Sensor) -- gemeinsame Sensor-Schnittstelle, Abhängigkeit der BME280-Bibliothek.
- `Adafruit BME280 Library` (https://github.com/adafruit/Adafruit_BME280_Library) -- Auslesen des Klimasensors.
- `BH1750` (https://github.com/claws/BH1750) -- Auslesen des Lichtsensors.


**Für BLE (nicht notwendig, wenn in `events.h` `DEVICE_MODE = DEVICE_NODE`):**

Damit BLE problemlos läuft muss zunächst die Library NimBLE-Arduino von h2Zero in
der Arduino-IDE installiert werden. Anschlieÿend muss in der nimconfig.h (standard-
mäßig unter _./Arduino/libraries/NimBLE-Arduino/src/nimconfig.h)_ extended Advertising
aktiviert werden indem die erste Flag in den extended Advertising Settings eingeschaltet
wird.
- Unsprünglich: _//#define CONFIG_BT_NIMBLE_EXT_ADV 1_
- Angepasst: _#define CONFIG_BT_NIMBLE_EXT_ADV 1_

**ESP-NOW-Setup (nur notwendig, wenn in `events.h` `DEVICE_MODE = DEVICE_HUB` oder `DEVICE_MODE = DEVICE_NODE`)**

Für die Kommunikation über ESP-NOW werden keine zusätzlichen Arduino-Bibliotheken benötigt. Die benötigten Komponenten (`WiFi.h`, `esp_now.h` und `esp_wifi.h`) sind bereits Bestandteil des ESP32 Arduino Core.

Weitere Anpassungen sind nicht erforderlich. Die Initialisierung von ESP-NOW erfolgt automatisch beim Start und umfasst unter anderem:

- Wechsel in den WiFi-Station-Modus (`WIFI_STA`)
- Initialisierung von ESP-NOW
- Festlegen des WLAN-Kanals
- Setzen des Primary Master Keys (PMK)
- Registrierung der benötigten Sende- und Empfangs-Callbacks
- Initialisierung des Pairing-Mechanismus zwischen Hub und Nodes

Nach dem Flashen suchen `DEVICE_NODE`-Boards automatisch nach einem `DEVICE_HUB` und führen das Pairing selbstständig durch. Ein manuelles Konfigurieren von MAC-Adressen oder Schlüsseln ist nicht erforderlich.

**Hinweis:** Alle ESP-NOW-Geräte müssen denselben WLAN-Kanal verwenden. Der Kanal wird aktuell im Quellcode über

```cpp
static const uint8_t WIFI_CHANNEL = 1;
```

festgelegt und muss für Hub und alle Nodes identisch sein.

**Hinweis:** Die Pairing-Informationen werden nicht dauerhaft gespeichert. Nach einem Neustart eines Nodes wird dieses automatisch in den Pairing-Mode zurückgesetzt. Geht die Verbindung zum Hub dauerhaft verloren, wechselt der Node nach mehreren fehlgeschlagenen Übertragungsversuchen ebenfalls selbstständig zurück in den Pairing-Modus.


**Deichläufer (nur notwendig, wenn in events.h DEVICE_MODE = DEVICE_DIKERUNNER):**

Für die Übertragung der Daten des Deichläufers über MQTT wird die Library PubSubClient benötigt. 
Weitere Anpassungen an der Library sind nicht erforderlich.



**LoRa-Setup (nur notwendig, wenn in events.h DEVICE_MODE = DEVICE_HUB):**
1. RadioLib (von Jan Gromes) installieren.
2. In loraConfig.h die "NODE_ID" und "HELTEC_V4_REV" anpassen. Letzteres ist auf der Rückseite des Boards zu finden.

In TTN (https://eu1.cloud.thethings.network/console/applications/)
- neuen Account anlegen
- neue Application anlegen
- im Header "End devices" der Application "Register end device" klicken
- "Enter end device specifics manually"
- Frequency plan: "Europe 836-870 MHz (SF12 for RX2)"
- LoRaWAN version: "LoRaWAN Specification 1.0.4"
- Regional Parameters: 1.0.4
- JoinEUI mit 0en füllen
- DevEUI und AppKey generieren
- Name vergeben
- "Register end device" klicken -> Gerät ist registriert
- Aufs neue Device klicken und die Keys von dort in loraConfig.h kopieren (ULL am Ende lassen)
- Optional: Unter "Payload formatters" -> "Uplink" als Formatter "Custom Javascript formatter" auswählen, den Code aus decoder.js hineinpasten und "Save changes" klicken
- In den Settings des Devices, unter "Network-Layer" -> "Custom MAC settings":
    - "Status count periodicity" und "Status time periodicity" auf 0 setzen
    - "ADR" disablen
    - "Save changes"



**In Thingsboard**

Eine neue Datenquelle im Sinne des zuvor erläuterten TTN kann über das ThingsBoard-Gateway (linke Seitenleiste unter „Gateways“) und einen darin konfigurierbaren MQTT-Konnektor angebunden werden. Beispielhafte Implementierungen sind bereits angelegt und zeigen, wie die TTN-Credentials genutzt werden müssen, um die Daten des Netzwerks zu empfangen. Besonders wichtig ist es, MQTT Version 3.1 zu nutzen, da neuere Versionen die Funktion beeinträchtigen können.

Bislang ist das System auf einen bestimmten Datensatz und eine feste JSON-Payload-Struktur konfiguriert. Von dieser darf nicht abgewichen werden, da nachfolgende Rule-Chains und Parsing-Versuche sonst mit hoher Wahrscheinlichkeit fehlschlagen.

Ein beispielhafter Hex-Payload für TTN, der zum Anlegen, Aktivieren und Anzeigen einzelner Messknoten als „aktiv“ auf den Dashboards führen sollte, liegt im Folgenden bei:

FFAEAC0B310384749800AED60BF41A5D01A90100FF02781C3103347998000298080A146801F10F00FFF75819310380649800

Die Rule-Chains, welche die Logik zum Parsen enthalten, können unter „Rule Chains“ eingesehen und erweitert werden.


**Hinweis**: Die Timings sind aktuell deutlich kürzer als im echten Betrieb. Um Duty-Cycle/TTN compliant zu sein, würden wir folgendes ändern:
1. In loraConfig.h (bei den DEVICE_HUB Boards):
    - "LORA_MIN_UPLINK_INTERVAL_MS" = 1800000UL
    - "OUTBOX_LINGER_MS" = 900000UL
2. In der sensorTask (bei den DEVICE_NODE Boards):
    - "SENSOR_PERIOD_MS" = 600000UL
    - "HEARTBEAT_MS" = 14400000UL