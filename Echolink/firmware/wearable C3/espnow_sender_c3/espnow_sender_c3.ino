/*
  Milestone 2a: ESP-NOW Sender
  Board: ESP32-C3 mini

  Purpose: Prove the wireless link works before adding audio,
  compression, or encryption. Sends a short text packet once a
  second using the SAME packet struct the real build will use for
  audio chunks later -- seq / chunkIndex / chunkTotal / msgType /
  payload. Right now every message is a single chunk (chunkTotal=1),
  but the receiver code is already written to reassemble multi-chunk
  messages, so this struct carries forward unchanged.

  No wiring needed for this test -- WiFi radio is on-chip.

  Written against Arduino-ESP32 core 3.x ESP-NOW API. If you're on
  core 2.x, the OnDataSent callback signature is the same, but check
  esp_now.h for exact types if you hit a compile error there.
*/

#include <WiFi.h>
#include <esp_now.h>

// Broadcast address -- every ESP-NOW peer on the same channel receives
// this, no MAC address lookup/hardcoding needed for this test stage.
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// msgType values (shared convention with the receiver and future milestones)
#define MSG_TEXT 0
#define MSG_AUDIO 1   // used in milestone 3
#define MSG_SOS   2   // used in milestone 3 (MPU6050 override)

typedef struct __attribute__((packed)) {
  uint16_t seq;          // increments per logical message
  uint8_t  chunkIndex;   // which chunk this is (0-based)
  uint8_t  chunkTotal;   // how many chunks make up the full message
  uint8_t  msgType;      // MSG_TEXT / MSG_AUDIO / MSG_SOS
  uint8_t  length;       // valid bytes in payload
  uint8_t  payload[200]; // ESP-NOW packets max out around 250 bytes total,
                          // this leaves headroom for the header fields above
} espnow_packet_t;

espnow_packet_t outgoing;
uint16_t seqCounter = 0;

// NOTE: core 3.3.x changed this callback's first parameter from a raw MAC
// address (const uint8_t*) to a wifi_tx_info_t* containing tx metadata,
// including the destination MAC at tx_info->des_addr if you need it later.
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "delivered" : "FAILED");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed -- halting.");
    while (true) delay(1000);
  }

  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0;     // 0 = use current WiFi channel
  peerInfo.encrypt = false; // plain text for this milestone

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer -- halting.");
    while (true) delay(1000);
  }

  Serial.println("ESP-NOW sender ready.");
}

void loop() {
  char msg[64];
  snprintf(msg, sizeof(msg), "Hello from C3 #%u", seqCounter);

  outgoing.seq = seqCounter;
  outgoing.chunkIndex = 0;
  outgoing.chunkTotal = 1;
  outgoing.msgType = MSG_TEXT;
  outgoing.length = strlen(msg);
  memcpy(outgoing.payload, msg, outgoing.length);

  // Only send the bytes actually in use, not the whole padded struct
  size_t packetSize = offsetof(espnow_packet_t, payload) + outgoing.length;
  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&outgoing, packetSize);

  Serial.print("Sending: ");
  Serial.print(msg);
  Serial.println(result == ESP_OK ? "  [queued ok]" : "  [queue FAILED]");

  seqCounter++;
  delay(1000);
}
