#ifndef BME_AIRQUALITYHEART_H
#define BME_AIRQUALITYHEART_H

/* ##############################################################################
*  ############################# DEFINE STATEMENTS ##############################
*  ##############################################################################
*/
#include <Wire.h>
#include <Seeed_HM330X.h>
#include "MQ7.h"
#include "MAX30100_PulseOximeter.h"
#include <U8g2lib.h>
#include <NanoBLEFlashPrefs.h>
#include <Crypto.h>
#include <SHA256.h>
#include <myAES.h>
#include <mbed.h>

// Pin Definitions
#define MQ2_PIN A0
#define MQ7_PIN A1
#define BUTTON_UP_PIN 2
#define BUTTON_DOWN_PIN 3
#define BUTTON_LEFT_PIN 4
#define BUTTON_RIGHT_PIN 5
#define BUTTON_MENU_PIN 6
#define BUZZER_PIN 7

// Timing Intervals
#define DISPLAY_PERIOD_MS 5000
#define PULSE_OXIMETER_READ_INTERVAL 100
#define AIR_QUALITY_READ_INTERVAL 100
#define AVERAGE_WINDOW 10
#define RISK_WINDOW_SECONDS 30
#define INVALID_READING_TIMEOUT 500
#define STABILIZATION_PERIOD 5000
#define DEBOUNCE_DELAY_MS 200
#define MENU_HOLD_TIME_MS 1000
#define WAIT_TIME_MS 2000
#define LOCKED_OUT_WAIT_TIME_MS 10000
#define LOGGING_INTERVAL_MS 30000 // better set to 5 min = 300000
#define RISK_STATE_LOGGING_INTERVAL_MS 10000 // 10 sec -> set to a value less than LOGGING_INTERVAL_MS
#define BUZZER_DURATION 2000 // ring the buzzer for 2 sec
#define BUZZER_INTERVAL 5000 // ring the buzzer every 5 sec for 2 sec

// Threshold Values
#define PM25_THRESHOLD 50
#define MQ2_THRESHOLD 160
#define MQ7_THRESHOLD 50
#define HEART_RATE_LOW_THRESHOLD 40
#define HEART_RATE_HIGH_THRESHOLD 100
#define HEART_RATE_MIN_VALID_THRESHOLD 25
#define SPO2_THRESHOLD 92
#define SPO2_MIN_VALID_THRESHOLD 60

// Security Values (hashing, AES and password attempts)
#define PASSWORD_LENGTH 8
#define SALT_LENGTH 16
#define HASH_LENGTH 32
#define AES_DATA_LENGTH 16
#define IV_LENGTH 16
#define MAX_PASSWORD_ATTEMPTS 5
#define LOCKOUT_TIME_MS 60000

// Records and flash memory
#define FLASH_PAGE_SIZE 3600 // the data area only: MAX_RECORDS (100) records * 36 bytes (AES ciphertext per record), without the 48 hash/salt data bytes
#define PAGES_PER_RECORD 2
#define DATA_RECORD_SIZE 24 // sizeof(DataRecord)
#define MAX_RECORDS 100


/* ##############################################################################
*  ################################ ENUMERATORS #################################
*  ##############################################################################
*/
enum FilterType { 
    NONE_FILTER, 
    SMA, 
    EMA, 
    DEMA 
};

enum MainMenuOption {
    MAIN_INVALID_OPTION = -1,
    WELCOME_OPTION,
    VIEW_DATA_OPTION,
    ENABLE_LOGGING_OPTION,
    DISABLE_LOGGING_OPTION,
    SAVE_OPTION,
    LOCK_OPTION,
    UNLOCK_OPTION,
    CHANGE_PASSWORD_OPTION,
    DELETE_RECORDS_OPTION,
    LOGOUT_OPTION,
    EXIT_OPTION,
    MAIN_MENU_OPTION_COUNT
};

enum MenuState { 
    MAIN_MENU, 
    PASSWORD_MENU, 
    VIEW_DATA, 
    VIEW_RECORD_POSITION, 
    VIEW_RECORD_PAGE, 
    VIEW_DATA_EXIT, 
    REALTIME_DISPLAY, 
    PASSWORD_CORRECT, 
    PASSWORD_CHANGED, 
    WAIT, 
    WAIT_MESSAGE, 
    LOGOUT, 
    EXIT, 
    LOCKED_MESSAGE, 
    UNLOCKED_MESSAGE, 
    LOGGING_ON_MESSAGE, 
    LOGGING_OFF_MESSAGE, 
    DATA_SAVED_MESSAGE, 
    NO_RECORDS_MESSAGE,
    DELETE_RECORDS_MESSAGE
};

