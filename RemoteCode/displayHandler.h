#include <HardwareSerial.h>

const int songImages[] = {SONG_0, SONG_1, SONG_2, SONG_3, SONG_4, SONG_5};
const int numSongs = 6;
bool isToggleOn = false, isThemeOn = true;
int scrollOffset = 0;
int themeVolume = 120;

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

  Serial2.print("batt_icon.pic="); Serial2.print(imageId);
  Serial2.write(0xff); Serial2.write(0xff); Serial2.write(0xff);
}

void sendRandomSongToNextion() {
  static int lastIndex = -1;
  int randomIndex;
  if (numSongs > 1) {
    do { randomIndex = random(0, numSongs); } while (randomIndex == lastIndex);
  } else { randomIndex = 0; }
  lastIndex = randomIndex;
  int selectedImageId = songImages[randomIndex];

  Serial2.print("pic_song.pic="); Serial2.print(selectedImageId);
  Serial2.write(0xff); Serial2.write(0xff); Serial2.write(0xff);
}

void updateSongNameOnNextion(int buttonIdx) {
  int realIdx = buttonIdx + scrollOffset;
  String displayName;
  if (realIdx >= 0 && realIdx < totalSongs) { 
    String rawName = String(songList[realIdx]);
    int dotIndex = rawName.lastIndexOf('.');
    if (dotIndex > 0) rawName = rawName.substring(0, dotIndex);

    String checkName = rawName;
    checkName.toUpperCase();
    if (checkName.endsWith("_POP") || checkName.endsWith("_RAP") || 
        checkName.endsWith("_ROC") || checkName.endsWith("_MAN") || 
        checkName.endsWith("_EDM")) {
        rawName = rawName.substring(0, rawName.length() - 4); 
    }
    displayName = rawName; 
  } else {
    displayName = "---";
  }
  Serial2.print("song"); Serial2.print(buttonIdx); Serial2.print(".txt=\"");
  Serial2.print(displayName); Serial2.print("\"");
  Serial2.write(0xff); Serial2.write(0xff); Serial2.write(0xff);
}

