//  Display-Task: rendert die letzten Sensordaten auf das OLED und loggt
//  eingehende Events zusätzlich per Serial.
//
//  Das OLED hängt am ERSTEN I2C-Bus (Wire, OLED-Pins SDA_OLED/SCL_OLED) –
//  die Sensoren liegen auf Wire1, es gibt also keinen Bus-Konflikt.
//  Vext (Versorgung) wird zentral in main.ino setup() eingeschaltet.

#include "events.h"
#include "HT_SSD1306Wire.h"
#include "loraConfig.h"
#include "esp_mac.h"

extern SSD1306Wire display;

//Icons fuer Home Screen
static const unsigned char wlan[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0xF0, 0x0F, 0xFC, 0x3F,
  0x0E, 0x70, 0xE6, 0x67, 0xF0, 0x0F, 0x18, 0x18,
  0xC8, 0x13, 0xE0, 0x07, 0x20, 0x04, 0x80, 0x01,
  0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const unsigned char bluetooth[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x80, 0x03,
  0x80, 0x04, 0x90, 0x04, 0xA0, 0x02, 0xC0, 0x01,
  0x80, 0x00, 0xC0, 0x01, 0xA0, 0x02, 0x90, 0x04,
  0x80, 0x02, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00
};

static const unsigned char lora[] PROGMEM = {
  0xC0, 0x03, 0x30, 0x0C, 0xD0, 0x0B, 0x68, 0x16,
  0xA8, 0x15, 0xA8, 0x15, 0xA8, 0x15, 0x98, 0x19,
  0x50, 0x0A, 0xC0, 0x03, 0xC0, 0x03, 0x60, 0x06,
  0x60, 0x06, 0xB0, 0x0D, 0x70, 0x0E, 0x10, 0x08
};

static const unsigned char light[] PROGMEM = {
  0xE0, 0x07, 0x10, 0x0A, 0x08, 0x14, 0x00, 0x18,
  0x04, 0x20, 0xE4, 0x27, 0xA4, 0x25, 0x40, 0x02,
  0x08, 0x10, 0x10, 0x0A, 0xE0, 0x07, 0x00, 0x07,
  0x20, 0x04, 0xE0, 0x07, 0x20, 0x04, 0xC0, 0x03
};

static const unsigned char battery[] PROGMEM = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xFE, 0x1F, 0x01, 0x20, 0x01, 0x40, 0x01, 0xC0,
  0x01, 0xC0, 0x01, 0x40, 0x01, 0x20, 0xFE, 0x1F,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const unsigned char temperatur[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x3C, 0x0C,
  0x3C, 0x3F, 0x98, 0x13, 0xC0, 0x01, 0xC0, 0x00,
  0xC0, 0x00, 0xC0, 0x00, 0x80, 0x01, 0x80, 0x1F,
  0x00, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const unsigned char water[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01,
  0xC0, 0x03, 0xC0, 0x03, 0xE0, 0x07, 0xF0, 0x0F,
  0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xE0, 0x07,
  0xC0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const unsigned char humid[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01,
  0xC0, 0x03, 0xE0, 0x07, 0xE0, 0x07, 0xB0, 0x0D,
  0xF0, 0x0E, 0x70, 0x0F, 0xB0, 0x0D, 0xE0, 0x07,
  0xC0, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const unsigned char pressure[] PROGMEM = {
  0x00, 0x00, 0x80, 0x01, 0xE0, 0x07, 0x30, 0x0C,
  0x18, 0x1A, 0x08, 0x13, 0x88, 0x11, 0x08, 0x10,
  0x08, 0x10, 0x10, 0x08, 0x70, 0x0E, 0xC0, 0x03,
  0x80, 0x01, 0xFC, 0x3F, 0xFC, 0x3F, 0x00, 0x00
};

// beim Kompilieren wird festgelegt, was für ein Menu Aktiv ist.

#if DEVICE_MODE == DEVICE_NODE
static const MenuItem_t mainMenuItems[] = {
  { "Device Info", SCREEN_DEVICE_INFO },
  { "Sensordaten", SCREEN_SENSORS },
  { "Messages",    SCREEN_MESSAGES },
  { "Status",      SCREEN_ERRORS },
};
static const MenuItem_t deviceInfoMenuItems[] = {
  { "Node ID",     SCREEN_DEVICE_NODE_ID },
  { "MAC Address", SCREEN_DEVICE_MAC },
  { "GPS",         SCREEN_DEVICE_GPS },
  { "Timestamp",   SCREEN_DEVICE_TIMESTAMP },
};
static const MenuItem_t messagesMenuItems[] = {
  { "Total Messages", SCREEN_NUM_MSG },
  { "Last Message",   SCREEN_LAST_MESSAGE },
  { "LoRa Traffic",   SCREEN_LORA },
  { "Pairing Check",  SCREEN_PAIRING_CHECK },
};

#elif DEVICE_MODE == DEVICE_HUB
static const MenuItem_t mainMenuItems[] = {
  { "Device Info", SCREEN_DEVICE_INFO },
  { "Messages",    SCREEN_MESSAGES },
  { "Status",      SCREEN_ERRORS },
};
static const MenuItem_t deviceInfoMenuItems[] = {
  { "Node ID",     SCREEN_DEVICE_NODE_ID },
  { "MAC Address", SCREEN_DEVICE_MAC },
  { "GPS",         SCREEN_DEVICE_GPS },
  { "Timestamp",   SCREEN_DEVICE_TIMESTAMP },
};
static const MenuItem_t messagesMenuItems[] = {
  { "Total Messages", SCREEN_NUM_MSG },
  { "Last Message",   SCREEN_LAST_MESSAGE },
  { "LoRa Traffic",   SCREEN_LORA },
  { "Pairing Check",  SCREEN_PAIRING_CHECK },
};

#elif DEVICE_MODE == DEVICE_DIKERUNNER
static const MenuItem_t mainMenuItems[] = {
  { "Device Info", SCREEN_DEVICE_INFO },
  { "Messages",    SCREEN_MESSAGES },
  { "Status",      SCREEN_ERRORS },
};
static const MenuItem_t deviceInfoMenuItems[] = {
  { "Node ID",     SCREEN_DEVICE_NODE_ID },
  { "MAC Address", SCREEN_DEVICE_MAC },
  { "GPS",         SCREEN_DEVICE_GPS },
  { "Timestamp",   SCREEN_DEVICE_TIMESTAMP },
};
static const MenuItem_t messagesMenuItems[] = {
  { "Total Messages", SCREEN_NUM_MSG },
  { "Last Message",   SCREEN_LAST_MESSAGE },
  { "LoRa Traffic",   SCREEN_LORA },
  { "Pairing Check",  SCREEN_PAIRING_CHECK },
};

static char runnerBleHubIdsText[128] = "Received Hub IDs: ";

#endif

static const uint8_t mainMenuItemCount       = sizeof(mainMenuItems) / sizeof(mainMenuItems[0]);
static const uint8_t deviceInfoMenuItemCount = sizeof(deviceInfoMenuItems) / sizeof(deviceInfoMenuItems[0]);
static const uint8_t messagesMenuItemCount   = sizeof(messagesMenuItems) / sizeof(messagesMenuItems[0]);

static Screen_t currentScreen = SCREEN_HOME;

static uint8_t selectedMenuItem = 0;        //für menu
static uint8_t selectedDeviceInfoItem = 0;  //für "device info"

static char deviceMacAddress[18] = "00:00:00:00:00:00"; //speichert echte mac addresse

//für Messages
static uint32_t receivedMsgCount = 0;
static uint8_t selectedMsgItem = 0;
static uint32_t warningCount = 0;


// Letzte Nachricht variables
static bool hasLastNodeMessage = false;
static uint8_t lastNodeId = 0;
static SensorPayload_t lastNodeSensor = {};

static uint8_t lastMsgHours = 0;
static uint8_t lastMsgMinutes = 0;
static uint8_t lastMsgSeconds = 0;

// System Screen Variables
static char espNowStatusText[24] = "DATA";
static char loraStatusText[24] = "Idle";
static char bleStatusText[24] = "Ready";
static char sensorStatusText[24] = "OK";

// Warning Variables
static bool previousWetState = false;
static bool previousVibrationState = false;


//für pairing check - noch keine echte logik
//static bool pairingDemoActive = false;
//static const char pairingMac[] = "8C:FD:49:B5:47:60";
//static uint8_t pairingNodeId = 2;
//static uint8_t pairingTotalNodes = 2;

//Pairing UI state
static bool pairingModeActive = false;
static char pairingMac[18] = "--:--:--:--:--:--";
static uint8_t pairingNodeId = 0;
static uint8_t pairingTotalNodes = 0;
static char pairingStatusText[32] = "OFF";

// Speichert dei Daten
static SensorPayload_t latestSensor = {};

// letzte 5 LoRa-Funkverkehr-Ereignisse, neueste Zeile unten
static char loraLog[5][24] = { "", "", "", "", "" };

static struct {
  char message[64];
  bool hasError;
} latestError = { "Keine Fehler", false };

// Wifi Helper
static void sendWifiCommand(WifiCommandKind_e command)
{
  Event_t ev = {};
  ev.type = EVENT_WIFI_COMMAND;
  ev.timestamp = millis();
  ev.payload.wifiCommand.command = command;

  if (mainEventQueue) {
    xQueueSend(mainEventQueue, &ev, pdMS_TO_TICKS(10));
  }
}





// Anzeieg was für ein Device type aktiv ist
#if DEVICE_MODE == DEVICE_NODE
static const char* const DEVICE_MODE_LABEL = "NODE";
#elif DEVICE_MODE == DEVICE_HUB
static const char* const DEVICE_MODE_LABEL = "HUB";
#elif DEVICE_MODE == DEVICE_DIKERUNNER
static const char* const DEVICE_MODE_LABEL = "RUNNER";
#endif

// Ab hier renderfunktionen pro Screen eine
static void drawHome(){

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  display.drawString(128, 0, DEVICE_MODE_LABEL);

  display.drawXbm(2, 2, 16, 16, wlan);
  display.drawXbm(2, 20, 16, 16, lora);
  display.drawXbm(2, 38, 16, 16, bluetooth);
  char timeText[16];

  
  //display.drawString(20, 50, "Nodes:4 | MSG:15");
  #if DEVICE_MODE == DEVICE_DIKERUNNER
    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);

    display.drawStringMaxWidth(25, 10, 106, runnerBleHubIdsText);
  #else
    const int rectW = 60;
    const int rectH = 30;
    const int x = (128 - rectW) / 2;
    const int y = (64 - rectH) / 2;
    display.drawRect(x, y, rectW, rectH);
    snprintf(timeText, sizeof(timeText), "%02u:%02u:%02u",
        lastMsgHours, lastMsgMinutes, lastMsgSeconds);

    display.setFont(ArialMT_Plain_10);
    display.setTextAlignment(TEXT_ALIGN_CENTER);

    // Anzeige im Frame
    if (receivedMsgCount == 0) {
      display.drawString(x + rectW / 2, y + rectH / 2 - 5, "--:--:--");
    } else {
    display.drawString(x + rectW / 2, y + rectH / 2 - 5, timeText);
    }
    char homeLine[32];

    snprintf(
      homeLine,
      sizeof(homeLine),
      "Nodes:%u | MSG:%lu",
      pairingTotalNodes,
      static_cast<unsigned long>(receivedMsgCount)
    );

    display.drawString(60, 50, homeLine);
  #endif


}


// zeichent immer das men als liste untereinadern
static void drawMenuList(const char* title, const MenuItem_t* items, uint8_t count, uint8_t selectedIndex, uint8_t rowHeight) {
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);

  display.drawString(0, 0, title);
  display.drawLine(0, 12, 127, 12);

  for (uint8_t i = 0; i < count; i++) {
    uint8_t y = 15 + i * rowHeight;
    if (i == selectedIndex) {
      display.setColor(WHITE);
      display.fillRect(0, y, 128, 12);
      display.setColor(BLACK);
      display.drawString(0, y + 1, ">");
      display.drawString(10, y + 1, items[i].label);

      display.setColor(WHITE);
    } else {
      display.drawString(10, y + 1, items[i].label);
    }
  }
  display.setColor(WHITE);
}

static void drawMenu() {
  drawMenuList("MENU", mainMenuItems, mainMenuItemCount, selectedMenuItem, 12);
}


static void drawDeviceInfoMenu() {
  drawMenuList("DEVICE INFO", deviceInfoMenuItems, deviceInfoMenuItemCount, selectedDeviceInfoItem, 12);
}

static void drawDeviceNodeId() {
  char buf[32];

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, "NODE ID");
  display.drawLine(25, 12, 103, 12);

  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(NODE_ID));

  display.setFont(ArialMT_Plain_24);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 24, buf);

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 52, "<- Back");
}
//
static void readMacAddress()
{
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(deviceMacAddress, sizeof(deviceMacAddress), 
            "%02X:%02X:%02X:%02X:%02X:%02X", 
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void drawDeviceMac() {
  char ersteLinie[9];
  char zweiteLinie[9];

  strncpy(ersteLinie, deviceMacAddress, 8);
  ersteLinie[8] = '\0';
  strncpy(zweiteLinie, deviceMacAddress + 9, 8);
  zweiteLinie[8] = '\0';

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, "MAC-Address");
  display.drawLine(25, 12, 103, 12);

  display.drawString(64, 20, ersteLinie);
  display.drawString(64, 33, zweiteLinie);

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 52, "<- Back");
}

