#include "BMEAirQualityHeart.h"

/* ##############################################################################
*  #############################  GLOBAL VARIABLES ##############################
*  ##############################################################################
*/
bool buzzerOn = false;
bool isAuthenticated = false;
bool isInitialPasswordSetup = false; // Flag to indicate initial password setup
bool loggingEnabled = false;
bool lockMode = false;
bool menuButtonHeld = false;
bool overwriteBufferData = false;
bool riskStates[RISK_WINDOW_SECONDS * 1000 / PULSE_OXIMETER_READ_INTERVAL];
char currentPassword[PASSWORD_LENGTH + 1] = ""; // used for storing user input password
char keepPassword[PASSWORD_LENGTH + 1] = ""; // used for deriving key in AES operations
const char charSet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()_";
const char* str[] = {
    "sensor num: ", "PM1.0 (CF=1,ug/m3): ", "PM2.5 (CF=1,ug/m3): ",
    "PM10 (CF=1,ug/m3): ", "PM1.0 (ATM,ug/m3): ", "PM2.5 (ATM,ug/m3): ",
    "PM10 (ATM,ug/m3): "
};
const float alpha = 0.2;
const int charSetSize = sizeof(charSet) - 1;
DataRecord healthAirSensorsDataBuffer[MAX_RECORDS];
FilterType filterType = DEMA;
float demaValues[5] = {0, 0, 0, 0, 0};
float emaValues[5] = {0, 0, 0, 0, 0};
float filteredHeartRate = 0;
float filteredMQ2 = 0;
float filteredMQ7 = 0;
float filteredPM25 = 0;
float filteredSpO2 = 0;
float heartRate = 0;
float heartRateReadings[AVERAGE_WINDOW];
float mq2Readings[AVERAGE_WINDOW];
float mq7Ppm = 0;
float mq7Readings[AVERAGE_WINDOW];
float pm25 = 0;
float pm25Readings[AVERAGE_WINDOW];
float spO2 = 0;
float spO2Readings[AVERAGE_WINDOW];
HM330X sensor;
int dataRecordIndex = 0;
int displayPage = 0;
int failedAttempts = 0;
int mq2Value = 0;
int passwordIndex = 0;
int readingsIndex[5] = {0, 0, 0, 0, 0};
int riskIndex = 0;
MainMenuOption mainMenuOption = MAIN_INVALID_OPTION;
MenuState menuState = REALTIME_DISPLAY;
MQ7 mq7(MQ7_PIN, 3.3);
NanoBLEFlashPrefs myFlashPrefs;
PasswordMenuOption passwordMenuOption = PASSWORD_INVALID_OPTION;
PulseOximeter pox;
SHA256 sha256;
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
uint32_t dataSavedDisplayStart = 0;
uint32_t deleteRecordsDisplayStart = 0;
uint32_t lastBuzzerToggle = 0;
uint32_t lastButtonPressTime = 0;
uint32_t lastDisplayUpdate = 0;
uint32_t lastLoggingTime = 0;
uint32_t lastRiskCheck = 0;
uint32_t lastRiskDetection = 0;
uint32_t lastRiskStateLoggingTime = 0;
uint32_t lastSensorRead = 0;
uint32_t lastValidReading = 0;
uint32_t lockDataDisplayStart = 0;
uint32_t lockoutEndTime = 0;
uint32_t loggingDisplayStart = 0;
uint32_t menuButtonHoldStart = 0;
uint32_t noRecordsDisplayStart = 0;
uint32_t passwordChangedDisplayStart = 0;
uint32_t passwordCorrectDisplayStart = 0;
uint32_t stabilizationStart = 0;
uint32_t tsLastReport = 0;
uint32_t unlockDataDisplayStart = 0;
uint32_t waitMessageDisplayStart = 0;
uint8_t buf[30];
ViewDataContext viewDataContext;


/* ##############################################################################
*  ################################## FUNCTIONS #################################
*  ##############################################################################
*/
void addRecordToBuffer(float heartRate, float spO2, float pm25, float mq2, float mq7, bool risk, RiskCause riskCause) {
    Serial.print("Attempting to add record to buffer, current index: ");
    Serial.println(dataRecordIndex);

    if (dataRecordIndex >= MAX_RECORDS) {
      overwriteBufferData = true;
      dataRecordIndex = 0;
    }
    healthAirSensorsDataBuffer[dataRecordIndex].healthData.heartRate = heartRate;
    healthAirSensorsDataBuffer[dataRecordIndex].healthData.spO2 = spO2;
    healthAirSensorsDataBuffer[dataRecordIndex].airQualityData.pm25 = pm25;
    healthAirSensorsDataBuffer[dataRecordIndex].airQualityData.mq2 = mq2;
    healthAirSensorsDataBuffer[dataRecordIndex].airQualityData.mq7 = mq7;
    healthAirSensorsDataBuffer[dataRecordIndex].riskState = risk ? 1 : 0;
    healthAirSensorsDataBuffer[dataRecordIndex].riskCause = riskCause;
    healthAirSensorsDataBuffer[dataRecordIndex].reserved = 0xEFBE;  // Set to 0xBEEF to indicate end-of-record (first the LSB is written, so we set it here to 0xEFBE)        
    dataRecordIndex++;
    Serial.print("Record added to buffer, new index: ");
    Serial.println(dataRecordIndex);
}

bool checkPasswordAttempts() {
    // Check if currently locked out after exceeding password attempts and whether timeout interval has not passed
    if (millis() < lockoutEndTime) {
        Serial.println("Locked out due to too many failed attempts.");
        displayLockoutMessage(); // Display a lockout message on the display
        return false;
    }

    failedAttempts++;
    if (failedAttempts >= MAX_PASSWORD_ATTEMPTS) {
        lockoutEndTime = millis() + LOCKOUT_TIME_MS;
        Serial.println("Too many failed attempts. Locked out.");
        displayLockoutMessage(); // Display a lockout message on the display
    } else {
        Serial.println("Password is incorrect. Attempts left: " + String(MAX_PASSWORD_ATTEMPTS - failedAttempts));
        displayIncorrectPasswordMessage(MAX_PASSWORD_ATTEMPTS - failedAttempts); // Display remaining attempts
    }
}

void checkRiskAndSoundBuzzer(bool persistentRisk) {
    uint32_t currentMillis = millis();

    if (persistentRisk) {
        if (!buzzerOn && (currentMillis - lastRiskDetection >= BUZZER_INTERVAL)) {
            digitalWrite(BUZZER_PIN, HIGH);
            buzzerOn = true;
            lastBuzzerToggle = currentMillis;
        }

        if (buzzerOn && (currentMillis - lastBuzzerToggle >= BUZZER_DURATION)) {
            digitalWrite(BUZZER_PIN, LOW);
            buzzerOn = false;
            lastRiskDetection = currentMillis;
        }
    } else {
        digitalWrite(BUZZER_PIN, LOW);
        buzzerOn = false;
        lastRiskDetection = 0;
    }
}

