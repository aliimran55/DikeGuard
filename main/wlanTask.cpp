#include "events.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include <Arduino.h>
#include "loraConfig.h"
#include "WlanBackend.h"

static const uint32_t PAIR_MESSAGE     = 0x12121212;
static const uint32_t DATA_MESSAGE     = 0xDADADADA;
static const uint32_t PAIR_ACK_MESSAGE = 0xACACACAC;
static const uint32_t DATA_ACK_MESSAGE = 0xCDCDCDCD;

static const uint8_t WIFI_CHANNEL = 1;

static const uint8_t networkPMK[16] = {
    'E','S','P','N',
    'E','T','0','1',
    '2','3','4','5',
    '6','7','8','9'
};

static const uint8_t broadcastAddress[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

typedef struct {
    uint32_t message;             
} PairRequest;

typedef struct {
    uint32_t message;       
    uint8_t  lmk[16];              
} PairResponse;

typedef struct {
    uint32_t message;              
} PairAck;

typedef struct {
    uint32_t message;         
} DataAck;

static String macToString(const uint8_t *mac) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

//Display report helper
static void copyTextSafe(char *dest, size_t destSize, const char *src)
{
    if (!dest || destSize == 0) return;

    if (!src) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
}

static void sendWifiStatus(
    WifiStatusKind_e status,
    const uint8_t *mac = nullptr,
    const char *message = "",
    uint8_t nodeId = 0,
    uint8_t totalNodes = 0
)
{
    Event_t ev = {};
    ev.type = EVENT_WIFI_STATUS;
    ev.timestamp = millis();

    ev.payload.wifiStatus.status = status;
    ev.payload.wifiStatus.nodeId = nodeId;
    ev.payload.wifiStatus.totalNodes = totalNodes;

    if (mac) {
        snprintf(ev.payload.wifiStatus.mac,
                 sizeof(ev.payload.wifiStatus.mac),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5]);
    } else {
        copyTextSafe(ev.payload.wifiStatus.mac,
                     sizeof(ev.payload.wifiStatus.mac),
                     "--:--:--:--:--:--");
    }

    copyTextSafe(ev.payload.wifiStatus.message,
                 sizeof(ev.payload.wifiStatus.message),
                 message);

    if (mainEventQueue) {
        xQueueSend(mainEventQueue, &ev, pdMS_TO_TICKS(10));
    }
}

/////////////////////////////////////////////////////////////////////////// HUB LOGIC

#if DEVICE_MODE == DEVICE_HUB

typedef enum {
    HUB_MODE_DATA,
    HUB_MODE_PAIRING
} HubMode_t;
static HubMode_t hubMode = HUB_MODE_DATA;

typedef struct {
    uint8_t mac[6];
    uint8_t lmk[16];
    bool active;
} Node;
static Node nodes[MAX_NODES];

// Node counter
static uint8_t countActiveNodes()
{
    uint8_t count = 0;

    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].active) {
            count++;
        }
    }

    return count;
}

static bool waitingForAck = false;
static uint8_t pendingMac[6];

static uint32_t pairAckTimestamp = 0;
static const uint32_t PAIR_ACK_TIMEOUT_MS = 5000;

static void setHubMode(HubMode_t mode);

static bool isKnownNode(const uint8_t *mac) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].active && memcmp(nodes[i].mac, mac, 6) == 0)
            return true;
    }
    return false;
}

static bool getNodeLMK(const uint8_t *mac, uint8_t *lmkOut) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (nodes[i].active && memcmp(nodes[i].mac, mac, 6) == 0) {
            memcpy(lmkOut, nodes[i].lmk, 16);
            return true;
        }
    }
    return false;
}

static bool addPairedNode(const uint8_t *mac, const uint8_t *lmk) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (!nodes[i].active) {
            memcpy(nodes[i].mac, mac, 6);
            memcpy(nodes[i].lmk, lmk, 16);
            nodes[i].active = true;
            Serial.print("Paired node: ");
            Serial.println(macToString(mac));
            return true;
        }
    }
    Serial.println("[WIFI] Node list full");

    sendWifiStatus(
        WIFI_STATUS_ERROR,
        mac,
        "Node list full",
        0,
        countActiveNodes()
    );

    return false;
}