static const uint8_t gpsIcon[] PROGMEM = {
  0xE0, 0x07,
  0xF8, 0x1F,
  0x1C, 0x38,
  0x0E, 0x70,
  0x06, 0x60,
  0xC6, 0x63,
  0xC6, 0x63,
  0x06, 0x60,
  0x0E, 0x70,
  0x1C, 0x38,
  0x38, 0x1C,
  0x70, 0x0E,
  0xE0, 0x07,
  0xC0, 0x03,
  0x80, 0x01,
  0x00, 0x00
};

static void drawDeviceGps() {
  char lat[24];
  char lon[24];

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, "GPS");
  display.drawLine(25, 12, 103, 12);

  display.drawXbm(16, 27, 16, 16, gpsIcon);

  snprintf(lat, sizeof(lat), "%.3f N", latestSensor.latitude);
  snprintf(lon, sizeof(lon), "%.3f S", latestSensor.longitude);
  

  display.drawString(72, 20, lat);
  display.drawString(72, 38, lon);

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 52, "<- Back");
}

static void drawDeviceTimestamp() {
  char timeText[16];

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, "TIMESTAMP");
  display.drawLine(25, 12, 103, 12);

  snprintf(timeText, sizeof(timeText), "%02u:%02u:%02u", 
                (unsigned int)latestSensor.hours, (unsigned int)latestSensor.minutes, (unsigned int)latestSensor.seconds);
  display.drawString(64, 22, timeText);            

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 52, "<- Back");
}

