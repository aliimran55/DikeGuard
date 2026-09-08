#include <Arduino.h>
#include <NimBLEDevice.h>
#include <HT_SSD1306Wire.h>
#include <vector>
#include <mbedtls/ccm.h>
#include <esp_random.h>
#include <Preferences.h>
#include "bleConfig.h"
#include "events.h"
#include <LittleFS.h>
#include "secrets.h"

// global AES key (get from the NVM, defined in secrets.h, which is not uploaded to git for security reasons)
uint8_t AES_KEY[16];

// avoiding duplicate def
extern SSD1306Wire display;

// global variables we need (mainly) in the init functions
NimBLEExtAdvertising*  AdvertisingPointer = nullptr; // create the extended advertiser
NimBLEExtAdvertisement BLEData;                      // create the object where the data is placed in
NimBLEScan*            pScanner = nullptr;           // create the scanner
uint32_t wakeup_counters[12]    = {}; // keeps track of different counters of the Deichlaufer (up to 12, based on the ID that is send in the wake up)
uint32_t wakeUpCounter          = 1;  // per-runner counter stored separately
// Container for decrypted hub packets (runner mode)
std::vector<HubPacket> decryptedPackets;
NimBLEScanResults      results;
Preferences            prefs;     // needed to save things in non volantile storage
HubPacket              pkt;       // save the data in a global variable that constandly updates
Event_t                lastevent; // saves the last event and all its data
// (SensorReading, HubPacket and WakeUpPacket are defined in bleConfig.h)
char savedHubIds[128] = "Received Hub IDs: ";

// ------------Used by runner and hub------------
//  Hardware - init Display Helpfunction
// void turnVextON() {
//   pinMode(Vext, OUTPUT);   // activates display, telling it to be the output
//   digitalWrite(Vext, LOW); // turns display on

//   // the part needed to reset the display
//   pinMode(RST_OLED, OUTPUT);
//   digitalWrite(RST_OLED, LOW);
//   delay(20);
//   digitalWrite(RST_OLED, HIGH);

//   // wait a bit to stabilise
//   delay(100);
// }

// ------------Used by hub------------
//  Software - BLE Setup (Advertiser and Scanner) Helpfunction
void InitBLE() {
  NimBLEDevice::init("ESP32 Hub"); // init the NimBLE device and give it a name

  // Scanner
  pScanner = NimBLEDevice::getScan();          // point the ScannerPointer to the Scanner object
  pScanner->setPhy(NimBLEScan::Phy::SCAN_ALL); // configure the scanner to scan for every nearby packet
}

// ------------Used by hub------------
// Software - Create a Dummy Packet Helpfunction
HubPacket createDummyPacket(uint8_t dl_ID) {

  HubPacket pkt;
  pkt.hub_id     = 2;
  pkt.dl_counter = wakeup_counters[dl_ID];
  pkt.timestamp  = 1530000000; // UNIX time stamp
  // defining dummy sensor data
  for (int i = 0; i < 10; i++) {
    pkt.readings[i].temperature = 2000 + i * 10;
    pkt.readings[i].humidity    = 5000 + i * 10;
    pkt.readings[i].battery     = 37 + i;
    pkt.readings[i].moisture    = 1000 + i * 10;
    pkt.readings[i].latitude    = i + 1;
    pkt.readings[i].longitude   = i * 3;
  }
  return pkt;
}

// ------------Used by hub------------
//  creating the hub packet with real data from event
void createHubPacket(uint8_t dl_ID, Event_t event) {

  // create the Hub Packet Info
  pkt.hub_id = HUB_ID;

  pkt.dl_counter = wakeup_counters[dl_ID];
  pkt.timestamp  = event.timestamp; // the packet has the timestamp of the latest sensor data gotten
  Serial.println("[BLE] Finalizing Packet.");
}