static void OnDataRecv(const esp_now_recv_info_t *info,
                       const uint8_t *data,
                       int len) {
    if (len != sizeof(SensorMessage)) {
        //Serial.println("[WIFI] Invalid packet size (data)");
        return;
    }

    SensorMessage incoming;
    memcpy(&incoming, data, sizeof(incoming));

    if (incoming.message != DATA_MESSAGE) {
        Serial.println("[WIFI] Unknown message type");
        return;
    }

    if (!isKnownNode(info->src_addr)) {
        Serial.println("[WIFI] Node not known");
        return;
    }

    Serial.print("[WIFI] NEW Data from: ");
    Serial.println(macToString(info->src_addr));
    Serial.print("  Temp: "); Serial.println(incoming.sensor.temperature);
    Serial.print("  Hum: ");  Serial.println(incoming.sensor.humidity);
    
    sendWifiStatus(
        WIFI_STATUS_DATA_RECEIVED,
        info->src_addr,
        "Data RX",
        incoming.nodeId,
        countActiveNodes()
    );    
    bool queued = false;

    //forwarding logic
    if (mainEventQueue != NULL) {
        Event_t ev;
        ev.type = EVENT_NODEPACKET;
        ev.timestamp = millis();
        //memcpy(ev.payload.nodepacket.mac, info->src_addr, 6);
        ev.payload.nodepacket.nodeId = incoming.nodeId; 
        ev.payload.nodepacket.sensor = incoming.sensor;

        if (xQueueSend(mainEventQueue, &ev, pdMS_TO_TICKS(10)) == pdTRUE) {
            queued = true;
        } else {
            Serial.println("[WIFI] Failed to queue NODEPACKET event");
        
    }
    } else {
        Serial.println("[WIFI] mainEventQueue is NULL. Cannot forward packet");
    }
    
    if (queued) {
        DataAck ack;
        ack.message = DATA_ACK_MESSAGE;
        esp_err_t result = esp_now_send(info->src_addr, (uint8_t*)&ack, sizeof(ack));
        if (result == ESP_OK) {
            Serial.println("[WIFI] Data ACK sent to node");
            sendWifiStatus(
                WIFI_STATUS_DATA_ACK_SENT,
                info->src_addr,
                "ACK Sent",
                incoming.nodeId,
                countActiveNodes()
            );
        } else {
            Serial.println("[WIFI] Failed to send Data ACK");
            sendWifiStatus(
                WIFI_STATUS_ERROR,
                info->src_addr,
                "ACK failed",
                incoming.nodeId,
                countActiveNodes()
            );
        }
    }

}


static void handlePairAck(const uint8_t *mac) {
    if (!waitingForAck || memcmp(mac, pendingMac, 6) != 0) {
        return;
    }
    Serial.println("[WIFI] Pair ACK received. Pairing complete");
    waitingForAck = false;
    setHubMode(HUB_MODE_DATA);
    // Send to Display
    sendWifiStatus(
        WIFI_STATUS_PAIRING_SUCCESS,
        mac,
        "Success",
        0,
        countActiveNodes()
    );
}

