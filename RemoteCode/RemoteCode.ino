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

    for(;;) {
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
    setupComms(); // Initialize ESP-NOW
    RemoteState localState;
    DriveCommand localDc;
    uint32_t lastBatteryCheck = 0;

    for(;;) {
        // 1. Handle Outgoing Transmission
        if (xQueueReceive(commsQueue, &localState, 0)) {
            if(!localState.musicPlaying)
                esp_now_send(robotAddress, (uint8_t *) &localState.dc, sizeof(localState.dc));
            else
                esp_now_send(robotAddress, (uint8_t *) &localState, sizeof(localState));
        }

        // 2. Handle Battery (Every 10 seconds)
        if (millis() - lastBatteryCheck > 10000) {
            int pct = getBatteryPercentage();
            sendBatteryIconToNextion(pct);
            lastBatteryCheck = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // Keep radio responsive
    }
}

void setup() {
    Serial.begin(BAUD_RATE);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(DISPLAY_POWER_PIN, OUTPUT);
    digitalWrite(DISPLAY_POWER_PIN, HIGH);

    // Queue only needs to hold the "most recent" state
    commsQueue = xQueueCreate(1, sizeof(RemoteState));

    if (commsQueue != NULL) {
        xTaskCreatePinnedToCore(commsTask, "Comms", 4000, NULL, 2, NULL, 0);
        xTaskCreatePinnedToCore(uiTask, "UI", 4000, NULL, 1, NULL, 1);
    }
}

void loop() { 
    // FreeRTOS is running the show; loop stays empty.
    vTaskDelete(NULL); 
}