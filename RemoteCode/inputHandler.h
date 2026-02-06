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
  NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5,  
  NOTE_A4, NOTE_G4,                    
  0,                                   
  NOTE_G4, NOTE_C5                     
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
      while (digitalRead(PUSH_BUTTON_PIN) == LOW);
      esp_deep_sleep_start();
    }
  } else {
    isPressed = false;
  }
}

// --- SCANARE CU TAGURI (DE LA TINE) ---
void scanSDCard() {
    totalSongs = 0;
    File root = SD.open("/");
    if (!root) return;

    File file = root.openNextFile();
    while (file && totalSongs < MAX_SONGS) {
        if (!file.isDirectory()) {
            String fName = String(file.name());
            if (fName.startsWith("/")) fName.remove(0, 1); 

            if (fName.endsWith(".wav") || fName.endsWith(".WAV")) {
                String checkName = fName;
                checkName.toUpperCase();

                songGenres[totalSongs] = GENRE_DEFAULT;
                if (checkName.indexOf("_EDM.") > 0) songGenres[totalSongs] = GENRE_EDM;
                else if (checkName.indexOf("_POP.") > 0) songGenres[totalSongs] = GENRE_POP;
                else if (checkName.indexOf("_RAP.") > 0) songGenres[totalSongs] = GENRE_RAP;
                else if (checkName.indexOf("_ROC.") > 0) songGenres[totalSongs] = GENRE_ROC;
                else if (checkName.indexOf("_MAN.") > 0) songGenres[totalSongs] = GENRE_MAN;

                strncpy(songList[totalSongs], fName.c_str(), MAX_NAME_LEN - 1);
                songList[totalSongs][MAX_NAME_LEN - 1] = '\0'; 
                
                Serial.printf("Index %d: %s | GenreID: %d\n", totalSongs, songList[totalSongs], songGenres[totalSongs]);
                totalSongs++;
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
}

float getBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);
  return (raw / 4095.0) * 3.3 * 2.0;
}

int getBatteryPercentage() {
  float voltage = getBatteryVoltage();
  int percentage;
  if (voltage >= 3.6) percentage = 100;
  else if (voltage <= 3.1) percentage = 0;
  else percentage = map(voltage * 100, 300, 370, 0, 100);
  return percentage;
}

// --- THEME SONG (DE LA COLEGA) ---
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
    if (freq > 0) tone(BUZZER_PIN, freq, noteDurations[currentNote] * 0.8);
    else noTone(BUZZER_PIN);
    lastNoteTime = currentMillis;
  }
}

void updateInputs(RemoteState& state) {
  int xValue = analogRead(JOYSTICK_X_PIN);
  int yValue = analogRead(JOYSTICK_Y_PIN);
  int currentSwState = digitalRead(JOYSTICK_SW_PIN);

  if (xValue < xMinThreshold) state.dc.steer = -1;      
  else if (xValue > xMaxThreshold) state.dc.steer = 1;  
  else state.dc.steer = 0;                              

  if (yValue < yMinThreshold) state.dc.drive = 1;        
  else if (yValue > yMaxThreshold) state.dc.drive = -1; 
  else state.dc.drive = 0;                               

  if (currentSwState != swState) {
    if (millis() - lastSwPressTime > debounceDelay) {
      swState = currentSwState;
      lastSwPressTime = millis();
      if (swState == HIGH) {
        state.isSwPressed = !state.isSwPressed;
        triggerBeep(1000, 50);
        xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE);
        Serial.print("Joystick SW Toggle: "); Serial.println(state.isSwPressed ? "ON" : "OFF");
      }
    }
  }

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
  playThemeSong(state.musicPlaying);
}