static void handlePairRequest(const uint8_t *mac, const uint8_t *data, int len) {
    if (waitingForAck) {
        Serial.println("[WIFI] Currently waiting for ACK, new pair requests ignored");
        return;
    }
    if (len != sizeof(PairRequest)) {
        Serial.println("[WIFI] Invalid pair request size");
        return;
    }

    PairRequest incoming;
    memcpy(&incoming, data, sizeof(incoming));
    if (incoming.message != PAIR_MESSAGE) {
        Serial.println("[WIFI] Not a pair request");
        return;
    }

    Serial.print("[WIFI] Pair request from: ");
    Serial.println(macToString(mac));

    //Send to Display
    sendWifiStatus(
        WIFI_STATUS_NODE_FOUND,
        mac,
        "Pairing...",
        0,
        countActiveNodes()
    );

    //In case peer tries repairing(otherwise esp now stuck in encrypted deadlock)
    if (esp_now_is_peer_exist(mac)) {
        esp_now_del_peer(mac);
        Serial.println("[WIFI] Removed stale peer entry before re-pair");
    }

    //repairing in case of node reset
    uint8_t lmk[16];
    if (getNodeLMK(mac, lmk)) {
        Serial.println("[WIFI] Node already paired. Resending existing LMK");
    } else {
        for (int i = 0; i < 16; i++)
            lmk[i] = esp_random() & 0xFF;
        if (!addPairedNode(mac, lmk)) {
            return;
        }
    }

    esp_now_peer_info_t plainPeer = {};
    memcpy(plainPeer.peer_addr, mac, 6);
    plainPeer.channel = WIFI_CHANNEL;
    plainPeer.encrypt = false;
    if (esp_now_add_peer(&plainPeer) == ESP_OK)
        Serial.println("[WIFI] Temporary peer added");

    PairResponse response;
    response.message = PAIR_MESSAGE;
    memcpy(response.lmk, lmk, 16);
    esp_now_send(mac, (uint8_t*)&response, sizeof(response));
    Serial.println("[WIFI] LMK sent");

    //peer reregister now encrypted
    esp_now_del_peer(mac);
    esp_now_peer_info_t encPeer = {};
    memcpy(encPeer.peer_addr, mac, 6);
    memcpy(encPeer.lmk, lmk, 16);
    encPeer.channel = WIFI_CHANNEL;
    encPeer.encrypt = true;
    if (esp_now_add_peer(&encPeer) == ESP_OK)
        Serial.println("[WIFI] Encrypted peer added");
    else
        Serial.println("[WIFI] Failed to add encrypted peer");

    memcpy(pendingMac, mac, 6);
    waitingForAck = true;
    pairAckTimestamp = millis();
    Serial.println("[WIFI] Waiting for ACK...");
}

//code refactor
static void OnPairingRecv(const esp_now_recv_info_t *info,
                          const uint8_t *data,
                          int len) {
    if (len < (int)sizeof(uint32_t))
        return;

    uint32_t msgType;
    memcpy(&msgType, data, sizeof(msgType));

    if (msgType == PAIR_ACK_MESSAGE) {
        handlePairAck(info->src_addr);
    } else {
        handlePairRequest(info->src_addr, data, len);
    }
}

//code refactor 
static void setHubMode(HubMode_t mode) {
    hubMode = mode;
    if (mode == HUB_MODE_PAIRING) {
        esp_now_register_recv_cb(OnPairingRecv);
        waitingForAck = false;
        Serial.println("[WIFI] Hub: PAIRING mode");

        //Display Communication and node Count
        
        sendWifiStatus(
            WIFI_STATUS_PAIRING_MODE,
            nullptr,
            "Searching...",
            0,
            countActiveNodes()
        );

    } else {
        esp_now_register_recv_cb(OnDataRecv);
        Serial.println("[WIFI] Hub: DATA mode");

        //Display Communication and Node Count
        sendWifiStatus(
            WIFI_STATUS_DATA_MODE,
            nullptr,
            "OFF",
            0,
            countActiveNodes()
        );
    }
}

#endif // DEVICE_MODE == DEVICE_HUB


#if DEVICE_MODE == DEVICE_NODE

static bool paired = false;
static uint8_t hubAddress[6] = {0};
static uint8_t generatedLMK[16] = {0};

//Retry logic
static bool dataAckPending = false;
static uint32_t dataRetryCount = 0;
static uint32_t dataSendTimestamp = 0;
static const uint32_t DATA_ACK_TIMEOUT_MS = 1000;
static SensorMessage pendingDataMsg;   

static const uint32_t MAX_DATA_RETRIES = 20;

//Old callback logic, not usefull...
static void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS){
        Serial.println("[WIFI] SEND FAIL (not reached hub)");
    }
}

//send logic refactor
static void sendSensorPacket() {
    esp_now_send(hubAddress, (uint8_t*)&pendingDataMsg, sizeof(pendingDataMsg));
    dataAckPending = true;
    dataSendTimestamp = millis();
}

