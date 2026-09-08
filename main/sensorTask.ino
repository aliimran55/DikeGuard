// Liest die Sensoren aus, wrappt sie in ein Event und schickt es an main.
//
// Bus-Aufteilung: Die Sensoren hängen am zweiten I2C-Bus (Wire1), damit der
// erste Bus frei bleibt für die display steuerung.
//
// USE_REAL_SENSORS 0 -> keine Hardware nötig, erzeugt Zufallswerte
//                  1 -> echte Sensoren werden ausgelesen.
#define USE_REAL_SENSORS 0

#include "events.h"

#if USE_REAL_SENSORS
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "HT_TinyGPS++.h"
#endif

// --- Pinbelegung ---
#define SDA_PIN        4 //i2c
#define SCL_PIN        3 //i2c
#define VIBRATION_PIN  47
#define MOISTURE_D_PIN 48
#define MOISTURE_A_PIN 5
#define BAT_ADC_PIN    1
#define BAT_ADC_EN     37
#define BUTTON_PIN     0   // Programmierbarer Button auf GPIO0
#define VGNSS_CTRL     34

// --- Kalibrierung ---
#define BAT_SCALE      4.9f     // Teilerfaktor Batterie-ADC

// --- Timing ---
#define SENSOR_PERIOD_MS    180000UL  // Messintervall: 3 Minuten
#define HEARTBEAT_MS        3600000UL // Heartbeat: es wird mind. stündlich gesendet
#define STARTUP_DELAY_MS    15000UL   // Wartezeit bis zur ersten Messung (andere Services hochfahren lassen)
#define VIBRATION_DEBOUNCE  500    // Mindestabstand zwischen Erschütterungen
#define TASK_TICK_MS        20     // Poll-Intervall der Task-Schleife

// ------------------------------------------------------------
// Sende-Schwellwerte
// ------------------------------------------------------------
// Gemessen wird weiterhin alle SENSOR_PERIOD_MS. Gesendet (Event in die
// mainEventQueue) wird aber nur, wenn sich ggü dem zuletzt gesendeten Wert
// mindestens ein Schwellwert geändert hat ODER der Heartbeat fällig ist.
//
// NICHT als Sende-Trigger: Luftdruck, Lux.
// Diese Werte werden weiterhin gemessen und mitgesendet, lösen aber kein
// Senden aus.
#define THR_TEMPERATURE     1.0f    // °C   – Temperatur
#define THR_HUMIDITY        5.0f    // %rH  – Luftfeuchtigkeit
#define THR_MOISTURE_PCT    5.0f    // %    – Bodenfeuchte (% vom ADC-Vollausschlag)
#define MOISTURE_FULLSCALE  4095.0f // 12-bit ADC
#define THR_MOISTURE_RAW    ((int16_t)(THR_MOISTURE_PCT / 100.0f * MOISTURE_FULLSCALE)) // ~205 = 5% des Moisture Messbereichs

// Hysterese, damit der Low-Zustand bei Lastschwankungen nicht flattert:
//   - Wechsel auf "low"  bei  < BAT_LOW_VOLTS
//   - Wechsel zurück auf "ok" erst bei >= BAT_OK_VOLTS
#define BAT_LOW_VOLTS       3.29f     // ca. 20% Ladung Rest
#define BAT_OK_VOLTS        3.34f     // ca. 35% Ladung

#define BUTTON_DEBOUNCE_MS      30    // Entprellzeit gegen mechanisches Prellen
#define BUTTON_DOUBLE_GAP_MS    400   // Zeitfenster für Double Press
#define BUTTON_LONG_PRESS_MS    1000  // Ab 1 Sekunde Long Press

#if USE_REAL_SENSORS
static BH1750 lightMeter;
static Adafruit_BME280 bme;
static TinyGPSPlus gps;
#endif


// Einmalige Hardware-Initialisierung beim Task-Start.
// Hinweis: Vext (Sensor-/Display-Versorgung) wird zentral in main.ino setup() eingeschaltet.
static void sensorInit() {
  // GPIO0 ist active-low:
  // nicht gedrückt = HIGH, gedrückt = LOW
  pinMode(BUTTON_PIN, INPUT_PULLUP);

#if USE_REAL_SENSORS
  delay(100);

  Wire1.begin(SDA_PIN, SCL_PIN);   // Sensoren auf dem zweiten Bus

  analogReadResolution(12);
  pinMode(BAT_ADC_EN, OUTPUT);
  digitalWrite(BAT_ADC_EN, HIGH);

  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire1))
    Serial.println("[SENSOR] BH1750 initialisiert");
  else
    Serial.println("[SENSOR] BH1750 nicht gefunden!");

  if (bme.begin(0x76, &Wire1))
    Serial.println("[SENSOR] BME280 initialisiert");
  else
    Serial.println("[SENSOR] BME280 nicht gefunden!");

  pinMode(VIBRATION_PIN, INPUT);
  pinMode(MOISTURE_D_PIN, INPUT);

  // GPS-Modul anschalten
  pinMode(VGNSS_CTRL,OUTPUT);
	digitalWrite(VGNSS_CTRL,LOW);
  pinMode(42,OUTPUT);
	digitalWrite(42,HIGH);
  // Initialisieren für Sammeln der GPS-Daten
  Serial1.begin(9600,SERIAL_8N1,39,38);
