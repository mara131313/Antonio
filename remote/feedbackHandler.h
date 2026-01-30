void setupFeedback() {
  pinMode(BUZZER_PIN, OUTPUT);
}

void triggerBeep(int freq = 2000, int duration = 50) {
  tone(BUZZER_PIN, freq, duration);
}