void prepareHubPacket(Event_t event) {
  // get the event coordinates to later compare
  float currlatitude  = event.payload.sensor.latitude;
  float currlongitude = event.payload.sensor.longitude;

  // loop through all the readings (should be 10)
  for (int j = 0; j < MAX_READINGS; j++) {
    if ((pkt.readings[j].latitude == currlatitude) && (pkt.readings[j].longitude == currlongitude)) {
      Serial.printf("[BLE] Updating array at spot %d \n", j);
      // defining sensor data
      pkt.readings[j].temperature = event.payload.sensor.temperature;
      pkt.readings[j].humidity    = event.payload.sensor.humidity;
      pkt.readings[j].battery     = event.payload.sensor.batVolts;
      pkt.readings[j].moisture    = event.payload.sensor.moistureRaw;
      pkt.readings[j].latitude    = event.payload.sensor.latitude;
      pkt.readings[j].longitude   = event.payload.sensor.longitude;
      Serial.println("[BLE] Data updated.");
      break;
      // assuming there is no actual sensor at 0.0 | 0.0, this means this is the first empty array slot
    } else if ((pkt.readings[j].latitude == 0.0) && (pkt.readings[j].longitude == 0.0)) {
      Serial.printf("[BLE] Writing new sensor data at %d \n", j);
      pkt.readings[j].temperature = event.payload.sensor.temperature;
      pkt.readings[j].humidity    = event.payload.sensor.humidity;
      pkt.readings[j].battery     = event.payload.sensor.batVolts;
      pkt.readings[j].moisture    = event.payload.sensor.moistureRaw;
      pkt.readings[j].latitude    = event.payload.sensor.latitude;
      pkt.readings[j].longitude   = event.payload.sensor.longitude;
      Serial.println("[BLE] New data written.");
      break;
    }
  }
}

// ------------Used by Hub and runner------------
//  Software - do AES CCM encryption Helpfunction
bool encryptAES_CCM(uint8_t* input, uint16_t len, uint8_t* output, uint8_t* nonce_out, uint16_t* output_len) {

  // AES CCM needs some things bevor we can actually encrypt
  esp_fill_random(nonce_out, CCM_IV_LEN); // nonce is a random generated 12 bytes number so that the same plaintext doesn't produce the same ciphertext twice

  mbedtls_ccm_context ccm; // create context for CCM
  mbedtls_ccm_init(&ccm);  // init CCM with the context

  int res = mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128); // binds the key to the context (context, "use AES", AES key, key length)
  if (res != 0) {                                                          // checking if it was successful
    Serial.println("[BLE] CCM setkey failed");
    mbedtls_ccm_free(&ccm);
    return false;
  }

  res = mbedtls_ccm_encrypt_and_tag(&ccm,         // the context with the key
                                    len,          // how many bytes to encrypt
                                    nonce_out,    // random nonce
                                    CCM_IV_LEN,   // nonce length
                                    NULL, 0,      // no additional data (additional data is data that not encrypted also checked on integrity)
                                    input,        // plaintext
                                    output,       // encrypted output
                                    output + len, // tag written after encrypted data (the doc says to use output + len)
                                    CCM_TAG_LEN   // tag length
  );

  mbedtls_ccm_free(&ccm); // free up the memory

  if (res != 0) { // check encrypt result
    Serial.println("[BLE] CCM encrypt failed");
    return false;
  }

  *output_len = len + CCM_TAG_LEN; // tell the caller how many bytes were produced
  return true;
}

// ------------Used by runner------------
void sendEncryptedWakeUp(uint8_t dl_ID, uint32_t counter) {
  WakeUpPacket pkt;
  pkt.dl_ID   = RUNNER_ID;
  pkt.counter = wakeUpCounter;

  uint8_t buffer[sizeof(WakeUpPacket)];
  memset(buffer, 0, sizeof(buffer));
  buffer[0]          = pkt.dl_ID;
  size_t counter_off = offsetof(WakeUpPacket, counter);
  memcpy(buffer + counter_off, &pkt.counter, sizeof(pkt.counter));

  uint8_t  nonce[CCM_IV_LEN];
  uint8_t  encrypted[sizeof(WakeUpPacket) + CCM_TAG_LEN];
  uint16_t encrypted_len = 0;

  if (!encryptAES_CCM(buffer, sizeof(WakeUpPacket), encrypted, nonce, &encrypted_len)) {
    Serial.println("[BLE] WakeUp encryption failed");
    return;
  }

  // build final packet: [ nonce | encrypted data + tag ]
  uint8_t final_packet[CCM_IV_LEN + sizeof(WakeUpPacket) + CCM_TAG_LEN];
  memcpy(final_packet, nonce, CCM_IV_LEN);
  memcpy(final_packet + CCM_IV_LEN, encrypted, encrypted_len);
  uint16_t final_len = CCM_IV_LEN + encrypted_len;

  AdvertisingPointer = NimBLEDevice::getAdvertising();

  AdvertisingPointer->stop();
  delay(10);
  AdvertisingPointer->removeInstance(0);

  NimBLEExtAdvertisement data;
  data.setName("WakeUp");
  data.setManufacturerData((const uint8_t*)final_packet, sizeof(final_packet));
  AdvertisingPointer->setInstanceData(0, data);
  delay(20); // give NimBLE a moment to apply the new instance data
  AdvertisingPointer->start(0);
  // display.clear();
  // display.drawString(0, 0, "Waking up hubs");
  // display.display();
  delay(RUNNER_SEND_TIME);
  AdvertisingPointer->stop();

  // display.clear();
}

