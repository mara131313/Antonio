#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h> 
#include <freertos/stream_buffer.h>
#include <driver/dac_oneshot.h>
#include <SPI.h> 

// ========================================
//  PINI HARDWARE
// ========================================
#define TFT_WR   16
#define TFT_RS   4
#define TFT_RST  17
#define SR_LATCH 27

// CHIP SELECT (SEPARARE ECRANE)
#define TFT_CS_SMALL 19  // Ecran Mic (Visualizer)
#define TFT_CS_BIG   5   // Ecran Mare (Fata)

SPIClass spi(VSPI);

// ========================================
//  CONFIGURARE DIMENSIUNI
// ========================================
#define SMALL_W 320
#define SMALL_H 240
#define BIG_W   480
#define BIG_H   320

// Culori Generale
#define COLOR_BLACK   0x0000
#define COLOR_BG      0x0000 
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F

// CULORI VIZUALIZATOR
#define COLOR_BASS    0xF800  
#define COLOR_MID     0x07E0  
#define COLOR_HIGH    0x001F  

// Variabile Vizualizator
#define NUM_BANDS 16
#define BAND_WIDTH (SMALL_W / NUM_BANDS)
#define BAND_SPACING 2 
#define BAR_Y_OFFSET 20
#define MAX_BAR_HEIGHT (SMALL_H - 40)

uint8_t bandLevels[NUM_BANDS] = {0};      
uint8_t prevBandLevels[NUM_BANDS] = {0};
bool strobeActive = false;
unsigned long strobeStartTime = 0;
bool screenIsBlack = true; 

// Threshold marit pentru a preveni flash-uri false care consuma curent
int bassThreshold = 50; 

// Variabile Sistem
volatile int currentVolume = 0; 
volatile unsigned long lastRecvTime = 0; 
volatile uint8_t activeGenre = 0;
bool musicPlaying = false;
uint8_t currentFace = 255; 

// ========================================
//  STRUCTURI WIFI
// ========================================
struct DriveCommand { int8_t drive; int8_t steer; };
struct RemoteState {
    DriveCommand dc; uint8_t volume; uint8_t faceIdx; uint8_t speed;
    uint8_t testPart; bool isTesting; bool isSwPressed; bool musicPlaying; 
    uint8_t trackID; uint8_t currentGenre; 
};
struct StreamingPacket{
    uint32_t packetId; uint8_t audioData[240]; uint8_t driveControl; DriveCommand dc;
};
RemoteState incomingDataState;
StreamingPacket incomingDataSP;
StreamBufferHandle_t audioBuffer; 
dac_oneshot_handle_t dac_left, dac_right; 
esp_timer_handle_t audio_timer; 

// ========================================
//  DRIVER SHIFT REGISTER
// ========================================

void writeBus8_Small(uint8_t data) {
  spi.write(0x00);        
  spi.write(data);        
  digitalWrite(SR_LATCH, LOW); 
  digitalWrite(SR_LATCH, HIGH);
}

void writeBus16_Big(uint16_t data) {
  spi.write(data >> 8);   
  spi.write(data & 0xFF); 
  digitalWrite(SR_LATCH, LOW); 
  digitalWrite(SR_LATCH, HIGH);
}

// ========================================
//  FUNCTII ECRAN MIC (VISUALIZER)
// ========================================
void cmdSmall(uint8_t cmd) {
  // CRITIC: Asiguram ca Ecranul Mare e MORT (CS HIGH)
  digitalWrite(TFT_CS_BIG, HIGH);   
  
  digitalWrite(TFT_CS_SMALL, LOW); 
  digitalWrite(TFT_RS, LOW); digitalWrite(TFT_WR, LOW);
  writeBus8_Small(cmd);
  digitalWrite(TFT_WR, HIGH); digitalWrite(TFT_CS_SMALL, HIGH);
}

void dataSmall(uint8_t data) {
  digitalWrite(TFT_CS_BIG, HIGH);
  digitalWrite(TFT_CS_SMALL, LOW);
  digitalWrite(TFT_RS, HIGH); digitalWrite(TFT_WR, LOW);
  writeBus8_Small(data);
  digitalWrite(TFT_WR, HIGH); digitalWrite(TFT_CS_SMALL, HIGH);
}