bool checkRiskWindow() {
    int riskCount = 0;
    for (int i = 0; i < RISK_WINDOW_SECONDS * 1000 / PULSE_OXIMETER_READ_INTERVAL; i++) {
        if (riskStates[i]) {
            riskCount++;
        }
    }
    return riskCount > (RISK_WINDOW_SECONDS * 1000 / PULSE_OXIMETER_READ_INTERVAL) / 2;
}

RiskCause determineRiskCause(float pm25, float mq2, float mq7, float heartRate, float spO2) {
    RiskCause cause = NONE_RISK_CAUSE;
    if (spO2 < SPO2_THRESHOLD && spO2 >= SPO2_MIN_VALID_THRESHOLD) cause = (cause == NONE_RISK_CAUSE) ? SPO2_RISK_CAUSE : cause;
    if ((heartRate < HEART_RATE_LOW_THRESHOLD && heartRate >= HEART_RATE_MIN_VALID_THRESHOLD) || heartRate > HEART_RATE_HIGH_THRESHOLD) cause = (cause == NONE_RISK_CAUSE) ? HEART_RATE_RISK_CAUSE : cause;
    if (pm25 > PM25_THRESHOLD) cause = PM25_RISK_CAUSE;
    if (mq2 > MQ2_THRESHOLD) cause = (cause == NONE_RISK_CAUSE) ? MQ2_RISK_CAUSE : cause;
    if (mq7 > MQ7_THRESHOLD) cause = (cause == NONE_RISK_CAUSE) ? MQ7_RISK_CAUSE : cause;
    return cause;
}

void decryptAllRecords(AESMemoryChunk& aesMemoryChunk, MemoryChunk& memoryChunk, const char* key, const char* iv, int recordCount) {
    Serial.println("Entering decryptAllRecords().");
    Serial.println("recordCount in decryptAllRecords() = ");
    Serial.println(recordCount);
    for (int i = 0; i < recordCount; i++) {
        Serial.print("Decrypting record number: ");
        Serial.println(i + 1);
        if (!decryptRecord(aesMemoryChunk.encryptedData[i], memoryChunk.healthAirSensorsDataBuffer[i], key, iv)) {
            Serial.print("Invalid padding detected in record ");
            Serial.println(i);
            for (int j = i + 1; j < recordCount; j++) {
                memcpy(aesMemoryChunk.encryptedData[j - 1], aesMemoryChunk.encryptedData[j], sizeof(aesMemoryChunk.encryptedData[j]));
            }
            recordCount--;
            i--; // Re-check the current index since it now contains the next record
        }
    }
    Serial.println("Exiting decryptAllRecords().");
}

void decryptData(const char* ciphertext, char* plaintext, const char* key, const char* iv) {
    AES aes;
    aes.setup(key, AES::KEY_256, AES::MODE_CBC, iv); // Using AES-256
    aes.decrypt(ciphertext, plaintext, 16);
    aes.clear();
}

int decryptRecord(const char* ciphertext, DataRecord& record, const char* key, const char* iv) {
    char plaintext[32];
    int ret = 1;

    Serial.println("Decrypting record...");

    // Print the ciphertext for debugging
    Serial.print("Ciphertext: ");
    for (int i = 0; i < 36; i++) {
        Serial.print((int)ciphertext[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    // Decrypt each block
    for (int i = 0; i < 32; i += 16) {
        decryptData(ciphertext + i, plaintext + i, key, iv);
    }

    // Print decrypted plaintext for debugging
    Serial.print("Decrypted plaintext: ");
    for (int i = 0; i < 32; i++) {
        Serial.print((int)plaintext[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    Serial.println("Unpadding record...");
    unpadData(plaintext, reinterpret_cast<char*>(&record), 32);
    if (plaintext[31] != 8) {
        Serial.println("Unpadding successful.");
        ret = 0; // padding error
    }
    return ret;
}

void deleteRecords() {
    Serial.print("menuState: ");
    Serial.println(menuState);
    Serial.print("mainMenuOption: ");
    Serial.println(mainMenuOption);
    // Clear the flash memory where encrypted records are stored but keep the credentials
    AESMemoryChunk aesMemoryChunk;
    performGarbageCollection();
    
    // Read the existing memory chunk to preserve credentials
    int rc = myFlashPrefs.readPrefs(&aesMemoryChunk, sizeof(aesMemoryChunk));
    if (rc != 0) {
        Serial.println("Failed to read existing records.");
        displayRecordsDeleteFailedMessage();
        return;
    }
    
    // Preserve credentials and clear the health and air quality data
    CredentialsPrefs savedCredentials = aesMemoryChunk.credentials;
    memset(aesMemoryChunk.encryptedData, 0xFF, sizeof(aesMemoryChunk.encryptedData)); // Set to 0xFF which is the default erased state
    memcpy(&aesMemoryChunk.credentials, &savedCredentials, sizeof(savedCredentials)); // Restore credentials

    rc = myFlashPrefs.writePrefs(&aesMemoryChunk, sizeof(aesMemoryChunk));
    if (rc == 0) {
        Serial.println("All records deleted successfully.");
        displayRecordsDeletedMessage();
        delay(WAIT_TIME_MS);
        softReset();
    } else {
        Serial.println("Failed to delete records.");
        displayRecordsDeleteFailedMessage();
    }

    // Reinitialize sensors if needed
    reinitializeOximeter();
}

void deriveKeyAndIV(const char* password, const char* salt, char* key, char* iv) {
    SHA256 sha256;
    char concatenated[PASSWORD_LENGTH + SALT_LENGTH + 1]; // +1 for the final null terminator
    Serial.print("Password: ");
    Serial.println(password);
    Serial.print("Salt: ");
    Serial.println(salt);
    Serial.print("Concatenated size: ");
    Serial.println(sizeof(concatenated));

    Serial.println("Entering deriveKeyAndIV().");

    // Concatenate password and salt
    strncpy(concatenated, password, PASSWORD_LENGTH);
    strncat(concatenated, salt, SALT_LENGTH);
    // Ensure the concatenated string is null-terminated
    concatenated[PASSWORD_LENGTH + SALT_LENGTH] = '\0';

    Serial.println("Concatenated password and salt in deriveKeyAndIV().");
    Serial.print("Concatenated: ");
    Serial.println(concatenated);

    // Hash the concatenated string
    sha256.reset();
    sha256.update((const byte*)concatenated, strlen(concatenated));
    byte derivedHash[HASH_LENGTH];
    sha256.finalize(derivedHash, HASH_LENGTH);

    Serial.println("Derived hash in deriveKeyAndIV().");
    for (int i = 0; i < HASH_LENGTH; i++) {
        Serial.print(derivedHash[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    // Use the hash to generate key and IV
    memcpy(key, derivedHash, 32); // Use 32 bytes for the key (AES-256)
    memcpy(iv, derivedHash + 16, 16); // Use the last 16 bytes for the IV

    Serial.println("Exiting deriveKeyAndIV().");
}

void displayAirQualityValues() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);

    u8g2.drawStr(0, 10, "PM2.5: ");
    u8g2.drawStr(50, 10, String(filteredPM25).c_str());
    u8g2.drawLine(0, 15, 128, 15);

    u8g2.drawStr(0, 26, "MQ2: ");
    u8g2.drawStr(35, 26, String(filteredMQ2, 1).c_str());
    u8g2.drawStr(60, 26, " | ");
    u8g2.drawStr(70, 26, "MQ7: ");
    u8g2.drawStr(105, 26, String(filteredMQ7, 1).c_str());

    u8g2.sendBuffer();
}

void displayAlert() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "ALERT! User at risk!");
    u8g2.sendBuffer();
}

void displayDataSavedMessage() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Data saved.");
    u8g2.sendBuffer();
}

void displayExitOption() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Exit");
    u8g2.sendBuffer();
}

void displayHealthSensorValues() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);

    u8g2.drawStr(0, 10, "HR: ");
    u8g2.drawStr(40, 10, String(filteredHeartRate, 1).c_str());
    u8g2.drawStr(70, 10, " bpm");

    u8g2.drawStr(0, 20, "SpO2:");
    u8g2.drawStr(40, 20, String(filteredSpO2, 1).c_str());
    u8g2.drawStr(70, 20, " %");

    u8g2.sendBuffer();
}

void displayIncorrectPasswordMessage(int attemptsLeft) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Incorrect password");
    u8g2.setCursor(0, 20);
    u8g2.print("Attempts left: ");
    u8g2.print(attemptsLeft);
    u8g2.sendBuffer();
    delay(WAIT_TIME_MS);
    reinitializeOximeter();
}

