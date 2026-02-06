#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h> 
#include <freertos/stream_buffer.h>
#include <driver/dac_oneshot.h>
#include <SPI.h> 

// ========================================
//  PINI ECRAN (TFT) - Vizualizator
// ========================================
#define TFT_WR   16
#define TFT_RS   4
#define TFT_RST  17
#define TFT_CS   19
#define SR_LATCH 27

SPIClass spi(VSPI);

// ========================================
//  CONFIGURARE VIZUAL (STANDARD)
// ========================================
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define NUM_BANDS     16
#define MAX_BAR_HEIGHT (SCREEN_HEIGHT - 40)
#define BAND_WIDTH    (SCREEN_WIDTH / NUM_BANDS) 
#define BAND_SPACING  2   
#define BAR_Y_OFFSET  20  

#define COLOR_BG      0x0000  
#define COLOR_BASS    0xF800  // Rosu
#define COLOR_MID     0x07E0  // Verde
#define COLOR_HIGH    0x001F  // Albastru

uint8_t bandLevels[NUM_BANDS] = {0};      
uint8_t prevBandLevels[NUM_BANDS] = {0};
bool strobeActive = false;
unsigned long strobeStartTime = 0;
bool screenIsBlack = true; 

// --- VARIABILE STARE ---
volatile int currentVolume = 0; 
volatile unsigned long lastRecvTime = 0; 
volatile uint8_t activeGenre = 0;
int bassThreshold = 45;
bool musicPlaying = false;

// ========================================
//  STRUCTURI (Update cu Genre si isSwPressed)
// ========================================
struct DriveCommand { int8_t drive; int8_t steer; };

struct RemoteState {
    DriveCommand dc; 
    uint8_t volume; 
    uint8_t faceIdx; 
    uint8_t speed; 
    uint8_t testPart; 
    bool isTesting; 
    bool isSwPressed; // De la colega
    bool musicPlaying; 
    uint8_t trackID; 
    uint8_t currentGenre; // De la tine
};

struct StreamingPacket{
    uint32_t packetId; uint8_t audioData[240]; uint8_t driveControl; DriveCommand dc;
};

RemoteState incomingDataState;
StreamingPacket incomingDataSP;

// --- AUDIO GLOBALS ---
StreamBufferHandle_t audioBuffer; 
dac_oneshot_handle_t dac_left, dac_right; 
esp_timer_handle_t audio_timer; 

// ========================================
//  FUNCTII ECRAN (Low Level)
// ========================================
inline void writeData8(uint8_t data) {
  spi.write(data); digitalWrite(SR_LATCH, LOW); digitalWrite(SR_LATCH, HIGH);
}
void tftWriteCommand(uint8_t cmd) {
  digitalWrite(TFT_RS, LOW); digitalWrite(TFT_CS, LOW); digitalWrite(TFT_WR, LOW);
  writeData8(cmd);
  digitalWrite(TFT_WR, HIGH); digitalWrite(TFT_CS, HIGH); digitalWrite(TFT_RS, HIGH);
}
void tftWriteData(uint8_t data) {
  digitalWrite(TFT_RS, HIGH); digitalWrite(TFT_CS, LOW); digitalWrite(TFT_WR, LOW);
  writeData8(data);
  digitalWrite(TFT_WR, HIGH); digitalWrite(TFT_CS, HIGH);
}
void tftWriteData16(uint16_t data) {
  digitalWrite(TFT_RS, HIGH); digitalWrite(TFT_CS, LOW);
  digitalWrite(TFT_WR, LOW); writeData8(data >> 8);   digitalWrite(TFT_WR, HIGH);
  digitalWrite(TFT_WR, LOW); writeData8(data & 0xFF); digitalWrite(TFT_WR, HIGH);
  digitalWrite(TFT_CS, HIGH);
}
void setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  tftWriteCommand(0x2A); tftWriteData16(x0); tftWriteData16(x1);
  tftWriteCommand(0x2B); tftWriteData16(y0); tftWriteData16(y1);
  tftWriteCommand(0x2C); 
}
void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  if (w == 0 || h == 0) return; 
  setAddrWindow(x, y, x + w - 1, y + h - 1);
  uint32_t pixels = (uint32_t)w * h;
  uint8_t hi = color >> 8; uint8_t lo = color & 0xFF;
  digitalWrite(TFT_CS, LOW); digitalWrite(TFT_RS, HIGH);
  for (uint32_t i = 0; i < pixels; i++) {
    digitalWrite(TFT_WR, LOW); spi.write(hi); digitalWrite(SR_LATCH, LOW); digitalWrite(SR_LATCH, HIGH); digitalWrite(TFT_WR, HIGH);
    digitalWrite(TFT_WR, LOW); spi.write(lo); digitalWrite(SR_LATCH, LOW); digitalWrite(SR_LATCH, HIGH); digitalWrite(TFT_WR, HIGH);
  }
  digitalWrite(TFT_CS, HIGH);
}
void triggerStrobe(bool active) { tftWriteCommand(active ? 0x21 : 0x20); }
void clearScreen() { fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG); }

