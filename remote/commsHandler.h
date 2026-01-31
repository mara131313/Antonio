#include <esp_now.h>
#include <WiFi.h>
#include "protocol.h"

uint8_t robotAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 

void setupComms() {
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Eroare la initializarea ESP-NOW");
    return;
  }

  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, robotAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("ANTONIO wasn't added as a peer");
    return;
  }
}

void sendData(RemoteState &state) {
  esp_err_t result = esp_now_send(robotAddress, (uint8_t *) &state, sizeof(state));
    
  if (result != ESP_OK) {
    Serial.println("Error: couldn't send");
  }
}