void displayLoadMoreOption() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Load more");
    u8g2.sendBuffer();
}

void displayLockMessage() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Data are locked.");
    u8g2.sendBuffer();
}

void displayLockoutMessage() {
    int remainingTime = (lockoutEndTime - millis()) / 1000;
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "Please wait %d sec.", remainingTime);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Locked out!");
    u8g2.drawStr(0, 20, buffer);
    u8g2.sendBuffer();
    delay(LOCKED_OUT_WAIT_TIME_MS);
    reinitializeOximeter();
}

void displayLoggingOffMessage() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Data logging off.");
    u8g2.sendBuffer();
}

void displayLoggingOnMessage() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Data logging on.");
    u8g2.sendBuffer();
}

void displayMenuOption() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);

    if (menuState == MAIN_MENU) {
        switch (mainMenuOption) {
            case WELCOME_OPTION:
                u8g2.drawStr(0, 10, "Welcome to BME 2024!");
                u8g2.drawStr(0, 30, "<--");
                u8g2.drawStr(110, 30, "-->");
                break;
            case VIEW_DATA_OPTION:
                u8g2.drawStr(0, 10, "1. View Data");
                break;
            case ENABLE_LOGGING_OPTION:
                u8g2.drawStr(0, 10, "2. Enable Logging");
                break;
            case DISABLE_LOGGING_OPTION:
                u8g2.drawStr(0, 10, "3. Disable Logging");
                break;
            case SAVE_OPTION:
                u8g2.drawStr(0, 10, "4. Save Data");
                break;
            case LOCK_OPTION:
                u8g2.drawStr(0, 10, "5. Lock Data");
                break;
            case UNLOCK_OPTION:
                u8g2.drawStr(0, 10, "6. Unlock Data");
                break;
            case CHANGE_PASSWORD_OPTION:
                u8g2.drawStr(0, 10, "7. Change Password");
                break;
            case DELETE_RECORDS_OPTION:
                u8g2.drawStr(0, 10, "8. Delete Records");
                break;
            case LOGOUT_OPTION:
                u8g2.drawStr(0, 10, "9. Logout");
                break;
            case EXIT_OPTION:
                u8g2.drawStr(0, 10, "10. Exit");
                break;
            default:
                u8g2.drawStr(0, 10, "Invalid Menu Option");
                break;
        }
    } else if (menuState == PASSWORD_MENU) {
        switch (passwordMenuOption) {
            case ENTER_PASSWORD_OPTION:
                enterPassword();
                break;
            case SET_NEW_PASSWORD_OPTION:
                setPassword();
                break;
            case CONFIRM_PASSWORD_OPTION:
                u8g2.drawStr(0, 10, "Confirm Password");
                u8g2.drawStr(0, 20, "Press Menu to Confirm");
                break;
            case CANCEL_PASSWORD_OPTION:
                u8g2.drawStr(0, 10, "Cancel");
                u8g2.drawStr(0, 20, "Press Menu to Cancel");
                break;
            default:
                u8g2.drawStr(0, 10, "Invalid Menu Option");
                break;
        }
    } else if (menuState == LOCKED_MESSAGE) {
        u8g2.drawStr(0, 10, "Data are locked.");
        u8g2.sendBuffer();
    } else if (menuState == UNLOCKED_MESSAGE) {
        u8g2.drawStr(0, 10, "Data are unlocked.");
        u8g2.sendBuffer();
    } else if (menuState == LOGGING_ON_MESSAGE) {
        displayLoggingOnMessage();
        if (millis() - loggingDisplayStart >= WAIT_TIME_MS) {
            menuState = REALTIME_DISPLAY;
        }
    } else if (menuState == LOGGING_OFF_MESSAGE) {
        displayLoggingOffMessage();
        if (millis() - loggingDisplayStart >= WAIT_TIME_MS) {
            menuState = REALTIME_DISPLAY;
        }
    } else if (menuState == DATA_SAVED_MESSAGE) {
        displayDataSavedMessage();
        if (millis() - dataSavedDisplayStart >= WAIT_TIME_MS) {
            menuState = REALTIME_DISPLAY;
        }
    }

    u8g2.sendBuffer();
}

void displayNoMoreRecordsOption() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "No more records.");
    u8g2.sendBuffer();
}

void displayNoRecordsMessage() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "No records saved.");
    u8g2.sendBuffer();
}

void displayPasswordChanged() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Password changed.");
    u8g2.drawStr(0, 20, "Rebooting..."); 
    u8g2.sendBuffer();
}

void displayPasswordCorrect() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Password Correct!");
    u8g2.drawStr(0, 20, "Success.");
    u8g2.sendBuffer();
}

void displayPasswordFirstTimeSet() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Password was set.");
    u8g2.drawStr(0, 20, "Rebooting...");
    u8g2.sendBuffer();
}
void displayPasswordIncorrect() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Password Incorrect!");
    u8g2.drawStr(0, 20, "Please try again.");
    u8g2.sendBuffer();
}