// ========================================
//  VIZUALIZATOR BASIC
// ========================================
uint16_t getGradientColor(uint8_t level, uint8_t bandIndex) {
  if (bandIndex < 6) return COLOR_BASS; 
  else if (bandIndex < 12) return COLOR_MID; 
  else return COLOR_HIGH; 
}

void updateVisualizerFromAudio() {
  static float phase = 0;
  if (currentVolume > bassThreshold && !strobeActive) {
     triggerStrobe(true); strobeActive = true; strobeStartTime = millis();
     bandLevels[0] = 255; bandLevels[1] = 240;
  }
  if (strobeActive && (millis() - strobeStartTime > 35)) {
     triggerStrobe(false); strobeActive = false;
  }
  float volumeFactor = currentVolume / 60.0; 
  if (volumeFactor > 2.5) volumeFactor = 2.5;

  for (int i = 2; i < NUM_BANDS; i++) {
    int wave = 50 + 40 * sin(phase * (i * 0.3) + millis() / 150.0);
    int finalVal = wave * volumeFactor; 
    finalVal += random(-5, 5); 
    bandLevels[i] = constrain(finalVal, 0, 255);
  }
  if (!strobeActive) {
      if (bandLevels[0] > 0) bandLevels[0] -= 20;
      if (bandLevels[1] > 0) bandLevels[1] -= 15;
      if (bandLevels[0] < currentVolume * 2) bandLevels[0] = currentVolume * 2;
  }
  phase += 0.2;
  
  for (int i = 0; i < NUM_BANDS; i++) {
    uint8_t currentLevel = bandLevels[i];
    uint8_t prevLevel = prevBandLevels[i];
    if (abs(currentLevel - prevLevel) < 3) continue;
    uint16_t x = i * BAND_WIDTH + BAND_SPACING;
    uint16_t barWidth = BAND_WIDTH - (2 * BAND_SPACING);
    uint16_t currentHeight = map(currentLevel, 0, 255, 0, MAX_BAR_HEIGHT);
    uint16_t prevHeight = map(prevLevel, 0, 255, 0, MAX_BAR_HEIGHT);
    uint16_t barY = BAR_Y_OFFSET + (MAX_BAR_HEIGHT - currentHeight);
    uint16_t prevBarY = BAR_Y_OFFSET + (MAX_BAR_HEIGHT - prevHeight);
    if (currentHeight > prevHeight) fillRect(x, barY, barWidth, prevBarY - barY, getGradientColor(currentLevel, i));
    else if (currentHeight < prevHeight) fillRect(x, prevBarY, barWidth, barY - prevBarY, COLOR_BG);
    prevBandLevels[i] = currentLevel;
  }
}

// ========================================
//  TASK-URI
// ========================================

// AUDIO ISR (De la colega - Mono Fix)
void IRAM_ATTR onAudioTimer(void* arg) {
    uint8_t samples[2]; 
    size_t bytes = xStreamBufferReceiveFromISR(audioBuffer, samples, 2, NULL);
    if (bytes == 2) {
        int16_t sample16 = (int16_t)((samples[1] << 8) | samples[0]);
        uint8_t sample8 = (sample16 / 256) + 128; 
        dac_oneshot_output_voltage(dac_left, sample8);
        dac_oneshot_output_voltage(dac_right, sample8);
    } else {
        dac_oneshot_output_voltage(dac_left, 128);
        dac_oneshot_output_voltage(dac_right, 128);
    }
}