#else
  Serial.println("[SENSOR] Simulationsmodus.");
#endif
}

// ------------------------------------------------------------
// Button-Erkennung für GPIO0
// Erkennt Single Press, Double Press und Long Press
// ------------------------------------------------------------

static bool buttonLastRawPressed = false;   // Letzter direkt gelesener Zustand
static bool buttonStablePressed  = false;   // Entprellter Zustand

static unsigned long buttonLastChange     = 0; // Zeitpunkt der letzten Rohsignal-Änderung
static unsigned long buttonPressStart     = 0; // Zeitpunkt, wann der Button gedrückt wurde
static unsigned long buttonFirstClickTime = 0; // Zeitpunkt des ersten kurzen Klicks

static bool buttonLongSent = false;          // Verhindert mehrfaches Long-Press-Senden
static uint8_t buttonClickCount = 0;         // Zählt Klicks für Single/Double Press
static uint32_t buttonLastClickDurationMs = 0;

// GPIO0 ist active-low:
// HIGH = nicht gedrückt, LOW = gedrückt
static bool isButtonPressed() {
  return digitalRead(BUTTON_PIN) == LOW;
}

// Schickt ein Button-Event in die zentrale mainEventQueue
static void sendButtonEvent(ButtonPressType_e pressType, uint32_t pressDurationMs) {
  Event_t event = {};

  event.type = EVENT_BUTTON_PRESS;
  event.timestamp = millis();
  event.payload.button.pressType = pressType;
  event.payload.button.gpio = BUTTON_PIN;
  event.payload.button.pressDurationMs = pressDurationMs;

  // Nicht direkt WLAN aufrufen, sondern sauber über den Dispatcher gehen
  if (xQueueSend(mainEventQueue, &event, pdMS_TO_TICKS(10)) != pdTRUE) {
    Serial.println("[BUTTON] WARNUNG: mainEventQueue voll");
  }
}

// Wird regelmäßig aus sensorTask() aufgerufen
static void pollButton(unsigned long now) {
  bool rawPressed = isButtonPressed();

  // Rohzustand hat sich geändert → Entprell-Timer neu starten
  if (rawPressed != buttonLastRawPressed) {
    buttonLastRawPressed = rawPressed;
    buttonLastChange = now;
  }

  // Signal muss erst stabil sein
  if (now - buttonLastChange < BUTTON_DEBOUNCE_MS) {
    return;
  }

  // Entprellter Zustand hat sich geändert
  if (rawPressed != buttonStablePressed) {
    buttonStablePressed = rawPressed;

    if (buttonStablePressed) {
      // Button wurde gedrückt
      buttonPressStart = now;
      buttonLongSent = false;
    } else {
      // Button wurde losgelassen
      uint32_t pressDurationMs = now - buttonPressStart;
      buttonLastClickDurationMs = pressDurationMs;

      if (!buttonLongSent) {
        // Kurzer Klick zählt für Single/Double Press
        if (buttonClickCount == 0) {
          buttonFirstClickTime = now;
        }

        buttonClickCount++;

        // Zweiter Klick innerhalb des Zeitfensters → Double Press
        if (buttonClickCount >= 2) {
          if (now - buttonFirstClickTime <= BUTTON_DOUBLE_GAP_MS) {
            sendButtonEvent(BUTTON_DOUBLE_PRESS, pressDurationMs);
          }

          // Zu späte zweite Klicks nicht als Double Press zählen
          buttonClickCount = 0;
        }
      } else {
        // Nach Long Press soll kein Single Press mehr entstehen
        buttonClickCount = 0;
      }
    }
  }

  // Long Press wird erkannt, während der Button noch gedrückt ist
  if (buttonStablePressed &&
      !buttonLongSent &&
      now - buttonPressStart >= BUTTON_LONG_PRESS_MS) {
    sendButtonEvent(BUTTON_LONG_PRESS, now - buttonPressStart);
    buttonLongSent = true;
    buttonClickCount = 0;
  }

  // Single Press erst senden, wenn kein zweiter Klick mehr kommen kann
  if (!buttonStablePressed &&
      buttonClickCount == 1 &&
      now - buttonFirstClickTime > BUTTON_DOUBLE_GAP_MS) {
    sendButtonEvent(BUTTON_SINGLE_PRESS, buttonLastClickDurationMs);
    buttonClickCount = 0;
  }
}