void displayPasswordPrompt(const char* prompt) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, prompt);

    for (int i = 0; i < PASSWORD_LENGTH; i++) {
        if (currentPassword[i] != '\0') {
            char ch[2] = { currentPassword[i], '\0' };
            u8g2.drawStr(i * 8 + 2, 20, ch);
        } else {
            u8g2.drawStr(i * 8 + 2, 20, " ");
        }
    }

    u8g2.drawStr(passwordIndex * 8 + 2, 30, "_");
    u8g2.sendBuffer();
}

void displayRecordPage(DataRecord record, int page) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);

    if (page == 0) {
        // First page: HR, SpO2, and Risk (on separate lines)
        char line1[32];
        snprintf(line1, sizeof(line1), "HR: %.1f bpm", record.healthData.heartRate);
        u8g2.drawStr(0, 10, line1);

        char line2[32];
        snprintf(line2, sizeof(line2), "SpO2: %.1f%%", record.healthData.spO2);
        u8g2.drawStr(0, 20, line2);

        char line3[32];
        if (record.riskState) {
            const char* causeStr = getCauseString(static_cast<RiskCause>(record.riskCause));
            snprintf(line3, sizeof(line3), "Risk: Yes, Cause: %s", causeStr);
        } else {
            snprintf(line3, sizeof(line3), "Risk: No");
        }
        u8g2.drawStr(0, 30, line3);
    } else if (page == 1) {
        // Second page: Air quality sensor values (on separate lines)
        char line1[32];
        snprintf(line1, sizeof(line1), "PM2.5: %.1f", record.airQualityData.pm25);
        u8g2.drawStr(0, 10, line1);

        char line2[32];
        snprintf(line2, sizeof(line2), "MQ2: %.1f", record.airQualityData.mq2);
        u8g2.drawStr(0, 20, line2);

        char line3[32];
        snprintf(line3, sizeof(line3), "MQ7: %.1f", record.airQualityData.mq7);
        u8g2.drawStr(0, 30, line3);
    }
    u8g2.sendBuffer();
}

void displayRecordPositionPage(int currentRecord, int totalRecords) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    char positionText[32];
    snprintf(positionText, sizeof(positionText), "Record %d / %d", currentRecord, totalRecords);
    u8g2.drawStr(0, 10, positionText);
    u8g2.sendBuffer();
}

void displayRecordsDeleteFailedMessage() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Delete failed.");
    u8g2.sendBuffer();
}

void displayRecordsDeletedMessage() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Records deleted.");
    u8g2.drawStr(0, 20, "Rebooting...");
    u8g2.sendBuffer();
}

void displayRiskCause(RiskCause riskCause) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);

    char buffer[32];
    switch (riskCause) {
        case PM25_RISK_CAUSE:
            snprintf(buffer, sizeof(buffer), "Acceptable: <= %d ug/m3", PM25_THRESHOLD);
            u8g2.drawStr(0, 10, "Cause: PM2.5");
            u8g2.drawStr(0, 30, buffer);
            break;
        case MQ2_RISK_CAUSE:
            snprintf(buffer, sizeof(buffer), "Acceptable: <= %d (8-bit)", MQ2_THRESHOLD);
            u8g2.drawStr(0, 10, "Cause: MQ2");
            u8g2.drawStr(0, 30, buffer);
            break;
        case MQ7_RISK_CAUSE:
            snprintf(buffer, sizeof(buffer), "Acceptable: <= %d ppm", MQ7_THRESHOLD);
            u8g2.drawStr(0, 10, "Cause: MQ7");
            u8g2.drawStr(0, 30, buffer);
            break;
        case HEART_RATE_RISK_CAUSE:
            snprintf(buffer, sizeof(buffer), "Acceptable: %d-%d bpm", HEART_RATE_LOW_THRESHOLD, HEART_RATE_HIGH_THRESHOLD);
            u8g2.drawStr(0, 10, "Cause: Heart Rate");
            u8g2.drawStr(0, 30, buffer);
            break;
        case SPO2_RISK_CAUSE:
            snprintf(buffer, sizeof(buffer), "Acceptable: >= %d%%", SPO2_THRESHOLD);
            u8g2.drawStr(0, 10, "Cause: SpO2");
            u8g2.drawStr(0, 30, buffer);
            break;
        default:
            u8g2.drawStr(0, 10, "Unknown cause");
            break;
    }

    u8g2.sendBuffer();
}

void displayUnlockMessage() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Data are unlocked.");
    u8g2.sendBuffer();
}

void displayWaitMessage() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Please wait...");
    u8g2.sendBuffer();
}

float doubleExponentialMovingAverage(float newValue, float prevDema, float prevEma, float alpha) {
    float ema = exponentialMovingAverage(newValue, prevEma, alpha);
    float dema = alpha * (2 * ema - prevDema) + (1 - alpha) * prevDema;
    return dema;
}

void encryptData(const char* plaintext, char* ciphertext, const char* key, const char* iv) {
    AES aes;
    aes.setup(key, AES::KEY_256, AES::MODE_CBC, iv); // Using AES-256
    aes.encrypt(plaintext, ciphertext, 16); // AES has always 128-bit block size
    aes.clear();
}

void encryptRecord(const DataRecord& record, char* ciphertext, const char* key, const char* iv) {
    char plaintext[32]; // we encrypt 24 bytes data + up to 8 bytes padding, the last 4 bytes BEEF are written as unencrypted
    Serial.println("Padding record...");

    padData(reinterpret_cast<const char*>(&record), plaintext, sizeof(DataRecord));
    Serial.println("Encrypting record...");

    for (int i = 0; i < 32; i += 16) {
        encryptData(plaintext + i, ciphertext + i, key, iv);
    }

    // Add the BEEF marker twice
    ciphertext[32] = 0xBE;
    ciphertext[33] = 0xEF; 
    ciphertext[34] = 0xBE;
    ciphertext[35] = 0xEF;
}

void enterPassword() {
    displayPasswordPrompt("Enter password:");
}

float exponentialMovingAverage(float newValue, float prevEma, float alpha) {
    float ema = alpha * newValue + (1 - alpha) * prevEma;
    return ema;
}

float filterData(float newValue, float *readings, int index, float &ema, float &dema, int sensorIndex) {
    readings[index] = newValue;
    float filteredValue = newValue;

    switch (filterType) {
        case SMA:
            filteredValue = simpleMovingAverage(readings, AVERAGE_WINDOW);
            break;
        case EMA:
            filteredValue = exponentialMovingAverage(newValue, ema, alpha);
            ema = filteredValue;
            break;
        case DEMA:
            filteredValue = doubleExponentialMovingAverage(newValue, dema, ema, alpha);
            ema = exponentialMovingAverage(newValue, ema, alpha);
            dema = filteredValue;
            break;
        case NONE_FILTER:
        default:
            filteredValue = newValue;
            break;
    }

    return filteredValue;
}

