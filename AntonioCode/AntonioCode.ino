#include <esp_now.h>
#include <WiFi.h>
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
dac_oneshot_handle_t dac_handle; 
esp_timer_handle_t audio_timer; 

// --- AUDIO INTERRUPT (Runs every 62.5us) ---
void IRAM_ATTR onAudioTimer(void* arg) {
    uint8_t sample;
    // Try to grab 1 byte from buffer (Non-blocking)
    size_t bytes = xStreamBufferReceiveFromISR(audioBuffer, &sample, 1, NULL);
    
    if (bytes > 0) {
        // FIXED: Using 'dac_oneshot_output_voltage' as requested by compiler
        dac_oneshot_output_voltage(dac_handle, sample);
    } else {
        // Silence if buffer empty
        dac_oneshot_output_voltage(dac_handle, 0); 
    }
}

// --- CALLBACK: Radio Packet Received ---
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incoming, int len) {
  lastRecvTime = millis(); 

  // Case 1: Control Packet (RemoteState)
  if(len == sizeof(RemoteState)) {
    memcpy(&incomingDataState, incoming, sizeof(incomingDataState));
    musicPlaying = false;
  }
  // Case 2: Audio Packet (StreamingPacket)
  else if (len == sizeof(StreamingPacket)) {
    memcpy(&incomingDataSP, incoming, sizeof(incomingDataSP));
    musicPlaying = true;

    // Fill the buffer (Non-blocking)
    xStreamBufferSendFromISR(audioBuffer, incomingDataSP.audioData, 240, NULL);
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  pinMode(25, OUTPUT);
  pinMode(26, OUTPUT);


  // 1. Create Audio Buffer (Holds ~0.25 seconds of audio)
  audioBuffer = xStreamBufferCreate(4096, 1);

  // 2. Setup DAC (FIXED for your library version)
  dac_oneshot_config_t dac_conf = {
      .chan_id = DAC_CHAN_1, // GPIO 25
  };
  // Use 'new_channel' instead of 'new_handle'
  dac_oneshot_new_channel(&dac_conf, &dac_handle);
  dac_oneshot_output_voltage(dac_handle, 0); // Start silent

  // 3. Setup Timer (16kHz = 62.5 microseconds)
  const esp_timer_create_args_t timer_args = {
      .callback = &onAudioTimer,
      .name = "audio_timer"
  };
  esp_timer_create(&timer_args, &audio_timer);
  esp_timer_start_periodic(audio_timer, 62); 

  // 4. Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(OnDataRecv);
  
  Serial.println("Robot Receiver Ready (Fixed DAC Mode)");
}

void loop() {
  // --- FAIL-SAFE ---
  if (millis() - lastRecvTime > 1000) {
    incomingDataState.dc.drive = 0;
    incomingDataState.dc.steer = 0;
    musicPlaying = false;
  }

  // --- MOTOR CONTROL ---
  int drive = musicPlaying ? incomingDataSP.dc.drive : incomingDataState.dc.drive;
  // int steer = musicPlaying ? incomingDataSP.dc.steer : incomingDataState.dc.steer;

  // Debug Prints
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
      Serial.printf("Status: %s | Buffer: %d bytes\n", 
          musicPlaying ? "PLAYING" : "IDLE", 
          xStreamBufferBytesAvailable(audioBuffer));
      lastPrint = millis();
  }
  
  delay(10); 
}