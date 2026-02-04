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
  setupComms();  // Initialize ESP-NOW
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
    Serial.printf("MUSIC PLAYING: %d\n", myRemote.musicPlaying);
    if (!myRemote.musicPlaying) {
      esp_now_send(robotAddress, (uint8_t *)&myRemote, sizeof(myRemote));
      Serial.println("am trimis cacatu");
    } else {
      esp_now_send(robotAddress, (uint8_t *)&myStreaming, sizeof(myStreaming));
      Serial.println("am trimis pisatu");
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

void setup() {
  Serial.begin(BAUD_RATE);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(DISPLAY_POWER_PIN, OUTPUT);
  digitalWrite(DISPLAY_POWER_PIN, HIGH);

  myRemote.musicPlaying = false;

  // Queue only needs to hold the "most recent" state
  commsQueue = xQueueCreate(1, sizeof(RemoteState));

  commsEvents = xEventGroupCreate();

  if (commsQueue != NULL) {
    xTaskCreatePinnedToCore(commsTask, "Comms", 4000, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(uiTask, "UI", 4000, NULL, 1, NULL, 1);
  }
}

void loop() {
  // FreeRTOS is running the show; loop stays empty.
  vTaskDelete(NULL);
}