const char* getCauseString(RiskCause cause) {
    switch (cause) {
        case PM25_RISK_CAUSE:
            return "PM2.5";
        case MQ2_RISK_CAUSE:
            return "MQ2";
        case MQ7_RISK_CAUSE:
            return "MQ7";
        case HEART_RATE_RISK_CAUSE:
            return "Heart";
        case SPO2_RISK_CAUSE:
            return "SpO2";
        default:
            return "Unknown";
    }
}

int getEncryptedRecordsCount(AESMemoryChunk& aesMemoryChunk) {
    int count = 0;
    for (int i = 0; i < MAX_RECORDS; i++) {
        if (aesMemoryChunk.encryptedData[i][32] == 0xBE && aesMemoryChunk.encryptedData[i][33] == 0xEF && aesMemoryChunk.encryptedData[i][34] == 0xBE && aesMemoryChunk.encryptedData[i][35] == 0xEF) {
            count++;
        } else {
            break;
        }
    }
    // each record corresonds to two BEEF
    return count;
}

// currently not used
DataRecord getRecordFromFlash(int index) {
    AESMemoryChunk aesMemoryChunk;
    int dataSize = myFlashPrefs.readPrefs(&aesMemoryChunk, sizeof(aesMemoryChunk));
    char ciphertext[36]; // 24 bytes data + up to 8 bytes padding + 4 for end-of-record indicator BEEF twice
    char key[32];
    char iv[16];
    char hash[HASH_LENGTH];
    char salt[SALT_LENGTH];

    if (!loadCredentials(hash, salt)) {
        Serial.println("Failed to load encryption credentials.");
        return DataRecord(); // Return an empty record in case of failure
    }

    deriveKeyAndIV(keepPassword, salt, key, iv);

    Serial.print("Getting record from flash at index: ");
    Serial.println(index);

    if (dataSize >= sizeof(CredentialsPrefs) + (index + 1) * 32) { // Ensure sufficient data
        memcpy(ciphertext, &aesMemoryChunk.encryptedData[index], sizeof(ciphertext));
        DataRecord record;
        decryptRecord(ciphertext, record, key, iv);
        Serial.println("Record retrieved successfully.");
        return record;
    } else {
        Serial.println("Failed to retrieve record.");
        return DataRecord(); // Return an empty record in case of failure
    }
}

int getRecordsCount(MemoryChunk memoryChunk, int maxRecordsNum) {
    uint16_t records_count = 0;
    for (int i = 0; i < maxRecordsNum; i++) {
        if (memoryChunk.healthAirSensorsDataBuffer[i].reserved == 0xEFBE) {
            records_count++;
            Serial.println("BEEF found!");
        } else {
            break;
        }
    }
    Serial.println("getRecordsCount() records_count = ");
    Serial.println(records_count);
    return records_count;
}

void generateSalt(char* salt, size_t length) {
    for (size_t i = 0; i < length; i++) {
        salt[i] = charSet[random(charSetSize)];
    }
    salt[length] = '\0'; // Null-terminate the salt
}

void handleButtonPress() {
    static uint32_t lastButtonPress = 0;
    uint32_t currentMillis = millis();

    if (currentMillis - lastButtonPress > DEBOUNCE_DELAY_MS) {
        if (digitalRead(BUTTON_UP_PIN) == LOW) {
            lastButtonPress = currentMillis;
            handleUpButtonPress();
        } else if (digitalRead(BUTTON_DOWN_PIN) == LOW) {
            lastButtonPress = currentMillis;
            handleDownButtonPress();
        } else if (digitalRead(BUTTON_LEFT_PIN) == LOW) {
            lastButtonPress = currentMillis;
            handleLeftButtonPress();
        } else if (digitalRead(BUTTON_RIGHT_PIN) == LOW) {
            lastButtonPress = currentMillis;
            handleRightButtonPress();
        } else if (digitalRead(BUTTON_MENU_PIN) == LOW) {
            lastButtonPress = currentMillis;
            handleMenuButtonPress();
        }
    }
}

void handleDownButtonPress() {
    if (menuState == PASSWORD_MENU) {
        updateCurrentPasswordChar(-1);
    }
}

void handleLeftButtonPress() {
    Serial.print("Left button pressed in state: ");
    Serial.println(menuState);

    if (menuState == MAIN_MENU) {
        mainMenuOption = static_cast<MainMenuOption>((mainMenuOption == WELCOME_OPTION) ? EXIT_OPTION : mainMenuOption - 1);
    } else if (menuState == PASSWORD_MENU) {
        if (passwordMenuOption == ENTER_PASSWORD_OPTION || passwordMenuOption == SET_NEW_PASSWORD_OPTION) {
            updatePasswordIndex(-1);
        } else if (passwordMenuOption == CONFIRM_PASSWORD_OPTION) {
            passwordMenuOption = SET_NEW_PASSWORD_OPTION;
        } else if (passwordMenuOption == CANCEL_PASSWORD_OPTION) {
            passwordMenuOption = CONFIRM_PASSWORD_OPTION;
        }
    }
}

void handleMainMenuSelection() {
    switch (mainMenuOption) {
        case WELCOME_OPTION:
            break;
        case VIEW_DATA_OPTION:
            viewData();
            break;
        case ENABLE_LOGGING_OPTION:
            loggingEnabled = true;
            menuState = LOGGING_ON_MESSAGE;
            loggingDisplayStart = millis();
            break;
        case DISABLE_LOGGING_OPTION:
            loggingEnabled = false;
            menuState = LOGGING_OFF_MESSAGE;
            loggingDisplayStart = millis();
            break;
        case SAVE_OPTION:
            Serial.println("Save data option selected.");
            // pox.update();
            // heartRate = pox.getHeartRate();
            // spO2 = pox.getSpO2();
            // filteredHeartRate = filterData(heartRate, heartRateReadings, readingsIndex[0], emaValues[0], demaValues[0], 0);
            // filteredHeartRate = roundToHalfPoint(filteredHeartRate);
            // filteredSpO2 = filterData(spO2, spO2Readings, readingsIndex[1], emaValues[1], demaValues[1], 1);
            addRecordToBuffer(filteredHeartRate, filteredSpO2, filteredPM25, filteredMQ2, filteredMQ7, false, NONE_RISK_CAUSE);
            if (dataRecordIndex > 0) {
                writeBufferToFlash();
                menuState = DATA_SAVED_MESSAGE;
                dataSavedDisplayStart = millis();
            } else {
                Serial.println("No records to save.");
                menuState = NO_RECORDS_MESSAGE;
                noRecordsDisplayStart = millis();
            }
            break;
        case LOCK_OPTION:
            lockMode = true;
            menuState = LOCKED_MESSAGE;
            lockDataDisplayStart = millis();
            break;
        case UNLOCK_OPTION:
            lockMode = false;
            menuState = UNLOCKED_MESSAGE;
            unlockDataDisplayStart = millis();
            break;
        case CHANGE_PASSWORD_OPTION:
            handleMenuStateTransition(PASSWORD_MENU);
            passwordMenuOption = SET_NEW_PASSWORD_OPTION;
            break;
        case DELETE_RECORDS_OPTION:
            menuState = DELETE_RECORDS_MESSAGE;
            deleteRecordsDisplayStart = millis();
            deleteRecords();
            break;
        case LOGOUT_OPTION:
            logout();
            break;
        case EXIT_OPTION:
            menuState = REALTIME_DISPLAY;
            break;
        default:
            break;
    }
}