// WIFI CALLBACK (MERGE: Debug Colega + Volume Calc Tine + Genre Tine)
void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incoming, int len) {
  lastRecvTime = millis(); 

  // --- CAZ 1: COMANDA (RemoteState) ---
  if(len == sizeof(RemoteState)) {
    memcpy(&incomingDataState, incoming, sizeof(incomingDataState));
    musicPlaying = false; 
    
    // Debug Genre (De la tine)
    if (incomingDataState.currentGenre != activeGenre) {
        activeGenre = incomingDataState.currentGenre;
        Serial.print(">>> GENRE ID RECEIVED: "); Serial.println(activeGenre);
    }
  }
  // --- CAZ 2: AUDIO (StreamingPacket) ---
  else if (len == sizeof(StreamingPacket)) {
    memcpy(&incomingDataSP, incoming, sizeof(incomingDataSP));
    musicPlaying = true;
    
    // Calcul Volum (De la tine - pt vizualizator)
    long sum = 0;
    for(int i=0; i<240; i+=2) sum += abs((int)incomingDataSP.audioData[i] - 128);
    currentVolume = sum / 120; 

    // Buffer send (De la colega)
    xStreamBufferSendFromISR(audioBuffer, incomingDataSP.audioData, 240, NULL);
  }
}

// TASK VIZUAL (De la tine)
void visualizerTask(void *pvParameters) {
    Serial.println("VIZUALIZATOR STARTED");
    for (;;) { 
        if (millis() - lastRecvTime < 600 && musicPlaying) {
            updateVisualizerFromAudio();
            screenIsBlack = false;
        } else {
            if (!screenIsBlack) {
                triggerStrobe(false); strobeActive = false;
                for(int i=0; i<NUM_BANDS; i++) { bandLevels[i] = 0; prevBandLevels[i] = 0; }
                clearScreen();
                screenIsBlack = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5)); 
    }
}

void setup() {
  Serial.begin(115200);
  
  // INIT ECRAN
  spi.begin(18, -1, 23, -1);
  spi.setFrequency(20000000); 
  spi.setDataMode(SPI_MODE0); spi.setBitOrder(MSBFIRST);
  pinMode(SR_LATCH, OUTPUT); pinMode(TFT_WR, OUTPUT); pinMode(TFT_RS, OUTPUT);
  pinMode(TFT_RST, OUTPUT); pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_WR, HIGH); digitalWrite(TFT_RS, HIGH); digitalWrite(TFT_CS, HIGH); digitalWrite(SR_LATCH, HIGH);
  digitalWrite(TFT_RST, HIGH); delay(50); digitalWrite(TFT_RST, LOW); delay(50); digitalWrite(TFT_RST, HIGH); delay(200);
  tftWriteCommand(0x01); delay(150); tftWriteCommand(0x11); delay(150);
  tftWriteCommand(0x3A); tftWriteData(0x55); 
  tftWriteCommand(0x36); tftWriteData(0x68); tftWriteCommand(0x29); 
  clearScreen();

  // INIT WIFI & AUDIO
  WiFi.mode(WIFI_STA);
  esp_wifi_set_max_tx_power(84);
  esp_wifi_set_ps(WIFI_PS_NONE);
  
  audioBuffer = xStreamBufferCreate(8192, 1);
  
  dac_oneshot_config_t dac_conf = { .chan_id = DAC_CHAN_0 };
  dac_oneshot_new_channel(&dac_conf, &dac_left); dac_oneshot_output_voltage(dac_left, 128);
  dac_conf.chan_id = DAC_CHAN_1;
  dac_oneshot_new_channel(&dac_conf, &dac_right); dac_oneshot_output_voltage(dac_right, 128);

  const esp_timer_create_args_t timer_args = { .callback = &onAudioTimer, .name = "audio_timer" };
  esp_timer_create(&timer_args, &audio_timer);
  
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(OnDataRecv);
  
  while(xStreamBufferBytesAvailable(audioBuffer) < 1000) delay(50);
  esp_timer_start_periodic(audio_timer, 62);
  
  xTaskCreatePinnedToCore(visualizerTask, "VisualTask", 4096, NULL, 1, NULL, 1);
  Serial.println("SYSTEM READY.");
}

void loop() {
  // Motor fail-safe
  if (millis() - lastRecvTime > 1000) {
    incomingDataState.dc.drive = 0; incomingDataState.dc.steer = 0;
  }
  delay(10);
}