// ============================================================
//  Sende-Entscheidung
//  Vergleicht die aktuelle Messung mit dem zuletzt GESENDETEN Stand
//  und entscheidet, ob das Event in die Queue gegeben wird.
// ============================================================

// Zuletzt gesendete daten.
typedef struct {
  bool     valid;        // wurde schon mindestens einmal gesendet?
  float    temperature;
  float    humidity;
  int16_t  moistureRaw;
  bool     wet;          // Bodenfeuchte-Bool (nass/trocken)
  bool     batLow;       // entprellter Low-Batt-Zustand (Hysterese)
} TxSnapshot_t;

static TxSnapshot_t  lastTx     = {};
static unsigned long lastTxTime = 0;   // millis() des letzten Sendens (für Heartbeat)

// Wertet den Low-Batt-Zustand mit Hysterese aus.
static bool evalBatLow(float volts, bool prevLow) {
  return prevLow ? (volts < BAT_OK_VOLTS)   // erst bei >= BAT_OK_VOLTS wieder "ok"
                 : (volts < BAT_LOW_VOLTS);  // Wechsel auf "low" bei < BAT_LOW_VOLTS
}

// Der heartbeat läuft jetzt einfach stündlich. Schöner fänd ich den zu jeder vollen Stunde laufen lassen, kannst du das ändern, sobald gps läuft? Die millis können dann als fallback drin bleiben, falls gps nicht mehr läuft
// Entscheidet, ob die aktuelle Messung gesendet werden soll.
// batLow wird vorab ausgewertet übergeben, damit es auch
// für den anschließenden Commit konsistent ist. reason -> Log-Grund.
static bool shouldTransmit(const SensorPayload_t& s, bool batLow,
                           unsigned long now, const char** reason) {
  // 1) Erste Messung immer senden -> setzt den Referenzwert
  if (!lastTx.valid)                                                  { *reason = "init";      return true; }
  // 2) Heartbeat: mindestens stündlich (unsigned-Subtraktion ist überlaufsicher)
  if (now - lastTxTime >= HEARTBEAT_MS)                               { *reason = "heartbeat"; return true; }
  // 3) Temperatur
  if (fabsf(s.temperature - lastTx.temperature) >= THR_TEMPERATURE)   { *reason = "temp";      return true; }
  // 4) Luftfeuchtigkeit
  if (fabsf(s.humidity - lastTx.humidity) >= THR_HUMIDITY)            { *reason = "hum";       return true; }
  // 5) Bodenfeuchte (analog)
  if (abs((int)s.moistureRaw - (int)lastTx.moistureRaw) >= THR_MOISTURE_RAW) { *reason = "moist"; return true; }
  // 6) Bodenfeuchte-Bool: Zustandswechsel nass/trocken
  if (s.wet != lastTx.wet)                                            { *reason = "wet-bool";  return true; }
  // 6b) Vibration: Erschütterung seit der letzten Messung -> sofort senden
  if (s.vibration)                                                    { *reason = "vibration"; return true; }
  // 7) Akku low: Zustandswechsel (mit Hysterese vorab ausgewertet)
  if (batLow != lastTx.batLow)                                        { *reason = "batt-low";  return true; }
  return false;
}

// Übernimmt die gesendete Messung als neuen Referenzstand.
static void commitTxState(const SensorPayload_t& s, bool batLow, unsigned long now) {
  lastTx.valid       = true;
  lastTx.temperature = s.temperature;
  lastTx.humidity    = s.humidity;
  lastTx.moistureRaw = s.moistureRaw;
  lastTx.wet         = s.wet;
  lastTx.batLow      = batLow;
  lastTxTime         = now;
}