// ------------Used by hub------------
//  Software - decryption Helpfunction
bool decryptWakeUpAES_CCM(uint8_t* input, uint16_t len, uint8_t* output, uint8_t* nonce, uint16_t output_len) {
  // Create and initialize the mbedTLS CCM context
  mbedtls_ccm_context ccm;
  mbedtls_ccm_init(&ccm);

  // Set the AES key for CCM mode (128-bit key)
  // If key setup fails, clean up and return false
  if (mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128) != 0) {
    mbedtls_ccm_free(&ccm);
    return false;
  }

  int res = mbedtls_ccm_auth_decrypt(&ccm,       // the context with the key
                                     output_len, // how many bytes
                                     nonce,      // nonce
                                     CCM_IV_LEN, // nonce lenght
                                     NULL, 0,    // no additional authenticated data (additional data is data that not encrypted also checked on integrity)
                                     input,      // ciphertext input
                                     output,     // decrypted plaintext output
                                     input + output_len, // authentication tag appended after ciphertext
                                     CCM_TAG_LEN         // tag length
  );

  // free up memory
  mbedtls_ccm_free(&ccm);

  return (res == 0); // decryption was successful if ress == 0
}

// ------------Used by hub------------
// Software - check if the wake up message was valid Helpfunction
bool isValidWakeUp(WakeUpPacket& pkt) {
  // Replay-protection
  if (pkt.counter <= wakeup_counters[pkt.dl_ID]) {
    Serial.println("[BLE] Replay or duplicate Packet detected!");
    return false;
  }

  wakeup_counters[pkt.dl_ID] = pkt.counter;
  prefs.begin("hubData", false);
  prefs.putBytes("counters", wakeup_counters, sizeof(wakeup_counters));
  prefs.end();
  return true;
}

// ------------Used by hub------------
// Software - Data Sending Helpfunction
void SendSensorData(uint8_t dl_ID) {
  Serial.println("[BLE] Sending sensor data.");

  createHubPacket(dl_ID, lastevent); // finalize a Hub Packet with the dl ID, HUB ID and timestamp
  HubPacket hubdata = pkt;           // the entire up-to-date packet should now be stored in pkt

  // convert the struct into raw Bytes (needed for setManufaturerData and AES CCM)
  uint8_t  buffer[sizeof(HubPacket)]; // make array same size as struct
  uint16_t len = sizeof(HubPacket);   // check how many bytes that was
  memcpy(buffer, &hubdata, len);      // copy the struct's bytes into the array

  // encrypt. first we create the output variables, then we use the encrypt function
  uint8_t  nonce[CCM_IV_LEN];
  uint8_t  encrypted[sizeof(HubPacket) + CCM_TAG_LEN];
  uint16_t encrypted_len = 0;

  if (!encryptAES_CCM(buffer, len, encrypted, nonce, &encrypted_len)) {
    Serial.println("[BLE] Encryption failed, aborting send.");
    return;
  }

  // build final packet: [ nonce | encrypted data + tag ]
  uint8_t final_packet[CCM_IV_LEN + sizeof(HubPacket) + CCM_TAG_LEN]; // creates an array big enough to hold the data
  memcpy(final_packet, nonce, CCM_IV_LEN);                            // copy the nonce in
  memcpy(final_packet + CCM_IV_LEN, encrypted, encrypted_len);        // copy the encrypted data in after nonce
  uint16_t final_len = CCM_IV_LEN + encrypted_len;                    // length of the final packet

  // Advertiser
  AdvertisingPointer = NimBLEDevice::getAdvertising();                  // point the AdvertisingPointer to the Advertising object
  BLEData.setName("Sensor Data");                                       // set the name for the packet to send
  BLEData.setManufacturerData((const uint8_t*)final_packet, final_len); // set the message
  AdvertisingPointer->setInstanceData(0, BLEData);
  delay(20); // stabilizing

  AdvertisingPointer->start(0);
  delay(HUB_SEND_TIME);
  AdvertisingPointer->stop();

  // display.clear(); // clear any remaining text
  // display.drawString(0, 0, "Sending");
  // display.drawString(0, 16, "complete.");
  // display.display();
}