//initial pairing logic (recieved lmk from hub)
static void handlePairResponse(const uint8_t *mac, const uint8_t *data, int len) {
    if (len != sizeof(PairResponse)) {
        //commented out, because could get too messy with many nodes
        //Serial.println("[WIFI] Invalid response size");
        return;
    }

    //always only assign type after sizecheck
    PairResponse incoming;
    memcpy(&incoming, data, sizeof(incoming));

    Serial.print("[WIFI] Pair response from hub: ");
    Serial.println(macToString(mac));

    memcpy(hubAddress, mac, 6);
    memcpy(generatedLMK, incoming.lmk, 16);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    memcpy(peerInfo.lmk, generatedLMK, 16);
    peerInfo.channel = WIFI_CHANNEL;
    peerInfo.encrypt = true;
    if (esp_now_add_peer(&peerInfo) == ESP_OK){
        Serial.println("[WIFI] Encrypted peer (hub) added");
    }

    PairAck ack;
    ack.message = PAIR_ACK_MESSAGE;
    if (esp_now_send(hubAddress, (uint8_t*)&ack, sizeof(ack)) == ESP_OK){
        Serial.println("[WIFI] ACK sent successfully");
    }
    else{
        Serial.println("[WIFI] ACK send failed");
    }

    paired = true;
    dataAckPending = false; //just in case
}

//data recieve logic NEW helper
static void handleDataAck(const uint8_t *mac) {
    if (dataAckPending && memcmp(mac, hubAddress, 6) == 0) {
        Serial.println("[WIFI] Data ACK received from hub. Message confirmed");
        dataAckPending = false;
        //reset new count for next message
        dataRetryCount = 0;
    }
}

//node incoming logic
static void OnNodeRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len < (int)sizeof(uint32_t)){
        return;
    }

    uint32_t msgType;
    memcpy(&msgType, data, sizeof(msgType));

    if (msgType == PAIR_MESSAGE) {
        handlePairResponse(info->src_addr, data, len);
    } 
    else if (msgType == DATA_ACK_MESSAGE) {
        handleDataAck(info->src_addr);
    } 
    else {
        //too messy with many data flows
        //Serial.println("[WIFI] Unknown message type received");
    }
}

#endif // DEVICE_MODE == DEVICE_NODE


void wlanTask(void *parameter) {

#if DEVICE_MODE == DEVICE_DIKERUNNER
    WlanBackend backend;
#endif

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    Serial.println("[WIFI] wlanTask started");
    Serial.print("[WIFI] WiFi MAC: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[WIFI] ESP-NOW init failed");
    }

    esp_err_t chanResult = esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (chanResult == ESP_OK) {
        Serial.printf("[WIFI] Channel pinned to %d\n", WIFI_CHANNEL);
    } else {
        Serial.printf("[WIFI] Failed to set channel");
    }

    esp_now_set_pmk(networkPMK);


#if DEVICE_MODE == DEVICE_HUB
    for (int i = 0; i < MAX_NODES; i++){
        nodes[i].active = false;
    }
    setHubMode(HUB_MODE_DATA);
    Serial.println("[WIFI] Hub booted in DATA mode");

#elif DEVICE_MODE == DEVICE_NODE
    esp_now_register_recv_cb(OnNodeRecv);
    esp_now_register_send_cb(OnDataSent);

    //initial broadcast for pairing request
    esp_now_peer_info_t broadcastPeer = {};
    memcpy(broadcastPeer.peer_addr, broadcastAddress, 6);
    broadcastPeer.channel = WIFI_CHANNEL;
    broadcastPeer.encrypt = false;
    if (esp_now_add_peer(&broadcastPeer) != ESP_OK)
        Serial.println("[WIFI] Broadcast peer add failed");

    paired = false;
    dataAckPending = false;
    Serial.println("[WIFI] Node started, searching for hub");

#endif

    Event_t event;
    TickType_t xDelay = pdMS_TO_TICKS(100);

#if DEVICE_MODE == DEVICE_DIKERUNNER
    backend.begin();