void handleMenuButtonPress() {
    Serial.print("Menu button pressed in state: ");
    Serial.println(menuState);

    if (menuState == MAIN_MENU) {
        handleMainMenuSelection();
    } else if (menuState == PASSWORD_MENU) {
        handlePasswordMenuSelection();
    } else if (menuState == VIEW_RECORD_POSITION || menuState == VIEW_RECORD_PAGE || menuState == VIEW_DATA_EXIT) {
        // Properly reset the variables and exit the view data mode
        Serial.println("Exiting view data mode via handleMenuButtonPress().");
        viewDataContext.currentRecord = 0;
        viewDataContext.currentPage = 0;
        menuState = MAIN_MENU;
        mainMenuOption = WELCOME_OPTION;
        reinitializeOximeter();
    }
}

// Function to handle menu state transitions
void handleMenuStateTransition(MenuState newState) {
    Serial.print("Transitioning from ");
    Serial.print(menuState);
    Serial.print(" to ");
    Serial.println(newState);

    menuState = newState;
    if (newState == MAIN_MENU) {
        mainMenuOption = WELCOME_OPTION;
        // Reset the view data context
        viewDataContext.currentPage = 0;
        viewDataContext.currentRecord = 0;
    } else if (newState == PASSWORD_MENU) {
        passwordMenuOption = ENTER_PASSWORD_OPTION;
    }
}

void handlePasswordMenuSelection() {
    switch (passwordMenuOption) {
        case CONFIRM_PASSWORD_OPTION:
            if (isInitialPasswordSetup) {
                // Initial password setup
                char hash[HASH_LENGTH];
                char salt[SALT_LENGTH];
                generateSalt(salt, SALT_LENGTH);
                hashPassword(currentPassword, salt, hash);
                storeCredentials(hash, salt);
                isAuthenticated = true;
                memcpy(keepPassword, currentPassword, sizeof(currentPassword));
                isInitialPasswordSetup = false; // Reset the flag after setting the initial password
                menuState = PASSWORD_CHANGED;
                passwordChangedDisplayStart = millis();
            } else {
                if (mainMenuOption == CHANGE_PASSWORD_OPTION) {
                    char hash[HASH_LENGTH];
                    char salt[SALT_LENGTH];
                    generateSalt(salt, SALT_LENGTH);
                    hashPassword(currentPassword, salt, hash);
                    storeCredentials(hash, salt);
                    isAuthenticated = true;
                    memcpy(keepPassword, currentPassword, sizeof(currentPassword));
                    menuState = PASSWORD_CHANGED;
                    passwordChangedDisplayStart = millis();
                } else {
                    char enteredHash[HASH_LENGTH];
                    char storedHash[HASH_LENGTH];
                    char storedSalt[SALT_LENGTH];
                    loadCredentials(storedHash, storedSalt);
                    hashPassword(currentPassword, storedSalt, enteredHash);
                    if (memcmp(enteredHash, storedHash, HASH_LENGTH) == 0) {
                        isAuthenticated = true;
                        menuState = PASSWORD_CORRECT;
                        passwordCorrectDisplayStart = millis();
                        memcpy(keepPassword, currentPassword, sizeof(currentPassword));
                        memset(currentPassword, 0, sizeof(currentPassword));
                        passwordIndex = 0;
                        failedAttempts = 0;
                        lockoutEndTime = 0;
                    } else {
                        displayPasswordIncorrect();
                        delay(WAIT_TIME_MS);
                        memset(currentPassword, 0, sizeof(currentPassword));
                        passwordIndex = 0;
                        checkPasswordAttempts();
                        menuState = REALTIME_DISPLAY;
                    }
                }
            }
            break;
        case CANCEL_PASSWORD_OPTION:
            memset(currentPassword, 0, sizeof(currentPassword));
            passwordIndex = 0;
            menuState = REALTIME_DISPLAY;
            break;
        default:
            break;
    }
}

void handleRightButtonPress() {
    Serial.print("Right button pressed in state: ");
    Serial.println(menuState);

    if (menuState == MAIN_MENU) {
        mainMenuOption = static_cast<MainMenuOption>((mainMenuOption + 1) % MAIN_MENU_OPTION_COUNT);
    } else if (menuState == PASSWORD_MENU) {
        if (passwordMenuOption == ENTER_PASSWORD_OPTION || passwordMenuOption == SET_NEW_PASSWORD_OPTION) {
            if (passwordIndex < PASSWORD_LENGTH - 1) {
                updatePasswordIndex(1);
            } else {
                passwordMenuOption = CONFIRM_PASSWORD_OPTION;
            }
        } else if (passwordMenuOption == CONFIRM_PASSWORD_OPTION) {
            passwordMenuOption = CANCEL_PASSWORD_OPTION;
        }
    } else if (menuState == VIEW_RECORD_POSITION) {
        Serial.println("Transitioning to VIEW_RECORD_PAGE");
        menuState = VIEW_RECORD_PAGE;
    } else if (menuState == VIEW_RECORD_PAGE) {
        viewDataContext.currentPage++;
        Serial.print("Current Page: ");
        Serial.println(viewDataContext.currentPage);
        if (viewDataContext.currentPage >= PAGES_PER_RECORD) {
            viewDataContext.currentPage = 0;
            viewDataContext.currentRecord++;
            Serial.print("Current Record: ");
            Serial.println(viewDataContext.currentRecord);
            if (viewDataContext.currentRecord < viewDataContext.totalRecords) {
                Serial.println("Transitioning to VIEW_RECORD_POSITION");
                menuState = VIEW_RECORD_POSITION;
            } else {
                Serial.println("Transitioning to VIEW_DATA_EXIT");
                menuState = VIEW_DATA_EXIT;
            }
        }
    } else if (menuState == VIEW_DATA_EXIT) {
        Serial.println("Resetting view data context and transitioning to MAIN_MENU");
        viewDataContext.currentRecord = 0;
        viewDataContext.currentPage = 0;
        menuState = MAIN_MENU;
        mainMenuOption = WELCOME_OPTION;
        reinitializeOximeter();
        Serial.println("Exiting view data mode via handleRightButtonPress().");
    }
}