void data16Small(uint16_t data) {
  digitalWrite(TFT_CS_BIG, HIGH);
  digitalWrite(TFT_CS_SMALL, LOW);
  digitalWrite(TFT_RS, HIGH);
  
  digitalWrite(TFT_WR, LOW); writeBus8_Small(data >> 8);   digitalWrite(TFT_WR, HIGH);
  digitalWrite(TFT_WR, LOW); writeBus8_Small(data & 0xFF); digitalWrite(TFT_WR, HIGH);
  
  digitalWrite(TFT_CS_SMALL, HIGH);
}

void setAddrWindowSmall(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  cmdSmall(0x2A); data16Small(x0); data16Small(x1);
  cmdSmall(0x2B); data16Small(y0); data16Small(y1);
  cmdSmall(0x2C); 
}

void fillRectSmall(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  if (w == 0 || h == 0) return; 
  setAddrWindowSmall(x, y, x + w - 1, y + h - 1);
  uint32_t pixels = (uint32_t)w * h;
  
  // SECVENTA STRICTA ANTI-BLEED
  digitalWrite(TFT_CS_BIG, HIGH); // Asiguram OFF
  digitalWrite(TFT_CS_SMALL, LOW); // Pornim Mic
  digitalWrite(TFT_RS, HIGH);

  for (uint32_t i = 0; i < pixels; i++) {
    // High Byte
    digitalWrite(TFT_WR, LOW); spi.write(0x00); spi.write(color >> 8); 
    digitalWrite(SR_LATCH, LOW); digitalWrite(SR_LATCH, HIGH); digitalWrite(TFT_WR, HIGH);
    // Low Byte
    digitalWrite(TFT_WR, LOW); spi.write(0x00); spi.write(color & 0xFF);
    digitalWrite(SR_LATCH, LOW); digitalWrite(SR_LATCH, HIGH); digitalWrite(TFT_WR, HIGH);
  }
  digitalWrite(TFT_CS_SMALL, HIGH);
}

void clearSmall(uint16_t color) { fillRectSmall(0, 0, SMALL_W, SMALL_H, color); }
void triggerStrobe(bool active) { cmdSmall(active ? 0x21 : 0x20); }

// ========================================
//  FUNCTII ECRAN MARE (FACES)
// ========================================
void cmdBig(uint8_t cmd) {
  digitalWrite(TFT_CS_SMALL, HIGH); 
  digitalWrite(TFT_CS_BIG, LOW);    
  digitalWrite(TFT_RS, LOW); digitalWrite(TFT_WR, LOW);
  writeBus16_Big((uint16_t)cmd);    
  digitalWrite(TFT_WR, HIGH); digitalWrite(TFT_CS_BIG, HIGH);
}

void dataBig(uint16_t data) {
  digitalWrite(TFT_CS_SMALL, HIGH);
  digitalWrite(TFT_CS_BIG, LOW);
  digitalWrite(TFT_RS, HIGH); digitalWrite(TFT_WR, LOW);
  writeBus16_Big(data);
  digitalWrite(TFT_WR, HIGH); digitalWrite(TFT_CS_BIG, HIGH);
}

void setAddrWindowBig(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  cmdBig(0x2A); dataBig(x0 >> 8); dataBig(x0 & 0xFF); dataBig(x1 >> 8); dataBig(x1 & 0xFF);
  cmdBig(0x2B); dataBig(y0 >> 8); dataBig(y0 & 0xFF); dataBig(y1 >> 8); dataBig(y1 & 0xFF);
  cmdBig(0x2C);
}

void fillRectBig(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  if (w == 0 || h == 0) return; 
  setAddrWindowBig(x, y, x + w - 1, y + h - 1);
  uint32_t pixels = (uint32_t)w * h;
  
  digitalWrite(TFT_CS_SMALL, HIGH);
  digitalWrite(TFT_CS_BIG, LOW);
  digitalWrite(TFT_RS, HIGH);

  for (uint32_t i = 0; i < pixels; i++) {
    digitalWrite(TFT_WR, LOW);
    writeBus16_Big(color);
    digitalWrite(TFT_WR, HIGH);
  }
  digitalWrite(TFT_CS_BIG, HIGH);
}