static void drawSensors() {
  char buf[32];
  display.setFont(ArialMT_Plain_10);

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  snprintf(buf, sizeof(buf), "%.1f lx", latestSensor.light);
  display.drawString(16, 3, buf);
  display.drawXbm(0, 0, 16, 16, light);

  snprintf(buf, sizeof(buf), "%.1fC", latestSensor.temperature);
  display.drawString(16, 23, buf);
  display.drawXbm(0, 20, 16, 16, temperatur);

  snprintf(buf, sizeof(buf), "%d%s", latestSensor.moistureRaw, latestSensor.wet ? " [wet]" : "");
  display.drawString(16, 43, buf);
  display.drawXbm(0, 40, 16, 16, water);

  display.setTextAlignment(TEXT_ALIGN_RIGHT);
  snprintf(buf, sizeof(buf), "%.2fV", latestSensor.batVolts);
  display.drawString(128, 3, buf);
  display.drawXbm(70, 0, 16, 16, battery);
  
  snprintf(buf, sizeof(buf), "%.1f%%rH", latestSensor.humidity);
  display.drawString(128, 23, buf);
  display.drawXbm(70, 20, 16, 16, humid);

  snprintf(buf, sizeof(buf), "%1.fhPa", latestSensor.pressure);
  display.drawString(128, 43, buf);
  display.drawXbm(70, 40, 16, 16, pressure);

  //display.drawString(16, 43, latestSensor.vibration ? "! Vibration" : "");
}


