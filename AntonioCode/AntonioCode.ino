#include <esp_now.h>
#include <WiFi.h>

struct DriveCommand {
    int8_t drive;
    int8_t steer;
};

struct RemoteState {
    DriveCommand dc;
    uint8_t volume; 
    uint8_t faceIdx;
    uint8_t speed;
    uint8_t testPart;
    bool isTesting;
    bool musicPlaying;
    uint8_t trackID;
};


struct StreamingPacket{
    uint32_t packetId;
    uint8_t audioData[240];
    uint8_t driveControl;
    DriveCommand dc;
};

RemoteState incomingDataState;
StreamingPacket incomingDataSP;
unsigned long lastRecvTime = 0;
unsigned long lastPrint = 0;
bool musicPlaying;

// Update this function in your receiver code
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incoming, int len) {
  // Extract the MAC address from the new info struct
  uint8_t* mac = recv_info->src_addr;
  if(len == sizeof(RemoteState)) {
    memcpy(&incomingDataState, incoming, sizeof(incomingDataState));
    musicPlaying = false;
  }
  else {
    memcpy(&incomingDataSP, incoming, sizeof(incomingDataSP));
    musicPlaying = true;
  }
  lastRecvTime = millis(); 
  Serial.printf("RX from: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  
    if(!musicPlaying) {
      Serial.print("MODE: ");
      if (incomingDataState.isTesting) Serial.print("TESTING ");
      else Serial.print("IDLE ");

      Serial.printf("| Part: %d | Face: %d | Drive: %d | Steer: %d | Speed: %d\n",
        incomingDataState.testPart, incomingDataState.faceIdx, incomingDataState.dc.drive, incomingDataState.dc.steer, incomingDataState.speed);

    } else {
      Serial.println("Music");
    }
}

void setup() {
  Serial.begin(115200);
  
  WiFi.mode(WIFI_STA);
  

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
  // --- FAIL-SAFE LOGIC ---
  // If we haven't heard from the remote in 1 second, stop the robot
  if (millis() - lastRecvTime > 1000) {
    incomingDataState.dc.drive = 0;
    incomingDataState.dc.steer = 0;
    incomingDataState.isTesting = false;
    // Serial.println("!!! REMOTE DISCONNECTED - EMERGENCY STOP !!!");
  }

  // Here is where you would map incomingData to your Motor Drivers
  // Example: analogWrite(MOTOR_PIN, incomingData.drive * speedMultiplier);
  
  delay(10); // Small delay to keep the watchdog happy
}