#include "FS.h"
#include "SD.h"
#include "SPI.h"
const int xMinThreshold = 1800, xMaxThreshold = 2400;
const int yMinThreshold = 1800, yMaxThreshold = 2400;

unsigned long lastSwPressTime = 0;
const long debounceDelay = 50;
bool swState = HIGH;
unsigned long pressStartTime = 0;
bool isPressed = false;
char songList[MAX_SONGS][MAX_NAME_LEN];
int totalSongs = 0;

void setupInputs() {
  pinMode(JOYSTICK_X_PIN, INPUT);
  pinMode(JOYSTICK_Y_PIN, INPUT);
  pinMode(JOYSTICK_SW_PIN, INPUT_PULLUP);
  pinMode(PUSH_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DISPLAY_POWER_PIN, OUTPUT);
  digitalWrite(DISPLAY_POWER_PIN, HIGH);

}

void espDeepSleep() {
  if (digitalRead(PUSH_BUTTON_PIN) == LOW) {
    if (!isPressed) {
      digitalWrite(DISPLAY_POWER_PIN, HIGH);
      pressStartTime = millis();
      isPressed = true;
    }

    if (millis() - pressStartTime > SLEEP_THRESHOLD) {
      Serial.println("Shutting down...");
      esp_sleep_enable_ext0_wakeup(PUSH_BUTTON_PIN, 0);

      while (digitalRead(PUSH_BUTTON_PIN) == LOW)
        ;

      esp_deep_sleep_start();
    }
  } else {
    isPressed = false;
  }
}

void scanSDCard() {
    totalSongs = 0;
    File root = SD.open("/");
    if (!root) return;

    File file = root.openNextFile();
    while (file && totalSongs < MAX_SONGS) {
        if (!file.isDirectory()) {
            const char* name = file.name();
            // Skip leading slash if library adds it
            if (name[0] == '/') name++; 

            if (String(name).endsWith(".wav") || String(name).endsWith(".WAV")) {
                // Safely copy the string to our fixed buffer
                strncpy(songList[totalSongs], name, MAX_NAME_LEN - 1);
                songList[totalSongs][MAX_NAME_LEN - 1] = '\0'; // Ensure null termination
                
                Serial.printf("Stored Index %d: %s\n", totalSongs, songList[totalSongs]);
                totalSongs++;
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    Serial.println("Scan function finished successfully.");
}

float getBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);
  return (raw / 4095.0) * 3.3 * 2.0;
}

int getBatteryPercentage() {
  float voltage = getBatteryVoltage();

  int percentage;

  // LOGICA DE CALIBRARE PENTRU BMS-ul "ZOMBIE"
  if (voltage >= 3.6) {
    // Daca citesti peste 3.6V, e clar ca bateria e in zona buna (Medie-Plina)
    percentage = 100;
  } else if (voltage <= 3.1) {
    // Daca scade sub 3.1V, e goala
    percentage = 0;
  } else {
    // Mapam liniar intervalul 3.1V - 3.6V
    // Asta e singura zona unde BMS-ul tau e sincer
    percentage = map(voltage * 100, 300, 370, 0, 100);
  }
  return percentage;
}

void updateInputs(RemoteState &state) {
  int xValue = analogRead(JOYSTICK_X_PIN);
  int yValue = analogRead(JOYSTICK_Y_PIN);
  int currentSwState = digitalRead(JOYSTICK_SW_PIN);

  if (xValue < xMinThreshold) state.dc.steer = -1;      // left
  else if (xValue > xMaxThreshold) state.dc.steer = 1;  // right
  else state.dc.steer = 0;                              // center

  if (yValue < yMinThreshold) state.dc.drive = 1;        // up/forward
  else if (yValue > yMaxThreshold) state.dc.drive = -1;  // down/backward
  else state.dc.drive = 0;                               // center

  // debounce SW button
  if (currentSwState != swState) {
    if (millis() - lastSwPressTime > debounceDelay) {
      swState = currentSwState;
      lastSwPressTime = millis();

      // Detectăm doar momentul apăsării (trecerea în LOW)
      if (swState == HIGH) {
        state.isSwPressed = !state.isSwPressed; // Toggle: din true devine false și invers
        triggerBeep(1000, 50); // Feedback sonor scurt pentru toggle
        
        // Notificăm task-ul de comunicație că s-a schimbat ceva în RemoteState
        xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE);
        
        Serial.print("Joystick SW Toggle: ");
        Serial.println(state.isSwPressed ? "ON" : "OFF");
      }
    }
  }

  // debounce push button
  int currentPushButtonState = digitalRead(PUSH_BUTTON_PIN);
  static unsigned long lastPushTime = 0;
  static bool pushState = HIGH;

  if (currentPushButtonState != pushState) {
    if (millis() - lastPushTime > 50) {
      pushState = currentPushButtonState;
      lastPushTime = millis();
      if (pushState == LOW) {
        Serial.println("Push Button Extra Apasat!");
        triggerBeep(1500, 100);
      }
    }
  }
}