enum PasswordMenuOption {
    PASSWORD_INVALID_OPTION = -1,
    ENTER_PASSWORD_OPTION,
    SET_NEW_PASSWORD_OPTION,
    CONFIRM_PASSWORD_OPTION,
    CANCEL_PASSWORD_OPTION,
    PASSWORD_MENU_OPTION_COUNT
};

enum RiskCause { 
    NONE_RISK_CAUSE, 
    PM25_RISK_CAUSE, 
    MQ2_RISK_CAUSE, 
    MQ7_RISK_CAUSE, 
    HEART_RATE_RISK_CAUSE, 
    SPO2_RISK_CAUSE 
};


/* ##############################################################################
*  ################################## STRUCTS ###################################
*  ##############################################################################
*/
struct HealthDataPrefs {
    float heartRate;
    float spO2;
};

struct AirQualityDataPrefs {
    float pm25;
    float mq2;
    float mq7;
};

struct DataRecord {
    HealthDataPrefs healthData;
    AirQualityDataPrefs airQualityData;
    uint8_t riskState;  // 1 byte for risk state (0 = no risk, 1 = at risk)
    uint8_t riskCause;  // 1 byte for risk cause (enum value)
    uint16_t reserved;  // 2 bytes reserved for end-of-record indicator
};

struct CredentialsPrefs {
    char hash[HASH_LENGTH];
    char salt[SALT_LENGTH];
};

struct MemoryChunk {
    CredentialsPrefs credentials;
    DataRecord healthAirSensorsDataBuffer[MAX_RECORDS];
};

struct AESMemoryChunk {
    CredentialsPrefs credentials;
    char encryptedData[MAX_RECORDS][36]; // Encrypted records of 32 bytes each + twice 2 for end-of-record "BEEF" (we want aligned struct that's why we extra write 4 and not 2 bytes)
};

struct ViewDataContext {
    int currentPage;
    int currentRecord;
    int totalRecords;
    MemoryChunk memoryChunk;
};


/* ##############################################################################
*  ######################### EXTERNAL GLOBAL VARIABLES ##########################
*  ##############################################################################
*/
extern bool buzzerOn;
extern bool isAuthenticated;
extern bool isInitialPasswordSetup;
extern bool loggingEnabled;
extern bool lockMode;
extern bool menuButtonHeld;
extern bool overwriteBufferData;
extern bool riskStates[RISK_WINDOW_SECONDS * 1000 / PULSE_OXIMETER_READ_INTERVAL];
extern char currentPassword[PASSWORD_LENGTH + 1];
extern char keepPassword[PASSWORD_LENGTH + 1];
extern const char charSet[];
extern const char* str[];
extern const float alpha;
extern const int charSetSize;
extern DataRecord healthAirSensorsDataBuffer[MAX_RECORDS];
extern FilterType filterType;
extern float demaValues[5];
extern float emaValues[5];
extern float filteredHeartRate;
extern float filteredMQ2;
extern float filteredMQ7;
extern float filteredPM25;
extern float filteredSpO2;
extern float heartRate;
extern float heartRateReadings[AVERAGE_WINDOW];
extern float mq2Readings[AVERAGE_WINDOW];
extern float mq7Ppm;
extern float mq7Readings[AVERAGE_WINDOW];
extern float pm25;
extern float pm25Readings[AVERAGE_WINDOW];
extern float spO2;
extern float spO2Readings[AVERAGE_WINDOW];
extern HM330X sensor;
extern int dataRecordIndex;
extern int displayPage;
extern int failedAttempts;
extern int mq2Value;
extern int passwordIndex;
extern int readingsIndex[5];
extern int riskIndex;
extern MainMenuOption mainMenuOption;
extern MenuState menuState;
extern MQ7 mq7;
extern NanoBLEFlashPrefs myFlashPrefs;
extern PasswordMenuOption passwordMenuOption;
extern PulseOximeter pox;
extern SHA256 sha256;
extern uint8_t buf[30];
extern uint32_t dataSavedDisplayStart;
extern uint32_t deleteRecordsDisplayStart;
extern uint32_t lastButtonPressTime;
extern uint32_t lastBuzzerToggle;
extern uint32_t lastDisplayUpdate;
extern uint32_t lastLoggingTime;
extern uint32_t lastRiskCheck;
extern uint32_t lastRiskDetection;
extern uint32_t lastRiskStateLoggingTime;
extern uint32_t lastSensorRead;
extern uint32_t lastValidReading;
extern uint32_t lockDataDisplayStart;
extern uint32_t lockoutEndTime;
extern uint32_t loggingDisplayStart;
extern uint32_t menuButtonHoldStart;
extern uint32_t noRecordsDisplayStart;
extern uint32_t passwordChangedDisplayStart;
extern uint32_t passwordCorrectDisplayStart;
extern uint32_t stabilizationStart;
extern uint32_t tsLastReport;
extern uint32_t unlockDataDisplayStart;
extern uint32_t waitMessageDisplayStart;
extern U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2;
extern ViewDataContext viewDataContext;


