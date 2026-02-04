#include <HardwareSerial.h>

const int songImages[] = {SONG_0, SONG_1, SONG_2, SONG_3, SONG_4, SONG_5};
const int numSongs = 6;
bool isToggleOn = true, isThemeOn = true;

void setupDisplay() {
  Serial2.begin(NEXTION_BAUD, SERIAL_8N1, RXD2, TXD2);
}

void sendBatteryIconToNextion(int percent) {
  int imageId;

  if (percent <= 10) imageId = BATT_ICON_0_PERCENT;
  else if (percent <= 30) imageId = BATT_ICON_25_PERCENT;
  else if (percent <= 60) imageId = BATT_ICON_50_PERCENT;
  else if (percent <= 90) imageId = BATT_ICON_75_PERCENT;
  else imageId = BATT_ICON_100_PERCENT;

  Serial2.print("batt_icon.pic=");
  Serial2.print(imageId);
  Serial2.write(0xff); Serial2.write(0xff); Serial2.write(0xff);
  
  Serial.printf("Nextion Update: %d%% -> Imagine ID: %d\n", percent, imageId);
}

void sendRandomSongToNextion() {
  int randomIndex = random(0, numSongs);
  int selectedImageId = songImages[randomIndex];

  Serial2.print("pic_song.pic=");
  Serial2.print(selectedImageId);
  Serial2.write(0xff); Serial2.write(0xff); Serial2.write(0xff);

  Serial.printf("Nextion Update: Song Random ID: %d (Index: %d)\n", selectedImageId, randomIndex);
}

void updateNextionButton(String objName, bool state, int ON_ID, int OFF_ID) {
  int imgId = state ? ON_ID : OFF_ID;

  Serial2.print(objName + ".pic=" + String(imgId));
  Serial2.write(0xff); Serial2.write(0xff); Serial2.write(0xff);
  
  Serial2.print(objName + ".pic2=" + String(imgId));
  Serial2.write(0xff); Serial2.write(0xff); Serial2.write(0xff);
  
  Serial2.print("ref " + objName);
  Serial2.write(0xff); Serial2.write(0xff); Serial2.write(0xff);
}

