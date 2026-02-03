#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

#define JOYSTICK_X_PIN 34
#define JOYSTICK_Y_PIN 35
#define JOYSTICK_SW_PIN 32
#define PUSH_BUTTON_PIN GPIO_NUM_33
#define BUZZER_PIN 25
#define BATTERY_PIN 39
#define DISPLAY_POWER_PIN 17
#define RXD2 13
#define TXD2 14

#define BAUD_RATE 115200
#define NEXTION_BAUD 9600
#define SLEEP_THRESHOLD 2000

#define BATT_ICON_0_PERCENT 34
#define BATT_ICON_25_PERCENT 35
#define BATT_ICON_50_PERCENT 36
#define BATT_ICON_75_PERCENT 37
#define BATT_ICON_100_PERCENT 38

struct DriveCommand {
    int8_t drive;
    int8_t steer;
};

struct RemoteState {
    DriveCommand dc;
    uint8_t volume; 
    uint8_t faceIdx;
    uint8_t speed;
    uint8_t testPart;
    bool isTesting;
    bool musicPlaying;
    uint8_t trackID;
};

struct SettingsCommand {
    uint8_t volume; 
    uint8_t faceIdx;
    uint8_t speed;
};

struct StreamingPacket{
    uint32_t packetId;
    uint8_t audioData[240];
    uint8_t driveControl;
    DriveCommand dc;
};


void triggerBeep(int freq = 2000, int duration = 50);

#endif