static void drawErrors() {
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, "=== SYSTEM STATUS ===");

  if (latestError.hasError) {
    display.drawString(0, 16, "Letzter Fehler:");
    display.drawString(0, 28, latestError.message);
  } else {
    display.drawString(0, 24, "Alle Systeme nominal.");
  }
}

static void drawLora() {
  display.setColor(WHITE);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);

  display.drawString(64, 0, "LORA TRAFFIC");
  display.drawLine(20, 12, 108, 12);

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  // zeigt die letzten 4 Einträge, Platz reicht wegen Header/Back nicht für alle 5
  for (uint8_t i = 1; i < 5; i++) {
    display.drawString(0, 14 + (i - 1) * 10, loraLog[i]);
  }

  display.drawString(0, 54, "<- Back");
}

static void drawMessages() {
  drawMenuList("MESSAGES", messagesMenuItems, messagesMenuItemCount, selectedMsgItem, 12);
}

static void drawTotalMsg()
{
  char messageText[16];
  char warningText[24];
  snprintf(messageText, sizeof(messageText), "%lu", static_cast<unsigned long>(receivedMsgCount));
  snprintf(warningText, sizeof(warningText), "Warnings: %lu", static_cast<unsigned long>(warningCount));

  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setColor(WHITE);
  display.drawString(64, 0, "Anzahl Messages");
  display.drawLine(25, 12, 103, 12);
  display.setFont(ArialMT_Plain_24);
  display.drawString(64, 17, messageText);

  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 43, warningText);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 52, "<- Back");
}