void updateAllVisibleSongs() {
  for (int i = 0; i < 10; i++) updateSongNameOnNextion(i);
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

void updateDisplay(RemoteState &remote) {
  if (Serial2.available()) {
    String data = Serial2.readStringUntil('\n');
    data.trim();
    Serial.print("Raw: ["); Serial.print(data); Serial.println("]");

    if (data.indexOf("PAGE_LOAD") >= 0) {
      int quickBatt = getBatteryPercentage(); 
      sendBatteryIconToNextion(quickBatt);
      remote.isTesting = false;
      scrollOffset = 0;
      Serial2.print("song_slider.val=10"); Serial2.write(0xff); Serial2.write(0xff); Serial2.write(0xff);
      updateAllVisibleSongs(); 
      xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE);
    }
    else if (data.indexOf("SONG_SLIDE=") >= 0) {
      int sliderVal = (int)data[11]; 
      if (totalSongs <= 10) scrollOffset = 0;
      else {
        int maxScrollSteps = totalSongs - 10;
        float ratio = (float)(10 - sliderVal) / 10.0;
        scrollOffset = (int)(ratio * maxScrollSteps + 0.5);
      }
      if (scrollOffset < 0) scrollOffset = 0;
      if (scrollOffset > (totalSongs - 10)) scrollOffset = max(0, totalSongs - 10);
      updateAllVisibleSongs();
    }
    else if (data.indexOf("SONG_PLAY") >= 0) {
      isToggleOn = !isToggleOn;
      remote.musicPlaying = isToggleOn;
      updateNextionButton("play", isToggleOn, SONG_PAUSE, SONG_PLAY);
      xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); 
    }
    // NEXT SONG
    else if (data.indexOf("SONG_PREV") >= 0 || data.indexOf("SONG_SKIP") >= 0 || data.startsWith("SONG")) {
      triggerBeep(2000, 100);
      if (data.indexOf("SONG_SKIP") >= 0) remote.trackID = (remote.trackID + 1) % totalSongs;
      else if (data.indexOf("SONG_PREV") >= 0) remote.trackID = (remote.trackID - 1 + totalSongs) % totalSongs;
      else if (data.startsWith("SONG") && isDigit(data[4])) {
        int buttonIdx = data.substring(4).toInt();
        remote.trackID = buttonIdx + scrollOffset;
      }
      if (remote.trackID >= totalSongs) remote.trackID = totalSongs - 1;
      if (remote.trackID < 0) remote.trackID = 0;

      // UPDATE CRITIC GENRE
      remote.currentGenre = songGenres[remote.trackID];
      myStreaming.packetId = remote.trackID;
      
      xEventGroupSetBits(commsEvents, EVENT_SEND_STREAMING);
      xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); // Pentru sincronizare instanta

      String rawName = String(songList[remote.trackID]);
      int dotIndex = rawName.lastIndexOf('.');
      if (dotIndex > 0) rawName = rawName.substring(0, dotIndex);
      String checkTitle = rawName;
      checkTitle.toUpperCase();
      if (checkTitle.endsWith("_POP") || checkTitle.endsWith("_RAP") || checkTitle.endsWith("_ROC") || checkTitle.endsWith("_MAN") || checkTitle.endsWith("_EDM")) {
          rawName = rawName.substring(0, rawName.length() - 4);
      }
      Serial2.print("song_title.txt=\""); Serial2.print(rawName); Serial2.print("\"");
      Serial2.write(0xff); Serial2.write(0xff); Serial2.write(0xff);
      sendRandomSongToNextion();
    }
    
    // Test Modes
    else if (data.indexOf("T_LEGS") >= 0)  { 
      if (digitalRead(JOYSTICK_SW_PIN) == LOW) {
        remote.testPart = 1; remote.isTesting = true; xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE);
      } 
    }
    else if (data.indexOf("T_WRIST") >= 0) { remote.testPart = 2; xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); }
    else if (data.indexOf("T_SHLD") >= 0)  { remote.testPart = 3; xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); }
    else if (data.indexOf("T_HEAD") >= 0)  { remote.testPart = 4; xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); }
    else if (data.indexOf("T_TAIL") >= 0)  { remote.testPart = 5; xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); }    
    else if (data.indexOf("START") >= 0) { 
      remote.isTesting = true; triggerBeep(2000, 100); 
      updateNextionButton("bstart", true, TEST_PLAY_ON, TEST_PLAY_OFF);
      updateNextionButton("bstop", false, TEST_STOP_ON, TEST_STOP_OFF);
      xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE);
    }
    else if (data.indexOf("STOP") >= 0)  { 
      remote.isTesting = false; triggerBeep(1000, 100); 
      updateNextionButton("bstart", false, TEST_PLAY_ON, TEST_PLAY_OFF);
      updateNextionButton("bstop", true, TEST_STOP_ON, TEST_STOP_OFF);
      xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE);
    }

    //THEME ON/OFF + VOLUME
    else if (data.indexOf("THEME_ON") >= 0) {
      isThemeOn = !isThemeOn;
      if (!isThemeOn) noTone(BUZZER_PIN);
      updateNextionButton("bthemeon", isThemeOn, THEME_ON, THEME_OFF);
    }
    else if (data.indexOf("REMO_VOL=") >= 0) {
      int pos = data.indexOf("="); 
      int rawVol = (unsigned char)data[pos + 1]; 
      Serial.printf("DEBUG Slider REMO_VOL: %d\n", rawVol);
      themeVolume = map(rawVol, 0, 10, 1, 120);      
      Serial.printf("Volume Theme Actualizat: %d\n", themeVolume);
    }
    
    // Faces
    else if (data.indexOf("FACE_HAPPY") >= 0) { remote.faceIdx = 1; updateNextionButton("bhappy", true, HAPPY_ON, HAPPY_OFF); updateNextionButton("bplayful", false, PLAYFUL_ON, PLAYFUL_OFF); updateNextionButton("bheart", false, HEART_ON, HEART_OFF); updateNextionButton("bdefault", false, DEFAULT_ON, DEFAULT_OFF); xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); }
    else if (data.indexOf("FACE_PLAYFUL") >= 0) { remote.faceIdx = 2; updateNextionButton("bhappy", false, HAPPY_ON, HAPPY_OFF); updateNextionButton("bplayful", true, PLAYFUL_ON, PLAYFUL_OFF); updateNextionButton("bheart", false, HEART_ON, HEART_OFF); updateNextionButton("bdefault", false, DEFAULT_ON, DEFAULT_OFF); xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); }
    else if (data.indexOf("FACE_HEART") >= 0) { remote.faceIdx = 3; updateNextionButton("bhappy", false, HAPPY_ON, HAPPY_OFF); updateNextionButton("bplayful", false, PLAYFUL_ON, PLAYFUL_OFF); updateNextionButton("bheart", true, HEART_ON, HEART_OFF); updateNextionButton("bdefault", false, DEFAULT_ON, DEFAULT_OFF); xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); }
    else if (data.indexOf("FACE_DEFAULT") >= 0) { remote.faceIdx = 4; updateNextionButton("bhappy", false, HAPPY_ON, HAPPY_OFF); updateNextionButton("bplayful", false, PLAYFUL_ON, PLAYFUL_OFF); updateNextionButton("bheart", false, HEART_ON, HEART_OFF); updateNextionButton("bdefault", true, DEFAULT_ON, DEFAULT_OFF); xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); }
    
    // Speed
    else if (data.indexOf("SPEED_SLOW") >= 0) { remote.speed = 0; updateNextionButton("bslow", true, SLOW_ON, SLOW_OFF); updateNextionButton("bmed", false, MED_ON, MED_OFF); updateNextionButton("bfast", false, FAST_ON, FAST_OFF); xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); }
    else if (data.indexOf("SPEED_MED") >= 0)  { remote.speed = 1; updateNextionButton("bslow", false, SLOW_ON, SLOW_OFF); updateNextionButton("bmed", true, MED_ON, MED_OFF); updateNextionButton("bfast", false, FAST_ON, FAST_OFF); xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); }
    else if (data.indexOf("SPEED_FAST") >= 0) { remote.speed = 2; updateNextionButton("bslow", false, SLOW_ON, SLOW_OFF); updateNextionButton("bmed", false, MED_ON, MED_OFF); updateNextionButton("bfast", true, FAST_ON, FAST_OFF); xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE); }
  
    // robot settings
    else if (data.indexOf("VOL=") >= 0) {
      int pos = data.indexOf("="); 
      int rawVol = (unsigned char)data[pos + 1];
      remote.volume = constrain(rawVol, 1, 10);
      xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE);
    }
    else if (data.indexOf("BRIGHT=") >= 0) {
      int pos = data.indexOf("="); 
      int rawBright = (unsigned char)data[pos + 1];
      remote.brightness = constrain(rawBright, 1, 10);
      xEventGroupSetBits(commsEvents, EVENT_SEND_REMOTE_STATE);
    }
  }
}