/*
  Milestone 2b: ESP-NOW Receiver
  Board: ESP32 WROOM

  Purpose: Receive the packets sent by espnow_sender_c3.ino, validate
  they arrived in order, and print them. This uses the same packet
  struct as the sender -- in milestone 3, msgType MSG_AUDIO packets
  get reassembled by chunkIndex/chunkTotal into an audio buffer and
  played through the MAX98357A instead of just printed.

  No wiring needed for this test -- WiFi radio is on-chip. You can
  wire the OLED (SDA=21, SCL=22) alongside this later to show
  "connected" / last message on screen instead of only Serial.
*/

#include <WiFi.h>
#include <esp_now.h>

#define MSG_TEXT  0
#define MSG_AUDIO 1
#define MSG_SOS   2

typedef struct __attribute__((packed)) {
  uint16_t seq;
  uint8_t  chunkIndex;
  uint8_t  chunkTotal;
  uint8_t  msgType;
  uint8_t  length;
  uint8_t  payload[200];
} espnow_packet_t;

uint16_t lastSeqSeen = 0;
bool firstPacket = true;

void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  if (len < (int)offsetof(espnow_packet_t, payload)) {
    Serial.println("Received undersized packet, ignoring.");
    return;
  }

  espnow_packet_t packet;
  memcpy(&packet, incomingData, len);

  // Basic ordering/loss check -- not a real protocol yet, just visibility
  // into whether packets are arriving reliably over the air.
  if (!firstPacket && packet.seq != (uint16_t)(lastSeqSeen + 1)) {
    Serial.print("!! Gap detected -- expected seq ");
    Serial.print(lastSeqSeen + 1);
    Serial.print(", got ");
    Serial.println(packet.seq);
  }
  lastSeqSeen = packet.seq;
  firstPacket = false;

  char senderMac[18];
  snprintf(senderMac, sizeof(senderMac), "%02X:%02X:%02X:%02X:%02X:%02X",
           recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
           recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);

  switch (packet.msgType) {
    case MSG_TEXT: {
      char text[201];
      memcpy(text, packet.payload, packet.length);
      text[packet.length] = '\0';
      Serial.print("[seq ");
      Serial.print(packet.seq);
      Serial.print(" from ");
      Serial.print(senderMac);
      Serial.print("] ");
      Serial.println(text);
      break;
    }
    case MSG_AUDIO:
      Serial.println("(audio chunk -- handled in milestone 3)");
      break;
    case MSG_SOS:
      Serial.println("!!! SOS packet -- handled in milestone 3 !!!");
      break;
    default:
      Serial.println("Unknown msgType, ignoring.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed -- halting.");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(OnDataRecv);

  Serial.print("Receiver MAC address: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Waiting for packets...");
}

void loop() {
  // Nothing to do here -- OnDataRecv fires asynchronously.
  delay(1000);
}
