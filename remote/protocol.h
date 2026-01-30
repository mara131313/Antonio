struct RemoteState {
  // movement
  int8_t drive;     // -1 => backward, 0 => stop, 1 => forward
  int8_t steer;     // -1 => left, 0 => balanced, 1 => right
  
  // system settings
  uint8_t volume;   // 0-100
  uint8_t faceIdx;  // index of face expression
  uint8_t speed; // 0 - slow 1 - med 2 - fast
  
  // test Mode
  uint8_t testPart; // 0: none, 1: legs, 2: wrists, 3: shoulders, 4: head, 5: tail
  bool isTesting;   // Start/Stop

  // music
  uint8_t trackID;
};