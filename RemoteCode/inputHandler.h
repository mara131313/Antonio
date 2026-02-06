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

extern bool isThemeOn;
int themeMelody[] = {
  NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5,  // Urcare veselă
  NOTE_A4, NOTE_G4,                    // Coborâre lină
  0,                                   // Pauză mică
  NOTE_G4, NOTE_C5                     // Final optimist
};
int noteDurations[] = {
  300, 300, 300, 450,
  300, 300,
  200,
  300, 600
};
int currentNote = 0;
unsigned long lastNoteTime = 0;

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
        songList[totalSongs][MAX_NAME_LEN - 1] = '\0';  // Ensure null termination

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


void playThemeSong(bool musicIsPlaying) {
  if (!isThemeOn || musicIsPlaying) {
    noTone(BUZZER_PIN);
    return;
  }

  unsigned long currentMillis = millis();

  if (currentMillis - lastNoteTime >= (unsigned long)noteDurations[currentNote]) {
    currentNote++;
    if (currentNote >= 9) {
      currentNote = 0;
      lastNoteTime = currentMillis + 800;
      noTone(BUZZER_PIN);
      return;
    }

    int freq = themeMelody[currentNote];
    if (freq > 0) {
      // "Secretul" pentru un sunet curat:
      // Folosim un multiplicator de 0.8 pentru a lăsa spațiu între note (Legato -> Staccato)
      tone(BUZZER_PIN, freq, noteDurations[currentNote] * 0.8);
    } else {
      noTone(BUZZER_PIN);
    }

    lastNoteTime = currentMillis;
  }
}
// void playThemeSong(bool musicIsPlaying) {
//   // Condiția ta: Cântă doar dacă Theme e ON ȘI nu rulează muzica pe robot
//   if (!isThemeOn || musicIsPlaying) {
//     noTone(BUZZER_PIN); // Asigură-te că buzzer-ul tace
//     return;
//   }

//   unsigned long currentMillis = millis();
//   int pauseBetweenNotes = themeMelody[currentNote] * 1.3; // Mică pauză între note

//   if (currentMillis - lastNoteTime >= (unsigned long)noteDurations[currentNote]) {
//     // Trecem la nota următoare
//     currentNote++;
//     if (currentNote >= 8) currentNote = 0; // O luăm de la capăt (loop)

//     tone(BUZZER_PIN, themeMelody[currentNote], noteDurations[currentNote]);
//     lastNoteTime = currentMillis;
//   }
// }

void updateInputs(RemoteState& state) {
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

      if (swState == HIGH) {
        state.isSwPressed = !state.isSwPressed;
        triggerBeep(1000, 50);
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
        Serial.println("Push Button Extra!");
        triggerBeep(1500, 100);
      }
    }
  }

  // theme song
  playThemeSong(state.musicPlaying);
}