//handshake icon
static const uint8_t handshakeIcon[] PROGMEM = {
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x60, 0x00, 0x00, 0x06,
  0x90, 0x00, 0x00, 0x09,
  0x88, 0xC0, 0x86, 0x08,
  0x88, 0x3F, 0xF0, 0x11,
  0x44, 0x10, 0x00, 0x11,
  0x44, 0x08, 0x00, 0x22,
  0x20, 0xC4, 0x03, 0x22,
  0x22, 0x34, 0x0C, 0x44,
  0x01, 0x08, 0x10, 0x44,
  0x11, 0x00, 0x20, 0x48,
  0xBE, 0x00, 0x40, 0x6C,
  0x40, 0x01, 0x80, 0x03,
  0x20, 0x07, 0x10, 0x03,
  0x20, 0x09, 0x60, 0x02,
  0xE0, 0x1C, 0x8C, 0x02,
  0x40, 0x64, 0x90, 0x01,
  0x80, 0x92, 0xE2, 0x00,
  0x00, 0x92, 0x24, 0x00,
  0x00, 0xCC, 0x1C, 0x00,
  0x00, 0x30, 0x03, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

/**static void drawPairingCheck()
{
  display.setColor(WHITE);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 0, "Pairing");
  display.drawLine(43, 12, 85, 12);

  display.setFont(ArialMT_Plain_24);
  if (pairingDemoActive)
  {
    display.drawString(70, 18, "ON");
    display.setFont(ArialMT_Plain_10);
    display.drawString(64, 49, "Searching...");
  }
  else
  {
    display.drawString(70, 18, "OFF");
  }
  display.drawXbm(8, 20, 32, 24, handshakeIcon); 
}**/

static void drawPairingCheck()
{
  display.setColor(WHITE);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);

  display.drawString(64, 0, "ESP-NOW Pairing");
  display.drawLine(25, 12, 103, 12);

  display.drawXbm(8, 22, 32, 24, handshakeIcon);

  display.setFont(ArialMT_Plain_24);

  if (pairingModeActive)
  {
    display.drawString(78, 18, "ON");

    display.setFont(ArialMT_Plain_10);
    display.drawString(78, 43, pairingStatusText);
    display.drawString(64, 54, "Drücken: Stop");
  }
  else
  {
    display.drawString(78, 18, "OFF");

    display.setFont(ArialMT_Plain_10);
    display.drawString(78, 43, pairingStatusText);
    display.drawString(64, 54, "Drücken: Start");
  }
}





//person icon
static const uint8_t personIcon[] PROGMEM = {
  0x00, 0x00,
  0xE0, 0x07,
  0xF0, 0x0F,
  0x18, 0x18,
  0x18, 0x18,
  0x18, 0x18,
  0xF0, 0x0F,
  0xE0, 0x07,
  0x00, 0x00,
  0xE0, 0x07,
  0xF8, 0x1F,
  0x1C, 0x38,
  0x0C, 0x30,
  0x0C, 0x30,
  0xFC, 0x3F,
  0x00, 0x00
};

static void drawPairingNodeFound()
{
  char ersteLinie[9];
  char zweiteLinie[9];
//Ersetzt: deviceMacAdress (eigene) mit pairingMac (Node) 
  strncpy(ersteLinie, pairingMac, 8);
  ersteLinie[8] = '\0';
  strncpy(zweiteLinie, pairingMac + 9, 8);
  zweiteLinie[8] = '\0';

  display.setColor(WHITE);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(76, 0, "Node Found");

  display.drawXbm(16, 23, 16, 16, personIcon);

  display.drawString(78, 17, ersteLinie);
  display.drawString(78, 30, zweiteLinie);
  display.drawString(64, 49, "Pairing...");
}

static void drawPairingSuccess()
{
  char nodeText[24];
  char totalText[20];

  snprintf(nodeText, sizeof(nodeText), "Node %02u hinzugefügt", pairingNodeId);
  snprintf(totalText, sizeof(totalText), "Total: %u", pairingTotalNodes);

  display.setColor(WHITE);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(82, 0, "Erfolg");

  // //icon für success check 
  display.drawCircle(24, 29, 13); //new
  display.drawLine(16, 29, 22, 36); //new
  display.drawLine(22, 36, 34, 20); //new

  display.drawString(84, 21, nodeText);
  display.drawString(84, 42, totalText);

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 52, "<- Back");
}

//Letzte Nachrichten Draw
static void drawLastMessage()
{
  char line[32];

  display.setColor(WHITE);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);

  display.drawString(64, 0, "LETZTE MESSAGE");
  display.drawLine(25, 12, 103, 12);

  display.setTextAlignment(TEXT_ALIGN_LEFT);

  if (!hasLastNodeMessage)
  {
    display.drawString(0, 24, "Keine Node Daten");
    display.drawString(0, 52, "<- Back");
    return;
  }

  snprintf(line, sizeof(line), "From: Node %02u", lastNodeId);
  display.drawString(0, 16, line);

  snprintf(line, sizeof(line), "Temp: %.1f C", lastNodeSensor.temperature);
  display.drawString(0, 28, line);

  snprintf(line, sizeof(line), "Hum: %.1f %%", lastNodeSensor.humidity);
  display.drawString(0, 40, line);

  display.drawString(0, 52, "<- Back");
}