void handleUpButtonPress() {
    if (menuState == PASSWORD_MENU) {
        updateCurrentPasswordChar(1);
    }
}

void hashPassword(const char* password, const char* salt, char* hash) {
    sha256.reset();
    sha256.update((const byte*)password, strlen(password));
    sha256.update((const byte*)salt, SALT_LENGTH);
    sha256.finalize((byte*)hash, HASH_LENGTH);
}

bool isReadingValid(float heartRate, float spO2) {
    return heartRate >= HEART_RATE_MIN_VALID_THRESHOLD && spO2 >= SPO2_MIN_VALID_THRESHOLD;
}

bool loadCredentials(char* hash, char* salt) {
    CredentialsPrefs prefs;
    int rc = myFlashPrefs.readPrefs(&prefs, sizeof(prefs));
    if (rc != 0) {
        Serial.println("Failed to read credentials from flash memory.");
        return false;
    }
    memcpy(hash, prefs.hash, HASH_LENGTH);
    memcpy(salt, prefs.salt, SALT_LENGTH);
    salt[SALT_LENGTH] = '\0'; // Null-terminate the salt
    return true;
}

// Function to logout
void logout() {
    Serial.print("menuState: ");
    Serial.println(menuState);
    Serial.print("mainMenuOption: ");
    Serial.println(mainMenuOption);
    Serial.println("Logout option selected.");
    isAuthenticated = false;
    menuState = REALTIME_DISPLAY;
    mainMenuOption = WELCOME_OPTION;
    memset(currentPassword, 0, sizeof(currentPassword));
    passwordIndex = 0;
}

void onBeatDetected() {
    Serial.println("Beat detected");
    lastValidReading = millis();
}

void padData(const char* input, char* output, int dataSize) {
    // int paddingLength = 16 - (dataSize % 16);
    int paddingLength = 8; // 32 - 24 = 8 bytes padding

    memcpy(output, input, dataSize);
    memset(output + dataSize, paddingLength, paddingLength);
}

void performGarbageCollection() {
    ret_code_t ret = fds_gc();
    if (ret == FDS_SUCCESS) {
        Serial.println("Garbage collection completed successfully.");
    } else {
        Serial.print("Garbage collection failed with error code: ");
        Serial.println(ret);
    }
}