void clearBig(uint16_t color) { fillRectBig(0, 0, BIG_W, BIG_H, color); }

// ========================================
//  LOGICA FACES + ANTI-BLEED FIX
// ========================================
void drawFace(uint8_t faceIdx) {
  uint16_t bg = COLOR_BLACK;
  uint16_t eyeColor = COLOR_WHITE;
  
  switch(faceIdx) {
    case 1: bg = COLOR_YELLOW; eyeColor = COLOR_BLACK; break; 
    case 2: bg = COLOR_CYAN; eyeColor = COLOR_BLACK; break;   
    case 3: bg = 0xF81F; eyeColor = COLOR_WHITE; break;       
    default: bg = COLOR_BLACK; eyeColor = COLOR_GREEN; break; 
  }
  
  clearBig(bg); 
  int eyeSize = 60; int eyeY = 100;
  
  // Ochi
  fillRectBig(100, eyeY, eyeSize, eyeSize, (faceIdx==3)? COLOR_RED : eyeColor);
  fillRectBig(320, eyeY, eyeSize, eyeSize, (faceIdx==3)? COLOR_RED : eyeColor);
  
  // Gura
  if(faceIdx == 1) { 
     fillRectBig(140, 240, 200, 30, eyeColor); fillRectBig(140, 240, 30, 60, eyeColor); fillRectBig(310, 240, 30, 60, eyeColor); 
  } else if (faceIdx == 3) {
     fillRectBig(160, 220, 160, 20, COLOR_RED); 
  } else {
     fillRectBig(140, 240, 200, 20, eyeColor); 
  }
  
  // === CRITIC: FIX PENTRU BLEEDING ===
  // Imediat dupa ce am terminat de desenat fata, trimitem comanda NOP (0x00)
  // Asta spune ecranului mare "Gata, inchide bufferul, nu mai asculta!"
  cmdBig(0x00); 
  
  // Asiguram inca o data ca CS-ul lui e HIGH
  digitalWrite(TFT_CS_BIG, HIGH);
}

// ========================================
//  LOGICA VIZUALIZATOR
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
    
    // ANTI-BLEED Check inainte de fiecare bara
    digitalWrite(TFT_CS_BIG, HIGH);

    if (currentHeight > prevHeight) {
      fillRectSmall(x, barY, barWidth, prevBarY - barY, getGradientColor(currentLevel, i));
    } else if (currentHeight < prevHeight) {
      fillRectSmall(x, prevBarY, barWidth, barY - prevBarY, COLOR_BG);
    }
    prevBandLevels[i] = currentLevel;
  }
}

// ========================================
//  INITIALIZARE
// ========================================
void initSmallScreen() {
    cmdSmall(0x01); delay(150);
    cmdSmall(0x11); delay(150);
    cmdSmall(0x3A); dataSmall(0x55);
    cmdSmall(0x36); dataSmall(0x68); 
    cmdSmall(0x29); 
    clearSmall(COLOR_BLACK);
}

void initBigScreen() {
    cmdBig(0x01); delay(150);
    cmdBig(0x11); delay(150);
    cmdBig(0x3A); dataBig(0x55); 
    cmdBig(0x36); dataBig(0xE8); 
    cmdBig(0x29); 
    clearBig(COLOR_BLACK);
}

// ========================================
//  TASKS
// ========================================
void IRAM_ATTR onAudioTimer(void* arg) {
    uint8_t samples[2]; 
    size_t bytes = xStreamBufferReceiveFromISR(audioBuffer, samples, 2, NULL);
    if (bytes == 2) {
        int16_t sample16 = (int16_t)((samples[1] << 8) | samples[0]);
        uint8_t sample8 = (sample16 / 256) + 128; 
        dac_oneshot_output_voltage(dac_left, sample8); dac_oneshot_output_voltage(dac_right, sample8);
    } else { dac_oneshot_output_voltage(dac_left, 128); dac_oneshot_output_voltage(dac_right, 128); }
}

