const int xMinThreshold = 1800, xMaxThreshold = 2400;
const int yMinThreshold = 1800, yMaxThreshold = 2400;

unsigned long lastSwPressTime = 0;
const long debounceDelay = 50;
bool swState = HIGH; 
unsigned long pressStartTime = 0;
bool isPressed = false;

void setupInputs() {
  pinMode(JOYSTICK_X_PIN, INPUT);
  pinMode(JOYSTICK_Y_PIN, INPUT);
  pinMode(JOYSTICK_SW_PIN, INPUT_PULLUP);
  pinMode(PUSH_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DISPLAY_POWER_PIN, OUTPUT);
  digitalWrite(DISPLAY_POWER_PIN, HIGH);
}

void espDeepSleep(){
  if (digitalRead(PUSH_BUTTON_PIN) == LOW) {
    if (!isPressed) {
      digitalWrite(DISPLAY_POWER_PIN, HIGH);
      pressStartTime = millis();
      isPressed = true;
    }
    
    if (millis() - pressStartTime > SLEEP_THRESHOLD) {
      Serial.println("Shutting down...");
      esp_sleep_enable_ext0_wakeup(PUSH_BUTTON_PIN, 0);
      
      while(digitalRead(PUSH_BUTTON_PIN) == LOW); 
      
      esp_deep_sleep_start();
    }
  } else {
    isPressed = false;
  }
}

float getBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);
  return (raw / 4095.0) * 3.3 * 2.0 + 0.7; 
}

int getBatteryPercentage() {
  float v = getBatteryVoltage();
  Serial.println(v);
  if (v < 3.0) return 0;
  if (v > 4.1) return 100;
  return (int)(v / 4.2 * 100);
}

void updateInputs(RemoteState &state) {
  int xValue = analogRead(JOYSTICK_X_PIN);
  int yValue = analogRead(JOYSTICK_Y_PIN);
  int currentSwState = digitalRead(JOYSTICK_SW_PIN);
  
  if (xValue < xMinThreshold) state.dc.steer = -1;      // left
  else if (xValue > xMaxThreshold) state.dc.steer = 1;  // right
  else state.dc.steer = 0;                              // center

  if (yValue < yMinThreshold) state.dc.drive = 1;       // up/forward
  else if (yValue > yMaxThreshold) state.dc.drive = -1; // down/backward
  else state.dc.drive = 0;                              // center

  // debounce SW button
  if (currentSwState != swState) {
    if (millis() - lastSwPressTime > debounceDelay) {
      swState = currentSwState;
      lastSwPressTime = millis();

      if (swState == LOW) {
        state.isTesting = false; 
        Serial.println("SW Pressed - Testing STOPPED");
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