#endif

    for (;;) {
        if (wlanInbox != NULL) {
            if (xQueueReceive(wlanInbox, &event, xDelay) == pdTRUE) {
                switch (event.type) {

#if DEVICE_MODE == DEVICE_HUB
                    /**case EVENT_BUTTON_PRESS:
                        if (event.payload.button.pressType == BUTTON_LONG_PRESS) {
                            HubMode_t newMode = (hubMode == HUB_MODE_DATA) ?
                                                HUB_MODE_PAIRING : HUB_MODE_DATA;
                            setHubMode(newMode);
                        }
                        break;**/
                    //Replaced to only happen in Pairing Check Screen
                    //FF
                    case EVENT_WIFI_COMMAND:
                        switch (event.payload.wifiCommand.command) {

                            case WIFI_COMMAND_SET_PAIRING_MODE:
                                Serial.println("Hub Change Pair");
                                setHubMode(HUB_MODE_PAIRING);
                                break;

                            case WIFI_COMMAND_SET_DATA_MODE:
                            Serial.println("Hub Change Data");   
                                setHubMode(HUB_MODE_DATA);
                                break;

                            case WIFI_COMMAND_TOGGLE_PAIRING_MODE:
                            {
                                HubMode_t newMode = (hubMode == HUB_MODE_DATA) ?
                                                    HUB_MODE_PAIRING : HUB_MODE_DATA;
                                setHubMode(newMode);
                                break;
                            }

                            default:
                                break;
                        }
                        break;
#endif

#if DEVICE_MODE == DEVICE_NODE
                    case EVENT_SENSOR_DATA: {
                        if (!paired) {
                            Serial.println("[WIFI] Not paired, cannot send data");
                            break;
                        }
                        if (dataAckPending) {
                            Serial.println("[WIFI] Waiting for previous data to be received, skipping new data");
                            break;
                        }

                        pendingDataMsg.message = DATA_MESSAGE;
                        pendingDataMsg.nodeId  = NODE_ID;
                        pendingDataMsg.sensor = event.payload.sensor;
                        sendSensorPacket();
                        //retry count reset
                        dataRetryCount = 0;
                        Serial.println("[WIFI] Sensor data sent, waiting for ACK...");
                        break;
                    }
#endif

#if DEVICE_MODE == DEVICE_DIKERUNNER
                    case EVENT_BLE_PACKET_RECEIVED:
                        backend.publishStoredHubPackets();
                        break;
#endif

                    default:
                        break;
                }
            }
        } else {
            Serial.println("wlanInbox is NULL – waiting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

//retry logic
#if DEVICE_MODE == DEVICE_NODE
        if (!paired) {
            PairRequest req;
            req.message = PAIR_MESSAGE;
            esp_now_send(broadcastAddress, (uint8_t*)&req, sizeof(req));
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            if (dataAckPending && millis() - dataSendTimestamp >= DATA_ACK_TIMEOUT_MS) {
                
                dataRetryCount++;

                if (dataRetryCount >= MAX_DATA_RETRIES) {
                    Serial.println("[WIFI] Max retries reached. Resetting to pairing mode.");
                    //remove hub
                    esp_now_del_peer(hubAddress);

                    //reset to pairing mode
                    paired = false;
                    dataAckPending = false;
                    dataRetryCount = 0;
                    memset(hubAddress, 0, sizeof(hubAddress));
                } else {
                    //Next retry
                    sendSensorPacket();
                    Serial.printf("[WIFI] Retry %d: sensor data resent\n", dataRetryCount);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
#endif

#if DEVICE_MODE == DEVICE_HUB

        if (waitingForAck &&
            millis() - pairAckTimestamp >= PAIR_ACK_TIMEOUT_MS) {

            Serial.println("[WIFI] Pairing failed: ACK timeout");

            waitingForAck = false;

            sendWifiStatus(
                WIFI_STATUS_ERROR,
                pendingMac,
                "No ACK",
                0,
                countActiveNodes()
            );

            setHubMode(HUB_MODE_DATA);
        }

#endif

    }
}
