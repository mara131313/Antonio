const int xMinThreshold = 1800, xMaxThreshold = 2400;
const int yMinThreshold = 1800, yMaxThreshold = 2400;

unsigned long lastSwPressTime = 0;
const long debounceDelay = 50;
bool swState = HIGH; 

void setupInputs() {
  pinMode(JOYSTICK_X_PIN, INPUT);
  pinMode(JOYSTICK_Y_PIN, INPUT);
  pinMode(JOYSTICK_SW_PIN, INPUT_PULLUP);
  pinMode(PUSH_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DISPLAY_POWER_PIN, OUTPUT);
}

float getBatteryVoltage() {
  int raw = analogRead(BATTERY_PIN);

  Serial.print("Valoare bruta VN: ");
  Serial.println(raw);
  delay(500);

  float voltage = (raw / 4095.0) * 3.7 * 2.0; 

  Serial.print("VOLTAAAAJ: ");
  Serial.println(voltage);
  delay(500);

  return voltage;
}

int getBatteryPercentage() {
  float v = getBatteryVoltage();
  if (v < 3.2) return 0;
  if (v > 4.0) return 100;
  
  // (V_now - V_min) / (V_max - V_mi) * 100
  int percentage = (int)((v - 2.7) / (4.2 - 2.7) * 100);
  return percentage;
}

void updateInputs(RemoteState &state) {
  int xValue = analogRead(JOYSTICK_X_PIN);
  int yValue = analogRead(JOYSTICK_Y_PIN);
  int currentSwState = digitalRead(JOYSTICK_SW_PIN);
  
  if (xValue < xMinThreshold) state.steer = -1;      // left
  else if (xValue > xMaxThreshold) state.steer = 1;  // right
  else state.steer = 0;                              // center

  if (yValue < yMinThreshold) state.drive = 1;       // up/forward
  else if (yValue > yMaxThreshold) state.drive = -1; // down/backward
  else state.drive = 0;                              // center

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