void printMemoryChunkHex(const void* memory, size_t size) {
    const uint8_t* byteMemory = reinterpret_cast<const uint8_t*>(memory);
    Serial.print("Memory chunk (size ");
    Serial.print(size);
    Serial.println(" bytes):");
    for (size_t i = 0; i < size; i++) {
        if (i % 16 == 0) {
            Serial.println();
            Serial.print("0x");
            Serial.print(i, HEX);
            Serial.print(": ");
        }
        if (byteMemory[i] < 0x10) {
            Serial.print("0");
        }
        Serial.print(byteMemory[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}

void reinitializeOximeter() {
    // For some reason, the oximeter sensor may fail after flash memory operations
    // and may need to be reinitialized to ensure proper functionality
    pox.begin();
    pox.setIRLedCurrent(MAX30100_LED_CURR_50MA);
    pox.setOnBeatDetectedCallback(onBeatDetected);
}

float roundToHalfPoint(float value) {
    return round(value * 2) / 2.0;
}

void setPassword() {
    displayPasswordPrompt("Set new password:");
}

float simpleMovingAverage(float *array, int size) {
    float sum = 0.0;
    for (int i = 0; i < size; i++) {
        sum += array[i];
    }
    float sma = sum / size;
    return sma;
}

void softReset() {
  NVIC_SystemReset();
}

void storeCredentials(const char* hash, const char* salt) {
    CredentialsPrefs prefs;
    memcpy(prefs.hash, hash, HASH_LENGTH);
    memcpy(prefs.salt, salt, SALT_LENGTH);
    int rc = myFlashPrefs.writePrefs(&prefs, sizeof(prefs));
    if (rc != 0) {
        Serial.println("Failed to store credentials in flash memory.");
    }
}

void unpadData(const char* input, char* output, int dataSize) {
    // We know the last byte should be 8 (since 32 - 24 = 8)
    // int paddingLength = input[dataSize - 1];
    int paddingLength = 8;

    //  // Validate padding length
    // if (paddingLength < 1 || paddingLength > 16) {
    //     Serial.print("Error: Invalid padding length: ");
    //     Serial.println(paddingLength);
    //     return;
    // }

    // // Validate padding bytes
    // for (int i = 0; i < paddingLength; i++) {
    //     if (input[dataSize - 1 - i] != paddingLength) {
    //         Serial.println("Error: Padding byte does not match expected pad value");
    //         return;
    //     }
    // }

    // Copy unpadded data to output
    memcpy(output, input, dataSize - paddingLength);
    
    // Optionally clear the rest of the output buffer to avoid leftover data
    memset(output + dataSize - paddingLength, 0, paddingLength);
}

void updateCurrentPasswordChar(int direction) {
    int currentCharIndex = (currentPassword[passwordIndex] == '\0') ? -1 : strchr(charSet, currentPassword[passwordIndex]) - charSet;
    currentCharIndex = (currentCharIndex + direction + charSetSize) % charSetSize;
    currentPassword[passwordIndex] = charSet[currentCharIndex];
}

void updatePasswordIndex(int direction) {
    passwordIndex = (passwordIndex + direction + PASSWORD_LENGTH) % PASSWORD_LENGTH;
}

void viewData() {
    menuState = VIEW_RECORD_POSITION; // Set the initial state to VIEW_RECORD_POSITION
    Serial.println("Entering viewData function");
    int rc = 0;
    char key[32];
    char iv[16];
    char hash[HASH_LENGTH];
    char salt[SALT_LENGTH];
    AESMemoryChunk aesMemoryChunk; // temporary AESMemoryChunk variable to populate viewDataContext.memoryChunk with decrypted data
    int recordCount = 0;

    Serial.println("Before credentials in viewData().");
    // Load credentials and derive key and IV
    if (!loadCredentials(hash, salt)) {
        Serial.println("Failed to load encryption credentials.");
        return;
    }

    Serial.println("After loading credentials in viewData(), now preparing key...");
    // Prepare key and IV for AES decryption
    deriveKeyAndIV(keepPassword, salt, key, iv);

    Serial.println("Key prepared in viewData().");
    // Read the whole encrypted memory to extract the stored records as decrypted data in viewDataContext.memoryChunk
    rc = myFlashPrefs.readPrefs(&aesMemoryChunk, sizeof(aesMemoryChunk));
    if (rc != 0) {
        Serial.println("Failed to read existing records.");
        return;
    }

    // Get the number of encrypted records
    recordCount = getEncryptedRecordsCount(aesMemoryChunk);
    Serial.print("Number of encrypted records: ");
    Serial.println(recordCount);

    // Decrypt the records
    decryptAllRecords(aesMemoryChunk, viewDataContext.memoryChunk, key, iv, recordCount);
    
    viewDataContext.totalRecords = getRecordsCount(viewDataContext.memoryChunk, MAX_RECORDS);
    Serial.print("Total records found in decrypted data: ");
    Serial.println(viewDataContext.totalRecords);

    // Debug: Print the contents of the flash buffer
    printMemoryChunkHex(&viewDataContext.memoryChunk, 256);

    if (viewDataContext.totalRecords == 0) {
        menuState = NO_RECORDS_MESSAGE;
        noRecordsDisplayStart = millis();
        return;
    }

    viewDataContext.currentRecord = 0;
    viewDataContext.currentPage = 0;

    while (menuState != MAIN_MENU) {
        if (viewDataContext.currentRecord < viewDataContext.totalRecords) {
            DataRecord record = viewDataContext.memoryChunk.healthAirSensorsDataBuffer[viewDataContext.totalRecords - viewDataContext.currentRecord - 1];
            Serial.print("Displaying Record ");
            Serial.print(viewDataContext.currentRecord + 1);
            Serial.print(" / ");
            Serial.println(viewDataContext.totalRecords);

            // Show "Record x / y" page first
            displayRecordPositionPage(viewDataContext.currentRecord + 1, viewDataContext.totalRecords);
            while (menuState == VIEW_RECORD_POSITION) {
                handleButtonPress();
                if (menuState == MAIN_MENU) {
                    return; // Exit if menuState changes to MAIN_MENU
                }
            }

            // Display record pages
            for (int page = 0; page < PAGES_PER_RECORD; ++page) {
                displayRecordPage(record, page);
                Serial.print("Displaying Record ");
                Serial.print(viewDataContext.currentRecord + 1);
                Serial.print(" Page ");
                Serial.println(page + 1);
                while (menuState == VIEW_RECORD_PAGE && viewDataContext.currentPage == page) {
                    handleButtonPress();
                    if (menuState == MAIN_MENU) {
                        return; // Exit if menuState changes to MAIN_MENU
                    }
                }
            }
            if (menuState == MAIN_MENU) break; // Exit if menuState changes to MAIN_MENU
        } else {
            // No more records to display
            displayNoMoreRecordsOption();
            Serial.println("No more records to display.");

           // Handle button presses for "No more records" and returning to main menu
            menuState = VIEW_DATA_EXIT;
            while (menuState == VIEW_DATA_EXIT) {
                handleButtonPress();
                if (menuState == MAIN_MENU) {
                    Serial.println("Exiting view data mode.");
                    return; // Exit view data mode
                }
            }
        }
    }

    // Properly reset the variables and exit the view data mode
    viewDataContext.currentRecord = 0;
    viewDataContext.currentPage = 0;
    reinitializeOximeter();
}

void writeBufferToFlash() {
    AESMemoryChunk aesMemoryChunk;
    MemoryChunk memoryChunk;
    int rc = 0, ret = 0;
    int existingRecords = 0, newRecordsToAdd = 0, totalRecords = 0, currentDataSize = 0;
    char ciphertext[36]; // 24 bytes data + up to 8 bytes padding + 4 bytes end-of-record indicator BEEF twice
    char key[32];
    char iv[16];
    char hash[HASH_LENGTH];
    char salt[SALT_LENGTH];
    performGarbageCollection();

    Serial.println("Entering writeBufferToFlash().");

    // Load existing credentials explicitly just to be sure
    if (!loadCredentials(hash, salt)) {
        Serial.println("Failed to load existing credentials.");
        return;
    }
    memcpy(memoryChunk.credentials.hash, hash, HASH_LENGTH);
    memcpy(memoryChunk.credentials.salt, salt, SALT_LENGTH);

    deriveKeyAndIV(keepPassword, salt, key, iv);

    Serial.println("Key and IV derived in writeBufferToFlash().");
    // Print key and IV for debugging
    Serial.print("Key: ");
    for (int i = 0; i < 32; i++) {
        Serial.print(key[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
    Serial.print("IV: ");
    for (int i = 0; i < 16; i++) {
        Serial.print(iv[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    // Read existing data from flash to preserve older records
    rc = myFlashPrefs.readPrefs(&aesMemoryChunk, sizeof(aesMemoryChunk));
        if (rc != 0) {
        Serial.println("Failed to read existing records.");
        Serial.println(myFlashPrefs.errorString(rc));
        Serial.println(myFlashPrefs.statusString());
        return;
    }

    existingRecords = getEncryptedRecordsCount(aesMemoryChunk);
    Serial.println("Existing records count: " + String(existingRecords));

    // Calculate the total number of records after adding the new ones
    newRecordsToAdd = dataRecordIndex;
    Serial.println("newRecordsToAdd = ");
    Serial.println(newRecordsToAdd);
    totalRecords = existingRecords + newRecordsToAdd;
    Serial.println("totalRecords = ");
    Serial.println(totalRecords);

    // Check if the new and existing records exceed the maximum allowed
    if ((totalRecords > MAX_RECORDS) || (overwriteBufferData == true)) {
        // If so, invalidate the entire memory after credentials
        memset(aesMemoryChunk.encryptedData, 0xFF, sizeof(aesMemoryChunk.encryptedData));
        // Reset existing records and total records to account for overwriting
        existingRecords = 0;
        totalRecords = newRecordsToAdd;
    }

    // Encrypt new records
    for (int i = 0; i < newRecordsToAdd; i++) {
        encryptRecord(healthAirSensorsDataBuffer[i], ciphertext, key, iv);
        memcpy(&aesMemoryChunk.encryptedData[existingRecords + i], ciphertext, sizeof(ciphertext));
    }

    // Write the combined data to flash
    rc = myFlashPrefs.writePrefs(&aesMemoryChunk, sizeof(aesMemoryChunk));
    if (rc != 0) {
        Serial.println("Failed to write data to flash memory.");
        Serial.println("Size to write:");
        Serial.println(sizeof(aesMemoryChunk));
        Serial.println(myFlashPrefs.errorString(rc));
        Serial.println(myFlashPrefs.statusString());
    } else {
        Serial.println("Data successfully written to flash memory.");
        dataRecordIndex = 0; // Reset the data record index after saving
    }

    // Debug: Print the contents of the flash buffer
    printMemoryChunkHex(&aesMemoryChunk, 256);

    // Reinitialize sensors if needed
    reinitializeOximeter();

    Serial.println("Exiting writeBufferToFlash().");
}