// ------------Used by runner------------
//  Software to decrypt data received by hub
bool decryptAES_CCM(uint8_t* input, uint16_t len, uint8_t* output, uint8_t* nonce, uint16_t output_len) {
  // Create and initialize the mbedTLS CCM context
  mbedtls_ccm_context ccm;
  mbedtls_ccm_init(&ccm);

  // Set the AES key for CCM mode (128-bit key)
  // If key setup fails, clean up and return false
  if (mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, AES_KEY, 128) != 0) {
    mbedtls_ccm_free(&ccm);
    return false;
  }

  int res = mbedtls_ccm_auth_decrypt(&ccm,       // the context with the key
                                     output_len, // how many bytes
                                     nonce,      // nonce
                                     CCM_IV_LEN, // nonce lenght
                                     NULL, 0,    // no additional authenticated data (additional data is data that not encrypted also checked on integrity)
                                     input,      // ciphertext input
                                     output,     // decrypted plaintext output
                                     input + output_len, // authentication tag appended after ciphertext
                                     CCM_TAG_LEN         // tag length
  );

  // free up memory
  mbedtls_ccm_free(&ccm);

  return (res == 0); // decryption was successful if ress == 0
}

// ------------Used by hub------------
//  load the wakeup_counters from non volatile storage, so that they are not reset on every reboot or crash (which would make the replay protection useless)
void loadCounters() {
  prefs.begin("hubData", false);
  if (prefs.isKey("counters")) {
    prefs.getBytes("counters", wakeup_counters, sizeof(wakeup_counters));
    Serial.println("[BLE] Loaded old/last wakeup_counters");
  }
  prefs.end();
}

// ------------Used by hub------------
//  Software - Scan & Respond Helperfunction
void ScanAndRespond() {
  // display.clear(); // clear any remaining text
  // display.drawString(0, 0, "Scanning...");
  // display.display();

  // creates BLE scanner object
  pScanner = NimBLEDevice::getScan(); // create a scanner

  // before the scan, we need to enable extended scan
  pScanner->setPhy(NimBLEScan::Phy::SCAN_ALL); // SCAN_ALL is for scanning every advertising

  pScanner->clearResults(); // clear the results

  // activate the scanner, and scan for HUB_SCAN_TIME seconds. the program STOPS until the time is over
  results = pScanner->getResults(HUB_SCAN_TIME);

  for (int i = 0; i < results.getCount(); i++) {                 // loop through the results for each
    const NimBLEAdvertisedDevice* device = results.getDevice(i); // get the device of the result
    if (device->getName() == "WakeUp") {
      Serial.println("[BLE] Wakeup found");
      std::string data = device->getManufacturerData();
      uint8_t     nonce[CCM_IV_LEN];
      memcpy(nonce, data.data(), CCM_IV_LEN); // copies the nonce (from data.data()(=pointer to the first byte), next CCM_IV_LEN-Bytes )

      uint8_t* encrypted = (uint8_t*)data.data() + CCM_IV_LEN; // encrypted data behind the nonce
      uint16_t enc_len   = data.size() - CCM_IV_LEN;
      uint16_t plain_len = sizeof(WakeUpPacket);
      uint8_t  decrypted[sizeof(WakeUpPacket)]; // will hold the decrypted bytes

      if (!decryptWakeUpAES_CCM(encrypted, enc_len, decrypted, nonce,
                                plain_len)) // if decryption fails it continues (=skips rest of this iteration ad jumps to the next iteration in the for loop)
        continue;

      WakeUpPacket pkt;
      memcpy(&pkt, decrypted, sizeof(WakeUpPacket));

      if (!isValidWakeUp(pkt)) // if not valid it continues (=skips rest of this iteration and jumps to the next iteration in the for loop)
        continue;

      Serial.println("[BLE] Wakeup was valid");
      // display.clear(); // clear any remaining text
      // display.drawString(0, 0, "Sending");
      // display.drawString(0, 16, "Data...");
      // display.display();

      uint8_t dl_ID = pkt.dl_ID;

      SendSensorData(dl_ID); // send the data
      break;                 // one match is enough
    } else {
    }
  }
}