// Liest alle Sensoren, baut das Event und sendet es bei Bedarf (Schwellwert/
// Heartbeat). vibration=true setzt das Vibrations-Flag und erzwingt damit das
// Senden – wird beim Sofort-Trigger einer Erschütterung genutzt.
static void measureAndSend(unsigned long now, bool vibration) {
  Event_t event;

#if USE_REAL_SENSORS
  int   rawMoisture = analogRead(MOISTURE_A_PIN);
  bool  wet         = !digitalRead(MOISTURE_D_PIN);
  float light       = lightMeter.readLightLevel();
  float temp        = bme.readTemperature();
  float humidity    = bme.readHumidity();
  float pressure    = bme.readPressure() / 100.0F;
  float batVolts    = analogReadMilliVolts(BAT_ADC_PIN) * BAT_SCALE / 1000.0F;
  int   seconds     = gps.time.second();
  int   minutes     = gps.time.minute();
  int   hours       = gps.time.hour();
  double latitude   = gps.location.lat();
  double longitude  = gps.location.lng();
#else
  int   rawMoisture = random(0, 4096);
  bool  wet         = random(0, 2);
  float light       = random(0, 10000) / 10.0f;
  float temp        = random(200, 300)  / 10.0f;
  float humidity    = random(400, 600)  / 10.0f;
  float pressure    = random(9800, 10300) / 10.0f;
  float batVolts    = random(330, 420)  / 100.0f;
  int   seconds     = random(0, 60);
  int   minutes     = random(0, 60);
  int   hours       = random(0, 24);
  double latitude   = 53.5400 + random(0, 1001)  / 100000.0;
  double longitude  =  9.9600 + random(0, 4001)  / 100000.0;
#endif

  event.type                        = EVENT_SENSOR_DATA;
  event.timestamp                   = now;
  event.payload.sensor.temperature  = temp;
  event.payload.sensor.humidity     = humidity;
  event.payload.sensor.light        = light;
  event.payload.sensor.pressure     = pressure;
  event.payload.sensor.moistureRaw  = (int16_t)rawMoisture;
  event.payload.sensor.wet          = wet;
  event.payload.sensor.batVolts     = batVolts;
  event.payload.sensor.vibration    = vibration;
  event.payload.sensor.seconds      = seconds;
  event.payload.sensor.minutes      = minutes;
  event.payload.sensor.hours        = hours;
  event.payload.sensor.latitude     = latitude;
  event.payload.sensor.longitude    = longitude;

  Serial.println("[SENSOR] ---");
  Serial.printf("[SENSOR] Licht:       %.1f lx\n", light);
  Serial.printf("[SENSOR] Temperatur:  %.2f °C\n", temp);
  Serial.printf("[SENSOR] Lufteuchte:     %.2f %%\n", humidity);
  Serial.printf("[SENSOR] Druck:       %.2f hPa\n", pressure);
  Serial.printf("[SENSOR] Bodenfeuchte: %d%s\n", rawMoisture, wet ? " [nass]" : "");
  Serial.printf("[SENSOR] Batterie:    %.2f V\n", batVolts);
  Serial.printf("[SENSOR] Zeit:   %02d:%02d:%02d\n", hours, minutes, seconds);
  Serial.printf("[SENSOR] LAT: %f\n", latitude);
  Serial.printf("[SENSOR] LON: %f\n", longitude);
  if (vibration) Serial.println("[SENSOR] Erschütterung");

  // --- Sende-Entscheidung: nur bei Schwellwert-Überschreitung oder Heartbeat ---
  const char* reason  = "";
  bool        batLow  = evalBatLow(batVolts, lastTx.batLow);

  if (shouldTransmit(event.payload.sensor, batLow, now, &reason)) {
    Serial.printf("[SENSOR] -> senden (Grund: %s)\n", reason);
    if (xQueueSend(mainEventQueue, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.println("[SENSOR] WARNUNG: queue voll!");
    } else {
      commitTxState(event.payload.sensor, batLow, now);
    }
  } else {
    Serial.println("[SENSOR] -> kein Schwellwert erreicht, nicht gesendet.");
  }
}

// Für Geräte ohne Sensoren (Hub/Dikerunner): pollt nur den GPIO0-Button,
// damit Display-Menü und Pairing weiterhin Button-Events bekommen.
void buttonTask(void* parameter) {
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  for (;;) {
    pollButton(millis());
    vTaskDelay(pdMS_TO_TICKS(TASK_TICK_MS));
  }
}

void sensorTask(void* parameter) {
  sensorInit();

  // Erste Messung erst nach STARTUP_DELAY_MS (nicht sofort), damit die übrigen
  // Services bereits laufen. Button-/Vibrations-Polling läuft währenddessen aber.
  unsigned long lastRead      = millis() + STARTUP_DELAY_MS - SENSOR_PERIOD_MS;
  unsigned long lastVibration = 0;

  for (;;) {
    unsigned long now = millis();

    // Button regelmäßig auslesen, unabhängig vom Sensor-Messintervall
    pollButton(now);


#if USE_REAL_SENSORS
    // GPS auslesen
    while (Serial1.available())
    {
      gps.encode(Serial1.read());
    }
    // Erschütterung wird laufend gepollt (entprellt) und löst SOFORT eine
    // Messung samt Sendung aus – ohne auf das Messintervall zu warten.
    if (digitalRead(VIBRATION_PIN) && now - lastVibration >= VIBRATION_DEBOUNCE) {
      lastVibration = now;
      Serial.println("[SENSOR] Erschütterung erkannt -> sofort senden");
      measureAndSend(now, true);
      lastRead = now;   // periodischen Messzyklus neu takten
    }
#endif

    // Periodische Messung
    if (now - lastRead >= SENSOR_PERIOD_MS) {
      lastRead = now;
      measureAndSend(now, false);
    }

    vTaskDelay(pdMS_TO_TICKS(TASK_TICK_MS));
  }
}
