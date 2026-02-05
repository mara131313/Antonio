#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h> 
#include <freertos/stream_buffer.h>
#include <driver/dac_oneshot.h>

// --- STRUCTURES ---
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
bool musicPlaying = false;

// --- AUDIO GLOBALS ---
StreamBufferHandle_t audioBuffer; 
dac_oneshot_handle_t dac_left, dac_right; 
esp_timer_handle_t audio_timer; 

// ✅ MODIFICAT: Citește 2 bytes (1 sample MONO) în loc de 4 bytes (stereo)
void IRAM_ATTR onAudioTimer(void* arg) {
    uint8_t samples[2]; // ✅ 2 bytes = 1 sample MONO 16-bit
    
    size_t bytes = xStreamBufferReceiveFromISR(audioBuffer, samples, 2, NULL);
    
    if (bytes == 2) {
        // ✅ Conversie corectă SIGNED 16-bit → UNSIGNED 8-bit
        int16_t sample16 = (int16_t)((samples[1] << 8) | samples[0]);
        uint8_t sample8 = (sample16 / 256) + 128;
        
        // ✅ ACELAȘI sample pe ambii DAC (MONO → Stereo)
        dac_oneshot_output_voltage(dac_left, sample8);
        dac_oneshot_output_voltage(dac_right, sample8);
    } else {
        dac_oneshot_output_voltage(dac_left, 128);
        dac_oneshot_output_voltage(dac_right, 128);
    }
}

// ✅ ADĂUGAT: Debug pentru pachete pierdute
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incoming, int len) {
  static uint32_t packetsReceived = 0;
  static uint32_t packetsLost = 0;
  static uint32_t lastPacketId = 0;
  
  lastRecvTime = millis(); 

  if(len == sizeof(RemoteState)) {
    memcpy(&incomingDataState, incoming, sizeof(incomingDataState));
    musicPlaying = false;
    Serial.println("🛑 Music STOPPED via RemoteState");
  }
  else if (len == sizeof(StreamingPacket)) {
    memcpy(&incomingDataSP, incoming, sizeof(incomingDataSP));
    musicPlaying = true;
    
    // ✅ ADĂUGAT: Detectare pachete pierdute
    packetsReceived++;
    if (incomingDataSP.packetId != lastPacketId + 1 && lastPacketId != 0) {
        packetsLost += (incomingDataSP.packetId - lastPacketId - 1);
        Serial.printf("⚠️ LOST %d packets!\n", incomingDataSP.packetId - lastPacketId - 1);
    }
    lastPacketId = incomingDataSP.packetId;
    
    // ✅ ADĂUGAT: Stats la fiecare 200 pachete
    if (packetsReceived % 200 == 0) {
        Serial.printf("📊 Received: %d | Lost: %d (%.1f%% loss) | Buffer: %d bytes\n", 
                      packetsReceived, packetsLost, 
                      (packetsLost * 100.0) / (packetsReceived + packetsLost),
                      xStreamBufferBytesAvailable(audioBuffer));
    }

    xStreamBufferSendFromISR(audioBuffer, incomingDataSP.audioData, 240, NULL);
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  
  // ✅ ADĂUGAT: WiFi max power pentru conexiune stabilă
  esp_wifi_set_max_tx_power(84);
  esp_wifi_set_ps(WIFI_PS_NONE);
  
  pinMode(25, OUTPUT);
  pinMode(26, OUTPUT);

  // ✅ MĂRIT: Buffer 8KB în loc de 4KB
  audioBuffer = xStreamBufferCreate(8192, 1);

  // Setup DAC LEFT (GPIO 25)
  dac_oneshot_config_t dac_conf_left = {
      .chan_id = DAC_CHAN_0,
  };
  dac_oneshot_new_channel(&dac_conf_left, &dac_left);
  dac_oneshot_output_voltage(dac_left, 128);

  // Setup DAC RIGHT (GPIO 26)
  dac_oneshot_config_t dac_conf_right = {
      .chan_id = DAC_CHAN_1,
  };
  dac_oneshot_new_channel(&dac_conf_right, &dac_right);
  dac_oneshot_output_voltage(dac_right, 128);

  // Setup Timer (16kHz = 62.5us)
  const esp_timer_create_args_t timer_args = {
      .callback = &onAudioTimer,
      .name = "audio_timer"
  };
  esp_timer_create(&timer_args, &audio_timer);
  
  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  
  Serial.println("✅ Robot Receiver Ready (MONO 16kHz 16-bit mode)");
  Serial.println("Waiting for audio buffer to fill...");
  
  // ✅ ADĂUGAT: Pre-buffering - așteaptă 1000 bytes
  while(xStreamBufferBytesAvailable(audioBuffer) < 1000) {
    delay(50);
  }
  
  // Pornește timer-ul DUPĂ pre-buffering
  esp_timer_start_periodic(audio_timer, 62);
  Serial.println("🎵 Audio timer started!");
}

void loop() {
  // FAIL-SAFE
  if (millis() - lastRecvTime > 1000) {
    incomingDataState.dc.drive = 0;
    incomingDataState.dc.steer = 0;
    if (musicPlaying) {
      Serial.println("⚠️ Connection lost - stopping music");
      musicPlaying = false;
    }
  }

  // MOTOR CONTROL
  int drive = musicPlaying ? incomingDataSP.dc.drive : incomingDataState.dc.drive;

  // ✅ ADĂUGAT: Debug îmbunătățit
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
      Serial.printf("Status: %s | Buffer: %d/%d bytes | Heap: %d\n", 
          musicPlaying ? "🎵 PLAYING" : "⏸️  IDLE", 
          xStreamBufferBytesAvailable(audioBuffer),
          xStreamBufferSpacesAvailable(audioBuffer),
          ESP.getFreeHeap());
      lastPrint = millis();
  }
  
  delay(10); 
}