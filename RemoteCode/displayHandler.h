#include <HardwareSerial.h>

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

void updateDisplay(RemoteState &state) {
  if (Serial2.available()) {
    String data = Serial2.readStringUntil('\n');
    data.trim();

    Serial.print("Raw: ["); Serial.print(data); Serial.println("]");

    if (data.indexOf("T_LEGS") >= 0)  { state.testPart = 1; Serial.println("OK: LEGS"); }
    else if (data.indexOf("T_WRIST") >= 0) { state.testPart = 2; Serial.println("OK: WRIST"); }
    else if (data.indexOf("T_SHLD") >= 0)  { state.testPart = 3; Serial.println("OK: SHLD"); }
    else if (data.indexOf("T_HEAD") >= 0)  { state.testPart = 4; Serial.println("OK: HEAD"); }
    else if (data.indexOf("T_TAIL") >= 0)  { state.testPart = 5; Serial.println("OK: TAIL"); }
    
    else if (data.indexOf("START") >= 0) { state.isTesting = true; triggerBeep(2000, 100); }
    else if (data.indexOf("STOP") >= 0)  { state.isTesting = false; triggerBeep(1000, 100); }
    
    else if (data.indexOf("FACE_HAPPY") >= 0)   { state.faceIdx = 1; Serial.println("OK: HAPPY"); }
    else if (data.indexOf("FACE_PLAYFUL") >= 0) { state.faceIdx = 2; Serial.println("OK: PLAYFUL"); }
    else if (data.indexOf("FACE_HEART") >= 0)   { state.faceIdx = 3; Serial.println("OK: HEART"); }
    else if (data.indexOf("FACE_DEFAULT") >= 0) { state.faceIdx = 4; Serial.println("OK: DEFAULT"); }
    
    else if (data.indexOf("SPEED_SLOW") >= 0) { state.speed = 0; }
    else if (data.indexOf("SPEED_MED") >= 0)  { state.speed = 1; }
    else if (data.indexOf("SPEED_FAST") >= 0) { state.speed = 2; }
    else if (data.indexOf("PAGE_LOAD") >= 0) {
      int quickBatt = getBatteryPercentage(); 
      sendBatteryIconToNextion(quickBatt);
      Serial.println("NEW PAGE DETECTED");
    }
  }
}