#include "globals.h"
#include "inputHandler.h"
#include "displayHandler.h"
#include "commsHandler.h"
#include <esp_now.h>
#include <WiFi.h>

// Shared Global State and Queue
RemoteState myRemote;
DriveCommand myDc;
QueueHandle_t commsQueue;
EventGroupHandle_t commsEvents;
StreamingPacket myStreaming;
SemaphoreHandle_t xTransmitSemaphore;

int batterySamples[5];
int sampleIndex = 0;
unsigned long lastSampleTime = 0;

// --- Helper: Feedback (formerly feedbackHandler.h) ---
void triggerBeep(int freq, int duration) {
  tone(BUZZER_PIN, freq, duration);
}

// --- TASK 1: UI, Inputs, and Display (CORE 1) ---
void uiTask(void *pvParameters) {
  // --- ONE-TIME SETUP FOR THIS TASK ---
  setupInputs();
  Serial2.begin(NEXTION_BAUD, SERIAL_8N1, RXD2, TXD2);
  digitalWrite(DISPLAY_POWER_PIN, HIGH);

  for (;;) {
    // --- REPEATED LOGIC ---
    updateInputs(myRemote);
    updateDisplay(myRemote);
    espDeepSleep();

    xQueueOverwrite(commsQueue, &myRemote);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
// --- TASK 2: Comms & Battery (CORE 0) ---
void commsTask(void *pvParameters) {
  setupComms();
  esp_now_register_send_cb(OnDataSent);
  uint32_t lastBatteryCheck = 0;

  for (;;) {
    EventBits_t bits = xEventGroupWaitBits(commsEvents,
                                           EVENT_SEND_REMOTE_STATE | EVENT_SEND_STREAMING,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(10));

    if (bits & EVENT_SEND_REMOTE_STATE) {
      esp_now_send(robotAddress, (uint8_t *)&myRemote, sizeof(myRemote));
      Serial.println("Sent: RemoteState (Settings changed)");
    }

    if (bits & EVENT_SEND_STREAMING) {
      esp_now_send(robotAddress, (uint8_t *)&myStreaming, sizeof(myStreaming));
      Serial.println("Sent: StreamingPacket (ID/Audio changed)");
    }
    if (!myRemote.musicPlaying) {
      esp_now_send(robotAddress, (uint8_t *)&myRemote, sizeof(myRemote));
    }

    // handle battery
    if (millis() - lastSampleTime > 2000) {  // for calculating and showing the battery percentage
      batterySamples[sampleIndex] = getBatteryPercentage();
      sampleIndex++;
      lastSampleTime = millis();

      if (sampleIndex >= 5) {
        int sum = 0;
        for (int i = 0; i < 5; i++) {
          sum += batterySamples[i];
        }
        int averagePercentage = sum / 5;

        // Trimitem media către Nextion
        sendBatteryIconToNextion(averagePercentage);

        // Resetăm indexul pentru a începe o nouă medie
        sampleIndex = 0;

        Serial.print("--- UPDATE BATERIE (Media pe 10s): ");
        Serial.print(averagePercentage);
        Serial.println("% ---");
      }
      vTaskDelay(pdMS_TO_TICKS(10));  // Keep radio responsive
    }
  }
}


void audioSenderTask(void *pvParameters) {
    uint8_t audioBuffer[240];
    
    // ✅ MODIFICAT: 7ms în loc de 15ms pentru MONO 16kHz
    // Calcul: 32000 bytes/sec ÷ 240 bytes = 133 packets/sec → 7.5ms per packet
    const TickType_t xFrequency = pdMS_TO_TICKS(7);
    
    TickType_t xLastWakeTime;

    for(;;) {
        if (!myRemote.musicPlaying) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (myRemote.trackID >= totalSongs) {
            Serial.println("Error: Invalid Track ID");
            myRemote.musicPlaying = false;
            continue;
        }

        char path[70];
        snprintf(path, sizeof(path), "/%s", songList[myRemote.trackID]);
        
        File audioFile = SD.open(path);
        if (!audioFile) {
            Serial.printf("Error: Could not open %s\n", path);
            myRemote.musicPlaying = false;
            continue;
        }

        audioFile.seek(44);
        Serial.printf("🎵 Playing: %s\n", path);

        xLastWakeTime = xTaskGetTickCount();
        uint32_t packetCounter = 0; // ✅ ADĂUGAT: Numerotare pachete

        while (audioFile.available() && myRemote.musicPlaying) {
            
            size_t bytesRead = audioFile.read(audioBuffer, 240);

            memcpy(myStreaming.audioData, audioBuffer, sizeof(audioBuffer));
            myStreaming.dc = myRemote.dc;
            myStreaming.driveControl = false;
            myStreaming.packetId = packetCounter++; // ✅ ADĂUGAT
            
            esp_err_t result = esp_now_send(robotAddress, (uint8_t *)&myStreaming, sizeof(StreamingPacket));
            
            // ✅ ADĂUGAT: Debug pentru erori de trimitere
            if (result != ESP_OK) {
                Serial.printf("❌ SEND FAILED! Error: %d\n", result);
            }

            if (xSemaphoreTake(xTransmitSemaphore, pdMS_TO_TICKS(50)) != pdTRUE) {
                Serial.println("⚠️ Radio Congestion!");
            }

            vTaskDelayUntil(&xLastWakeTime, xFrequency);
        }

        audioFile.close();
        
        if (myRemote.musicPlaying) {
             Serial.println("✅ Song Finished.");
             myRemote.musicPlaying = false;
        } else {
             Serial.println("🛑 Playback Stopped by User.");
        }
    }
}

void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  // Note: tx_info->dest_addr contains the MAC it was sent to

  // BaseType_t is the correct type (no 'x' at the start)
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  // Give the semaphore to signal the task to send the next packet
  xSemaphoreGiveFromISR(xTransmitSemaphore, &xHigherPriorityTaskWoken);

  // If giving the semaphore unblocked a higher priority task, yield the CPU
  if (xHigherPriorityTaskWoken) {
    portYIELD_FROM_ISR();
  }
}

void setup() {
  Serial.begin(BAUD_RATE);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(DISPLAY_POWER_PIN, OUTPUT);
  digitalWrite(DISPLAY_POWER_PIN, HIGH);

  myRemote.musicPlaying = false;

  if (!SD.begin(5)) {
    Serial.println("CRITICAL ERROR: SD Card Mount Failed!");
    Serial.println("Audio features will be disabled to prevent crash.");
    // Stop here or set a flag
    return;
  }

  Serial.println("SD Mounted Successfully.");
  scanSDCard();

  // Queue only needs to hold the "most recent" state
  commsQueue = xQueueCreate(1, sizeof(RemoteState));
  commsEvents = xEventGroupCreate();
  xTransmitSemaphore = xSemaphoreCreateBinary();
  if (commsQueue != NULL) {
    xTaskCreatePinnedToCore(commsTask, "Comms", 4000, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(uiTask, "UI", 4000, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(audioSenderTask, "Audio", 4000, NULL, 3, NULL, 0);
  }
}

void loop() {
  // FreeRTOS is running the show; loop stays empty.
  vTaskDelete(NULL);
}