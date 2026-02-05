#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

uint8_t robotAddress[] = {0x88, 0x13, 0xBF, 0x0D, 0xBF, 0xA0}; 

void setupComms() {
  WiFi.mode(WIFI_STA);
  
  // ✅ ADĂUGAT: WiFi max power
  esp_wifi_set_max_tx_power(84);
  esp_wifi_set_ps(WIFI_PS_NONE);
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
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