/* ##############################################################################
*  ################################# FUNCTIONS ##################################
*  ##############################################################################
*/
void addRecordToBuffer(float heartRate, float spO2, float pm25, float mq2, float mq7, bool risk, RiskCause riskCause);
bool checkPasswordAttempts();
void checkRiskAndSoundBuzzer(bool persistentRisk);
bool checkRiskWindow();
RiskCause determineRiskCause(float pm25, float mq2, float mq7, float heartRate, float spO2);
void decryptAllRecords(AESMemoryChunk& aesMemoryChunk, MemoryChunk& memoryChunk, const char* key, const char* iv, int recordCount);
void decryptData(const char* ciphertext, char* plaintext, const char* key, const char* iv);
int decryptRecord(const char* ciphertext, DataRecord& record, const char* key, const char* iv);
void deleteRecords();
void deriveKeyAndIV(const char* password, const char* salt, char* key, char* iv);
void displayAirQualityValues();
void displayAlert();
void displayDataSavedMessage();
void displayExitOption();
void displayHealthSensorValues();
void displayIncorrectPasswordMessage(int attemptsLeft);
void displayLoadMoreOption();
void displayLockMessage();
void displayLockoutMessage();
void displayLoggingOffMessage();
void displayLoggingOnMessage();
void displayMenuOption();
void displayNoMoreRecordsOption();
void displayNoRecordsMessage();
void displayPasswordChanged();
void displayPasswordCorrect();
void displayPasswordFirstTimeSet();
void displayPasswordIncorrect();
void displayPasswordPrompt(const char* prompt);
void displayRecordPage(DataRecord record, int page);
void displayRecordPositionPage(int currentRecord, int totalRecords);
void displayRecordsDeleteFailedMessage();
void displayRecordsDeletedMessage();
void displayRiskCause(RiskCause riskCause);
void displayUnlockMessage();
void displayWaitMessage();
float doubleExponentialMovingAverage(float newValue, float prevDema, float prevEma, float alpha);
void encryptData(const char* plaintext, char* ciphertext, const char* key, const char* iv);
void encryptRecord(const DataRecord& record, char* ciphertext, const char* key, const char* iv);
void enterPassword();
float exponentialMovingAverage(float newValue, float prevEma, float alpha);
float filterData(float newValue, float *readings, int index, float &ema, float &dema, int sensorIndex);
const char* getCauseString(RiskCause cause);
int getEncryptedRecordsCount(AESMemoryChunk& aesMemoryChunk);
DataRecord getRecordFromFlash(int index);
int getRecordsCount(MemoryChunk memoryChunk, int maxRecordsNum);
void generateSalt(char* salt, size_t length);
void handleButtonPress();
void handleDownButtonPress();
void handleLeftButtonPress();
void handleMainMenuSelection();
void handleMenuButtonPress();
void handleMenuStateTransition(MenuState newState);
void handlePasswordMenuSelection();
void handleRightButtonPress();
void handleUpButtonPress();
void hashPassword(const char* password, const char* salt, char* hash);
bool isReadingValid(float heartRate, float spO2);
bool loadCredentials(char* hash, char* salt);
void logout();
void onBeatDetected();
void padData(const char* input, char* output, int dataSize);
void performGarbageCollection();
void printMemoryChunkHex(const void* memory, size_t size);
void reinitializeOximeter();
float roundToHalfPoint(float value);
void setPassword();
float simpleMovingAverage(float *array, int size);
void softReset();
void storeCredentials(const char* hash, const char* salt);
void unpadData(const char* input, char* output, int dataSize);
void updateCurrentPasswordChar(int direction);
void updatePasswordIndex(int direction);
void viewData();
void writeBufferToFlash();

#endif // BME_AIRQUALITYHEART_H