static void drawPairingFailed()
{
  display.setColor(WHITE);
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);

  display.drawString(64, 0, "Pairing Fehlgeschlagen");
  display.drawLine(20, 12, 108, 12);

  display.setFont(ArialMT_Plain_24);
  display.drawString(64, 20, "ERROR");

  display.setFont(ArialMT_Plain_10);
  display.drawString(64, 46, pairingStatusText);

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 54, "<- Back");
}
static void updateDisplay() {
  display.clear();

  switch (currentScreen) {
    case SCREEN_HOME: drawHome(); break;
    case SCREEN_MENU: drawMenu(); break;
    case SCREEN_SENSORS: drawSensors(); break;
    case SCREEN_ERRORS: drawErrors(); break;
    case SCREEN_LORA: drawLora(); break;

    case SCREEN_DEVICE_INFO: drawDeviceInfoMenu(); break;
    case SCREEN_DEVICE_NODE_ID: drawDeviceNodeId(); break;
    case SCREEN_DEVICE_MAC: drawDeviceMac(); break;
    case SCREEN_DEVICE_GPS: drawDeviceGps(); break;
    case SCREEN_DEVICE_TIMESTAMP: drawDeviceTimestamp(); break;

    case SCREEN_MESSAGES: drawMessages(); break;
    case SCREEN_NUM_MSG: drawTotalMsg(); break;
    case SCREEN_LAST_MESSAGE: drawLastMessage(); break;

    case SCREEN_PAIRING_CHECK: drawPairingCheck(); break;
    case SCREEN_PAIRING_NODE_FOUND: drawPairingNodeFound(); break;
    case SCREEN_PAIRING_SUCCESS: drawPairingSuccess(); break;
    case SCREEN_PAIRING_FAILED: drawPairingFailed(); break;
    default: break;
  }
  display.display();
}