// ------------Used by runner------------
// only saves message in decryptedPackets, if not already in it
bool pushbackNew(HubPacket pkt) {
  for (const auto& existing : decryptedPackets) {
    if (existing.hub_id == pkt.hub_id) {
      Serial.println("[BLE] Duplicate packet ignored");
      return false;
    }
  }
  decryptedPackets.push_back(pkt); // saves the decrypted packet
  Serial.printf("[BLE] Decrypted Packet from hub_id=%d was saved \n [BLE]----------TOTAL NUMBER OF PACKETS: %d\n", pkt.hub_id, decryptedPackets.size());
  Event_t event;
  event.type = EVENT_BLE_PACKET_RECEIVED;
  char anhang[8];
  snprintf(anhang, sizeof(anhang), "%d,", pkt.hub_id);
  strcat(savedHubIds, anhang);
  strcpy(event.payload.hubPacketIds.message, savedHubIds);
  xQueueSend(displayInbox, &event, 0);
  Serial.printf("%s\n", savedHubIds);

  File file = LittleFS.open("/hubpackets.bin", FILE_APPEND);
  if (!file) {
    Serial.println("[BLE] Could not open packet log");
  }
  file.write((const uint8_t*)&pkt, sizeof(HubPacket));
  file.close();
  return true;
}

// ------------Used by runner------------
// scans for data from a hub, decrypts it and saves it if it is new
void scanForAdvertising() {
  // display.clear();
  // display.drawString(0,0, "Actively scanning...");
  // display.display();

  // creates BLE scanner object
  NimBLEScan* pScanner = NimBLEDevice::getScan(); // create a scanner

  // before the scan, we need to enable extended scan
  pScanner->setPhy(NimBLEScan::Phy::SCAN_ALL); // SCAN_ALL is for scanning every advertising

  // activate the scanner, and scan for RUNNER_SCAN_TIME milliseconds. the program STOPS until the time is over
  results = pScanner->getResults(RUNNER_SCAN_TIME);

  // display.clear();
  // display.drawString(0,0, "Checking results...");
  // display.display();
  // delay(500);
  // display.clear();

  for (int i = 0; i < results.getCount(); i++) {                 // loop through the results
    const NimBLEAdvertisedDevice* device = results.getDevice(i); // get one device
    // Serial.println(device->toString().c_str());  //for debugging

    // only saves messages from the sender, must match sender's packet name and UUID
    if (device->getName() == "Sensor Data") {
      std::string data = device->getManufacturerData(); // get the message from the manufacturer Data field

      if (data.size() >= (CCM_IV_LEN + CCM_TAG_LEN)) // check if the whole massage arrived
      {
        uint8_t nonce[CCM_IV_LEN];
        memcpy(nonce, data.data(), CCM_IV_LEN); // copies the nonce (from data.data()(=pointer to the first byte), next CCM_IV_LEN-Bytes )

        uint8_t* encrypted = (uint8_t*)data.data() + CCM_IV_LEN; // encrypted data behind the nonce
        uint16_t enc_len   = data.size() - CCM_IV_LEN;
        uint16_t plain_len = sizeof(HubPacket);
        uint8_t  decrypted[sizeof(HubPacket)]; // will hold the decrypted bytes

        if (decryptAES_CCM(encrypted, enc_len, decrypted, nonce, plain_len) == false) {
          Serial.println("[BLE] Decrypt failed");
          continue; // if the encryption fails, we should jump to the next received message/result
        }

        HubPacket pkt;
        memcpy(&pkt, decrypted, sizeof(HubPacket));
        pushbackNew(pkt); // only saves if it is not already saved
      }
    }
  }
}