void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incoming, int len) {
  lastRecvTime = millis(); 
  
  if(len == sizeof(RemoteState)) {
    memcpy(&incomingDataState, incoming, sizeof(incomingDataState));
    musicPlaying = false;
    
    if (incomingDataState.currentGenre != activeGenre) {
        activeGenre = incomingDataState.currentGenre;
        Serial.print(">>> GENRE: "); Serial.println(activeGenre);
    }
  }
  else if (len == sizeof(StreamingPacket)) {
    memcpy(&incomingDataSP, incoming, sizeof(incomingDataSP));
    musicPlaying = true;
    long sum = 0;
    for(int i=0; i<240; i+=2) sum += abs((int)incomingDataSP.audioData[i] - 128);
    currentVolume = sum / 120; 
    xStreamBufferSendFromISR(audioBuffer, incomingDataSP.audioData, 240, NULL);
  }
}

void displayTask(void *pvParameters) {
    for (;;) { 
        uint8_t requestedFace = incomingDataState.faceIdx;
        if (requestedFace == 0) requestedFace = 4;
        
        if (currentFace != requestedFace) {
            currentFace = requestedFace;
            drawFace(currentFace); 
        }

        if (millis() - lastRecvTime < 1000 && musicPlaying) {
            updateVisualizerFromAudio();
            screenIsBlack = false;
        } else {
            if (!screenIsBlack) {
                 clearSmall(COLOR_BLACK);
                 triggerStrobe(false); strobeActive = false;
                 for(int i=0; i<NUM_BANDS; i++) { bandLevels[i]=0; prevBandLevels[i]=0; }
                 screenIsBlack = true;
                 
                 // Cand muzica se opreste, redesenam fata o data pentru a curata eventuale glitch-uri
                 drawFace(currentFace);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15)); 
    }
}

void setup() {
  Serial.begin(115200);
  
  // SPI - 1MHz pentru siguranta
  spi.begin(18, -1, 23, -1);
  spi.setFrequency(1000000); 
  spi.setDataMode(SPI_MODE0); spi.setBitOrder(MSBFIRST);
  
  pinMode(SR_LATCH, OUTPUT); pinMode(TFT_WR, OUTPUT); pinMode(TFT_RS, OUTPUT);
  pinMode(TFT_RST, OUTPUT); 
  pinMode(TFT_CS_SMALL, OUTPUT); pinMode(TFT_CS_BIG, OUTPUT);
  
  digitalWrite(TFT_CS_SMALL, HIGH); digitalWrite(TFT_CS_BIG, HIGH);
  digitalWrite(TFT_WR, HIGH); digitalWrite(TFT_RS, HIGH); digitalWrite(SR_LATCH, HIGH);
  digitalWrite(TFT_RST, HIGH); delay(50); digitalWrite(TFT_RST, LOW); delay(50); digitalWrite(TFT_RST, HIGH); delay(200);
  
  initSmallScreen();
  initBigScreen();
  
  // Test scurt
  clearSmall(COLOR_RED); delay(200); clearSmall(COLOR_BLACK);
  clearBig(COLOR_BLUE); delay(200); clearBig(COLOR_BLACK);

  WiFi.mode(WIFI_STA);
  esp_wifi_set_max_tx_power(84);
  esp_wifi_set_ps(WIFI_PS_NONE);
  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(OnDataRecv);

  audioBuffer = xStreamBufferCreate(8192, 1);
  dac_oneshot_config_t dac_conf = { .chan_id = DAC_CHAN_0 };
  dac_oneshot_new_channel(&dac_conf, &dac_left); dac_oneshot_output_voltage(dac_left, 128);
  dac_conf.chan_id = DAC_CHAN_1;
  dac_oneshot_new_channel(&dac_conf, &dac_right); dac_oneshot_output_voltage(dac_right, 128);
  const esp_timer_create_args_t timer_args = { .callback = &onAudioTimer, .name = "audio_timer" };
  esp_timer_create(&timer_args, &audio_timer);
  esp_timer_start_periodic(audio_timer, 62);
  
  xTaskCreatePinnedToCore(displayTask, "DisplayTask", 4096, NULL, 1, NULL, 1);
}

void loop() { delay(10); }