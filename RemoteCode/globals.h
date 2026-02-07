#ifndef GLOBALS_H
#define GLOBALS_H

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

//IDs for the battery images
#define BATT_ICON_0_PERCENT 37
#define BATT_ICON_25_PERCENT 38
#define BATT_ICON_50_PERCENT 39
#define BATT_ICON_75_PERCENT 40
#define BATT_ICON_100_PERCENT 41

//IDs for individual song images
#define SONG_0 59
#define SONG_1 60
#define SONG_2 61
#define SONG_3 62
#define SONG_4 63
#define SONG_5 64

#define MAX_SONGS 20
#define MAX_NAME_LEN 64

//toggle buttons
#define SONG_PLAY 16
#define SONG_PAUSE 15
#define TEST_PLAY_ON 34
#define TEST_PLAY_OFF 33
#define TEST_STOP_ON 36
#define TEST_STOP_OFF 35
#define THEME_OFF 11
#define THEME_ON 10
#define HAPPY_OFF 51
#define HAPPY_ON 52
#define PLAYFUL_OFF 53
#define PLAYFUL_ON 54
#define HEART_OFF 55
#define HEART_ON 56
#define DEFAULT_OFF 57 
#define DEFAULT_ON 58
#define FAST_ON 1
#define FAST_OFF 2
#define MED_ON 4
#define MED_OFF 3
#define SLOW_OFF 5
#define SLOW_ON 6

// for sending protocols
#define EVENT_SEND_REMOTE_STATE  (1 << 0)
#define EVENT_SEND_STREAMING     (1 << 1)

//for the theme song
#define NOTE_C4  262
#define NOTE_E4  330
#define NOTE_G4  392
#define NOTE_C5  523
#define NOTE_A4  440

// GENRE DEFINITIONS
enum MusicGenre {
    GENRE_DEFAULT = 0,
    GENRE_POP = 1,
    GENRE_RAP = 2,
    GENRE_ROC = 3,
    GENRE_MAN = 4,
    GENRE_EDM = 5
};

struct DriveCommand {
    int8_t drive;
    int8_t steer;
};

struct RemoteState {
    DriveCommand dc;
    uint8_t volume;
    uint8_t brightness;
    uint8_t faceIdx;
    uint8_t speed;
    uint8_t testPart;
    bool isTesting;
    bool isSwPressed;
    bool musicPlaying;
    uint8_t trackID;
    uint8_t currentGenre;
};

struct StreamingPacket{
    uint32_t packetId;
    uint8_t audioData[240];
    uint8_t driveControl;
    DriveCommand dc;
};

void triggerBeep(int freq = 2000, int duration = 50);

extern EventGroupHandle_t commsEvents;
extern StreamingPacket myStreaming;
extern uint8_t songGenres[MAX_SONGS];

#endif