// ------------Used by runner------------
// displays the saved messages
void displayResults() {
  // now displaying the messages received
  if (decryptedPackets.empty()) { // if there were no messages
    Serial.println("[BLE] No messages found.");
    // display.clear();
    // display.drawString(0, 0, "No messages");
    // display.drawString(0, 16, "found.");
    // display.display();
    delay(300);
    // display.clear();
  } else {
    for (int i = 0; i < decryptedPackets.size(); i++) { // go trough the messages one by one
      HubPacket& hPkt = decryptedPackets[i];
      // print message number from total messages and the message itself
      // Serial.printf("[BLE] Message %d / %d \n", i + 1, decryptedPackets.size());
      // Serial.printf("[BLE]   - Hub=%d ReadingCount=%d Counter=%d Time=%lu\n", hPkt.hub_id, hPkt.packet_id, hPkt.dl_counter, hPkt.timestamp);

      // show message number on display
      //  display.clear();
      // display.drawString(0, 0, "Msg " + String(i + 1) + "/" + String(decryptedPackets.size()) + ":");

      // drawString can only show ca. 12 chars per line at 16px font,
      // so split the message across two lines if nessasary
      // display.drawString(0, 16, "from Hub " + String(hPkt.hub_id)); // draw the hub ID
      // display.display();
      delay(MESSAGE_DELAY); // show each message for Message_Delay miliseconds
    }
  }
}

// ------------Used by runner------------
// initialization of the runner
void runnerInit() {
  Serial.println("[BLE] BLE RUNNER Init");
  VextON();
  // display.init();
  // display.setFont(ArialMT_Plain_16);
  NimBLEDevice::init("ESP32 Deichlaeufer");
  AdvertisingPointer = NimBLEDevice::getAdvertising();
  NimBLEExtAdvertisement BLEDataLocal;
  AdvertisingPointer->setInstanceData(0, BLEDataLocal);
  prefs.begin("runnerData", false);             // create Namespace
  wakeUpCounter = prefs.getULong("counter", 1); // get counter (or 1 if counter does not exist)
  prefs.end();
  if (!LittleFS.begin(true)) {
    Serial.println("[BLE] loading LittleFS / log failed");
  }

  Event_t event;
  event.type = EVENT_BLE_PACKET_RECEIVED;
  strcpy(event.payload.hubPacketIds.message, savedHubIds);
  xQueueSend(mainEventQueue, &event, 0);
  Serial.printf("%s\n", savedHubIds);
}

// ------------Used by runner------------
//  Main loop for the runner
void runnerLoop() {
  sendEncryptedWakeUp(RUNNER_ID, wakeUpCounter);
  wakeUpCounter++;
  prefs.begin("runnerData", false);
  prefs.putULong("counter", wakeUpCounter); // save in non volantile storage
  prefs.end();
  Serial.printf("[BLE] Saved the wakeupcounter: %lu\n", wakeUpCounter);

  scanForAdvertising();

  displayResults();
}

// Task wrapper exposed to main
void bleTask(void* parameter) {
  Serial.println("[BLE] BLE Task is starting");

  prefs.begin("secrets", false);
  if (prefs.getBytes("aes_key", AES_KEY, sizeof(AES_KEY)) == 0) // checking if there is an aes key in the NVM
  {
    Serial.println("[BLE] No AES key found in NVM. Getting from secrets.h and saving to NVM.");
    prefs.putBytes("aes_key", SymmetricKey, sizeof(AES_KEY)); // saving the key in the NVM for later use
    memcpy(AES_KEY, SymmetricKey, sizeof(AES_KEY));
  } else {
    Serial.println("[BLE] AES key already exists in NVM. Using it.");
  }
  prefs.end();

#if DEVICE_MODE == DEVICE_DIKERUNNER
  runnerInit();
  for (;;) {
    runnerLoop();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
#elif DEVICE_MODE == DEVICE_HUB
  //turnVextON();
  // display.init();
  // display.setFont(ArialMT_Plain_16);
  InitBLE();
  loadCounters();
  Event_t event;
  for (;;) {
    if (xQueueReceive(bleInbox, &event, pdMS_TO_TICKS(20)) == pdTRUE) {
      if (event.type == EVENT_SENSOR_DATA) {
        if (DEVICE_MODE == DEVICE_HUB) {
          Serial.printf("[BLE]: Hub hat Sensordaten bekommen, Packet wird vorbereitet.\n");
          prepareHubPacket(event); // update the data, save it to pkt
        } else {
          // do something?
        }
      }
    }
    ScanAndRespond();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
#endif
}
