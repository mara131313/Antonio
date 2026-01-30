#include "config.h"
#include "protocol.h"
#include "feedbackHandler.h"
#include "inputHandler.h"
#include "displayHandler.h"

RemoteState myRemote;  // the global state object

int batterySamples[5]; 
int sampleIndex = 0;

void setup() {
  Serial.begin(BAUD_RATE);

  setupDisplay();
  setupInputs();
  setupFeedback();

  Serial.println("ANTONIO Remote Initialized...");
}

void loop() {
  updateInputs(myRemote);
  updateDisplay(myRemote);

  static unsigned long lastPrint = 0, lastSampleTime = 0;
  if (millis() - lastPrint > 300) { // shows stats
    Serial.print("MODE: ");
    if (myRemote.isTesting) Serial.print("TESTING ");
    else Serial.print("IDLE ");

    Serial.printf("| Part: %d | Face: %d | Drive: %d | Steer: %d | Speed: %d\n",
      myRemote.testPart, myRemote.faceIdx, myRemote.drive, myRemote.steer, myRemote.speed);
    lastPrint = millis();
  }
  if (millis() - lastSampleTime > 2000) { // for calculating and showing the battery percentage
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
  }
}