void updateDisplay(RemoteState &state) {
  if (Serial2.available()) {
    String data = Serial2.readStringUntil('\n');
    data.trim();

    Serial.print("Raw: ["); Serial.print(data); Serial.println("]");

    // for showing + updating the battery
    if (data.indexOf("PAGE_LOAD") >= 0) {
      int quickBatt = getBatteryPercentage(); 
      sendBatteryIconToNextion(quickBatt);
      Serial.println("NEW PAGE DETECTED");
    }

    // for the music section
    else if (data.indexOf("SONG_PLAY") >= 0) {
      isToggleOn = !isToggleOn;
      updateNextionButton("play", isToggleOn, SONG_PAUSE, SONG_PLAY);
      Serial.println(isToggleOn ? "MUSIC ACTIVE" : "MUSIC PAUSED");
    }
    else if (data.indexOf("SONG_PREV") >= 0 || data.indexOf("SONG_SKIP") >= 0 || data.indexOf("SONG") >= 0) {
      triggerBeep(2000, 100);
      sendRandomSongToNextion();
      // trebuie adaugata si citirea si adaugarea titlurilor
      if( data.indexOf("SONG_SKIP") >= 0 ) {
        // primeste titlul urmator
      }
      if (data.indexOf("SONG_PREV") >= 0) {
        // primeste titlul anterior
      }
    }

    // for test modes
    else if (data.indexOf("T_LEGS") >= 0)  { state.testPart = 1; Serial.println("OK: LEGS"); }
    else if (data.indexOf("T_WRIST") >= 0) { state.testPart = 2; Serial.println("OK: WRIST"); }
    else if (data.indexOf("T_SHLD") >= 0)  { state.testPart = 3; Serial.println("OK: SHLD"); }
    else if (data.indexOf("T_HEAD") >= 0)  { state.testPart = 4; Serial.println("OK: HEAD"); }
    else if (data.indexOf("T_TAIL") >= 0)  { state.testPart = 5; Serial.println("OK: TAIL"); }
    
    else if (data.indexOf("START") >= 0 || (state.testPart == 1 /*&& swPressed */)) { 
      state.isTesting = true; 
      triggerBeep(2000, 100); 
      updateNextionButton("bstart", true, TEST_PLAY_ON, TEST_PLAY_OFF);
      updateNextionButton("bstop", false, TEST_STOP_ON, TEST_STOP_OFF);
    }
    else if (data.indexOf("STOP") >= 0)  { 
      state.isTesting = false; 
      triggerBeep(1000, 100); 
      updateNextionButton("bstart", false, TEST_PLAY_ON, TEST_PLAY_OFF);
      updateNextionButton("bstop", true, TEST_STOP_ON, TEST_STOP_OFF);
    }

    // for the remote settings
    else if (data.indexOf("THEME_ON") >= 0) {
      isThemeOn = !isThemeOn;
      updateNextionButton("bthemeon", isThemeOn, THEME_ON, THEME_OFF);
    }
    
    // for antonio's settings
    else if (data.indexOf("FACE_HAPPY") >= 0) { 
      state.faceIdx = 1; 
      Serial.println("OK: HAPPY"); 
      updateNextionButton("bhappy", true, HAPPY_ON, HAPPY_OFF);
      updateNextionButton("bplayful", false, PLAYFUL_ON, PLAYFUL_OFF);
      updateNextionButton("bheart", false, HEART_ON, HEART_OFF);
      updateNextionButton("bdefault", false, DEFAULT_ON, DEFAULT_OFF);
    }
    else if (data.indexOf("FACE_PLAYFUL") >= 0) { 
      state.faceIdx = 2; 
      Serial.println("OK: PLAYFUL"); 
      updateNextionButton("bhappy", false, HAPPY_ON, HAPPY_OFF);
      updateNextionButton("bplayful", true, PLAYFUL_ON, PLAYFUL_OFF);
      updateNextionButton("bheart", false, HEART_ON, HEART_OFF);
      updateNextionButton("bdefault", false, DEFAULT_ON, DEFAULT_OFF);
    }
    else if (data.indexOf("FACE_HEART") >= 0) { 
      state.faceIdx = 3; 
      Serial.println("OK: HEART"); 
      updateNextionButton("bhappy", false, HAPPY_ON, HAPPY_OFF);
      updateNextionButton("bplayful", false, PLAYFUL_ON, PLAYFUL_OFF);
      updateNextionButton("bheart", true, HEART_ON, HEART_OFF);
      updateNextionButton("bdefault", false, DEFAULT_ON, DEFAULT_OFF);
    }
    else if (data.indexOf("FACE_DEFAULT") >= 0) { 
      state.faceIdx = 4; 
      Serial.println("OK: DEFAULT"); 
      updateNextionButton("bhappy", false, HAPPY_ON, HAPPY_OFF);
      updateNextionButton("bplayful", false, PLAYFUL_ON, PLAYFUL_OFF);
      updateNextionButton("bheart", false, HEART_ON, HEART_OFF);
      updateNextionButton("bdefault", true, DEFAULT_ON, DEFAULT_OFF);
    }
    
    else if (data.indexOf("SPEED_SLOW") >= 0) { 
      state.speed = 0; 
      updateNextionButton("bslow", true, SLOW_ON, SLOW_OFF);
      updateNextionButton("bmed", false, MED_ON, MED_OFF);
      updateNextionButton("bfast", false, FAST_ON, FAST_OFF);
    }
    else if (data.indexOf("SPEED_MED") >= 0)  { 
      state.speed = 1; 
      updateNextionButton("bslow", false, SLOW_ON, SLOW_OFF);
      updateNextionButton("bmed", true, MED_ON, MED_OFF);
      updateNextionButton("bfast", false, FAST_ON, FAST_OFF);
    }
    else if (data.indexOf("SPEED_FAST") >= 0) { 
      state.speed = 2; 
      updateNextionButton("bslow", false, SLOW_ON, SLOW_OFF);
      updateNextionButton("bmed", false, MED_ON, MED_OFF);
      updateNextionButton("bfast", true, FAST_ON, FAST_OFF);
    }
  }
}