// main task loop
void displayTask(void* parameter) {
  display.init();
  readMacAddress();
  display.setFont(ArialMT_Plain_10);

  // Initialer Zeichenvorgang beim Start
  updateDisplay();

  Event_t event;

  for (;;) {
    if (xQueueReceive(displayInbox, &event, portMAX_DELAY) == pdTRUE) {

      switch (event.type) {

        case EVENT_BUTTON_PRESS:
          Serial.printf("[DISPLAY] Button Event empfangen, pressType=%d, currentScreen=%d\n", event.payload.button.pressType, currentScreen);
          // Prüfen auf Single Press (Typ 0)
          //if Single Press -> cursor weiterbewegen
          if (event.payload.button.pressType == 0) {
            if(currentScreen == SCREEN_HOME){
              currentScreen = SCREEN_MENU;
              updateDisplay();
            }
            else if (currentScreen == SCREEN_MENU) {
              selectedMenuItem = (selectedMenuItem + 1) % mainMenuItemCount;
              Serial.printf("[DISPLAY] Menueauswahl gewechselt auf Index: %u\n", selectedMenuItem);
              updateDisplay();
            } else if (currentScreen == SCREEN_DEVICE_INFO) {
              selectedDeviceInfoItem = (selectedDeviceInfoItem + 1) % deviceInfoMenuItemCount;
              Serial.printf("[DISPLAY] Device-Info-Auswahl gewechselt auf Index: %u\n", selectedDeviceInfoItem);
              updateDisplay();
            } else if (currentScreen == SCREEN_MESSAGES) {
              selectedMsgItem = (selectedMsgItem + 1) % messagesMenuItemCount;
              updateDisplay();
             
            /**} else if (currentScreen == SCREEN_PAIRING_CHECK) { //check
            pairingDemoActive = !pairingDemoActive; 
            Serial.printf("[DISPLAY] Pairing-Demo: %s\n", pairingDemoActive ? "ON" : "OFF");
            updateDisplay();
            }**/
            } else if (currentScreen == SCREEN_PAIRING_CHECK) {

              if (pairingModeActive) {
                strcpy(pairingStatusText, "Stopping...");
                sendWifiCommand(WIFI_COMMAND_SET_DATA_MODE);
              } else {
                strcpy(pairingStatusText, "Starting...");
                sendWifiCommand(WIFI_COMMAND_SET_PAIRING_MODE);
              }

              updateDisplay();
            }
            //else{
            //currentScreen = static_cast<Screen_t>((currentScreen + 1) % SCREEN_COUNT);
            //Serial.printf("[DISPLAY] Screen gewechselt auf Index: %d\n", currentScreen);
            //updateDisplay();
            //}

          }
          //double press -> option öffnen
          else if (event.payload.button.pressType == 1) {
            if (currentScreen == SCREEN_MENU) {
              currentScreen = mainMenuItems[selectedMenuItem].targetScreen;
              // Beim Betreten von "Device Info" immer oben anfangen
              if (currentScreen == SCREEN_DEVICE_INFO) {
                selectedDeviceInfoItem = 0;
              }
              updateDisplay();
            } else if (currentScreen == SCREEN_DEVICE_INFO) {
              Serial.printf("[DISPLAY] Device-Info-Eintrag ausgewaehlt: %u\n", selectedDeviceInfoItem);
              currentScreen = deviceInfoMenuItems[selectedDeviceInfoItem].targetScreen;
              updateDisplay();
            } else if (currentScreen == SCREEN_MESSAGES) {
              currentScreen = messagesMenuItems[selectedMsgItem].targetScreen;
              updateDisplay();
            }
          }
          //long press -> zurück
          else if (event.payload.button.pressType == 2) {
            if (currentScreen == SCREEN_DEVICE_NODE_ID || currentScreen == SCREEN_DEVICE_MAC || currentScreen == SCREEN_DEVICE_GPS || currentScreen == SCREEN_DEVICE_TIMESTAMP) {
              currentScreen = SCREEN_DEVICE_INFO;
              updateDisplay();
            } else if (currentScreen == SCREEN_DEVICE_INFO || currentScreen == SCREEN_MESSAGES) {
              currentScreen = SCREEN_MENU;
              updateDisplay();
            } else if
                (currentScreen == SCREEN_NUM_MSG||
                currentScreen == SCREEN_LAST_MESSAGE ||
                currentScreen == SCREEN_LORA) {
              currentScreen = SCREEN_MESSAGES;
              updateDisplay();
            } else if (
              currentScreen == SCREEN_PAIRING_CHECK || 
              currentScreen == SCREEN_PAIRING_NODE_FOUND ||
              currentScreen == SCREEN_PAIRING_SUCCESS ||
              currentScreen == SCREEN_PAIRING_FAILED)
            {
              //paringModeActive = false; 
              currentScreen = SCREEN_MESSAGES;
              updateDisplay();
            } else if (currentScreen != SCREEN_MENU) {
              currentScreen = SCREEN_MENU;
              updateDisplay();
            } 
            else if (currentScreen == SCREEN_MENU){
              currentScreen = SCREEN_HOME;
              updateDisplay();
            }
          }
          break;

        case EVENT_SENSOR_DATA:
          latestSensor = event.payload.sensor;
          Serial.printf("[DISPLAY] Showed Sensor Values \n");
          // Erhöhe Warning Counter
          if (latestSensor.wet && !previousWetState) {
            warningCount++;
          }

          if (latestSensor.vibration && !previousVibrationState) {
            warningCount++;
          }

          previousWetState = latestSensor.wet;
          previousVibrationState = latestSensor.vibration;

          if (latestSensor.wet) {
            strcpy(sensorStatusText, "Wet");
          } else if (latestSensor.vibration) {
            strcpy(sensorStatusText, "Vibration");
          } else {
            strcpy(sensorStatusText, "OK");
          }
          // wirklich nur rendern, wenn der Screen gerade aktiv sein soll
          if (currentScreen == SCREEN_SENSORS) {
            updateDisplay();
          }
          break;

        case EVENT_NETWORKPACKET:
        {
          const NetworkPacket& packet = event.payload.packet;
          pairingTotalNodes = packet.count;

          receivedMsgCount = packet.count;

          // if (packet.count > 0) {
          //   const NodePacket& last = packet.nodes[packet.count - 1];
          //   hasLastNodeMessage = true;
          //   lastNodeId          = last.nodeId;
          //   lastNodeSensor       = last.sensor;
          //   lastMsgHours = lastNodeSensor.hours;
          //   lastMsgMinutes = lastNodeSensor.minutes;
          //   lastMsgSeconds = lastNodeSensor.seconds;
          //   }
          
          if (
            currentScreen == SCREEN_HOME ||
            currentScreen == SCREEN_NUM_MSG) {
            updateDisplay();
            }
          break;
        }

        case EVENT_IMPORTANT_DATA:
        {
          hasLastNodeMessage = true;
          lastNodeId = event.payload.nodepacket.nodeId;
          lastNodeSensor = event.payload.nodepacket.sensor;
          lastMsgHours = lastNodeSensor.hours;
          lastMsgMinutes = lastNodeSensor.minutes;
          lastMsgSeconds = lastNodeSensor.seconds;
          warningCount++;
          receivedMsgCount++;
          if (
            currentScreen == SCREEN_HOME ||
            currentScreen == SCREEN_NUM_MSG ||
            currentScreen == SCREEN_LAST_MESSAGE
           ) {
            updateDisplay();
            }

          break;
        }

        case EVENT_LORA_DATA:
          for (uint8_t i = 1; i < 5; i++) {
            snprintf(loraLog[i - 1], sizeof(loraLog[0]), "%s", loraLog[i]);
          }
          switch (event.payload.loraTraffic.kind) {
            case LORA_TRAFFIC_TTN_TX:        snprintf(loraLog[4], sizeof(loraLog[0]), "TTN TX"); break;
            case LORA_TRAFFIC_TTN_ACK:       snprintf(loraLog[4], sizeof(loraLog[0]), "TTN ACK"); break;
            case LORA_TRAFFIC_RELREQ_TX:     snprintf(loraLog[4], sizeof(loraLog[0]), "P2P RelReq TX"); break;
            case LORA_TRAFFIC_RELREQ_RX:     snprintf(loraLog[4], sizeof(loraLog[0]), "P2P RelReq RX N%u", event.payload.loraTraffic.nodeId); break;
            case LORA_TRAFFIC_RELREQ_ACK_TX: snprintf(loraLog[4], sizeof(loraLog[0]), "P2P Rel ACK TX N%u", event.payload.loraTraffic.nodeId); break;
            case LORA_TRAFFIC_RELREQ_ACK_RX: snprintf(loraLog[4], sizeof(loraLog[0]), "P2P Rel ACK RX"); break;
            case LORA_TRAFFIC_TTN_REL_TX:    snprintf(loraLog[4], sizeof(loraLog[0]), "TTN Rel TX"); break;
            default:                         snprintf(loraLog[4], sizeof(loraLog[0]), "LoRa ?"); break;
          }

          Serial.printf("[DISPLAY] LoRa Übertragung anzeigen: %s\n", loraLog[4]);

          if (currentScreen == SCREEN_LORA) {
            updateDisplay();
          }
          break;

        case EVENT_ERROR:
          snprintf(latestError.message, sizeof(latestError.message), "%s", event.payload.error.message);
          latestError.hasError = true;

          Serial.printf("[DISPLAY] Fehler anzeigen: %s\n", latestError.message);

          if (currentScreen == SCREEN_ERRORS) {
            updateDisplay();
          }
          break;

        case EVENT_WIFI_STATUS:
        {
          WifiStatusPayload_t status = event.payload.wifiStatus;

          switch (status.status)
          {
            case WIFI_STATUS_DATA_MODE:
              pairingModeActive = false;
              strcpy(pairingStatusText, "OFF");
              break;

            case WIFI_STATUS_PAIRING_MODE:
              pairingModeActive = true;
              strcpy(pairingStatusText, "Suche...");
              break;

            case WIFI_STATUS_NODE_FOUND:
              pairingModeActive = true;

              strncpy(pairingMac, status.mac, sizeof(pairingMac) - 1);
              pairingMac[sizeof(pairingMac) - 1] = '\0';

              pairingNodeId = status.nodeId;
              pairingTotalNodes = status.totalNodes;

              currentScreen = SCREEN_PAIRING_NODE_FOUND;
              break;

            case WIFI_STATUS_PAIRING_SUCCESS:
              pairingModeActive = false;

              strncpy(pairingMac, status.mac, sizeof(pairingMac) - 1);
              pairingMac[sizeof(pairingMac) - 1] = '\0';

              pairingNodeId = status.nodeId;
              pairingTotalNodes = status.totalNodes;

              currentScreen = SCREEN_PAIRING_SUCCESS;
              break;

          case WIFI_STATUS_ERROR:
            pairingModeActive = false;
            strcpy(espNowStatusText, "Error");

            strncpy(pairingStatusText, status.message, sizeof(pairingStatusText) - 1);
            pairingStatusText[sizeof(pairingStatusText) - 1] = '\0';

            currentScreen = SCREEN_PAIRING_FAILED;
            break;

            default:
              break;
          }
          updateDisplay();
          break;
        }
        case EVENT_NODEPACKET:
          {
            receivedMsgCount++;

            hasLastNodeMessage = true;
            lastNodeId = event.payload.nodepacket.nodeId;
            lastNodeSensor = event.payload.nodepacket.sensor;

            if (lastNodeSensor.wet || lastNodeSensor.vibration) {
              warningCount++;
            }

            strcpy(sensorStatusText, "OK");

            if (lastNodeSensor.wet) {
              strcpy(sensorStatusText, "Wet");
            }

            if (lastNodeSensor.vibration) {
              strcpy(sensorStatusText, "Vibration");
            }

            if (
              currentScreen == SCREEN_HOME ||
              currentScreen == SCREEN_NUM_MSG ||
              currentScreen == SCREEN_LAST_MESSAGE ||
              currentScreen == SCREEN_ERRORS
            ) {
              updateDisplay();
            }

            break;
          }

        
        case EVENT_BLE_PACKET_RECEIVED:
        {
          #if DEVICE_MODE == DEVICE_DIKERUNNER
                    strncpy(
                      runnerBleHubIdsText,
                      event.payload.hubPacketIds.message,
                      sizeof(runnerBleHubIdsText) - 1
                    );

                    runnerBleHubIdsText[sizeof(runnerBleHubIdsText) - 1] = '\0';

                    if (currentScreen == SCREEN_HOME) {
                      updateDisplay();
                    }
          #endif
          break;
        }
        

        default:
          break;
      }
    }
  }
}