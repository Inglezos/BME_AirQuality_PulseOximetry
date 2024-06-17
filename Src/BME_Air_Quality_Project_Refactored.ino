#include "BMEAirQualityHeart.h"

void setup() {
    Serial.begin(115200);
    while (!Serial);

    // Initialize OLED
    Serial.println("Initializing OLED...");
    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "Initializing...");
    u8g2.sendBuffer();
    Serial.println("OLED initialized.");

    // Initialize HM3301
    Serial.println("Initializing HM330X sensor...");
    if (sensor.init()) {
        Serial.println("HM330X init failed!!");
        while (1);
    } else {
        Serial.println("HM330X init success.");
    }

    // Initialize MQ7
    Serial.println("Calibrating MQ7 sensor...");
    mq7.calibrate();
    Serial.println("MQ7 Calibration done!");

    // Initialize Pulse Oximeter
    Serial.print("Initializing pulse oximeter...");
    if (!pox.begin()) {
        Serial.println("FAILED");
        while (1);
    } else {
        Serial.println("SUCCESS");
    }
    pox.setIRLedCurrent(MAX30100_LED_CURR_50MA);
    pox.setOnBeatDetectedCallback(onBeatDetected);

    lastDisplayUpdate = millis();
    memset(riskStates, 0, sizeof(riskStates));

    // Initialize button pins
    pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
    pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);
    pinMode(BUTTON_LEFT_PIN, INPUT_PULLUP);
    pinMode(BUTTON_RIGHT_PIN, INPUT_PULLUP);
    pinMode(BUTTON_MENU_PIN, INPUT_PULLUP);

    // Initialize buzzer pin
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    performGarbageCollection();

    // Load credentials from flash memory
    char hash[HASH_LENGTH];
    char salt[SALT_LENGTH];
    if (!loadCredentials(hash, salt)) {
        // Prompt user to set a password
        Serial.println("Setting initial password...");
        isInitialPasswordSetup = true;
        handleMenuStateTransition(PASSWORD_MENU);
        passwordMenuOption = SET_NEW_PASSWORD_OPTION;

        bool passwordSet = false;
        while (!passwordSet) {
            displayMenuOption();
            handleButtonPress();
            if (menuState == PASSWORD_CHANGED) {
                passwordSet = true;
                displayPasswordFirstTimeSet();
                delay(WAIT_TIME_MS);
                softReset();
                handleMenuStateTransition(MAIN_MENU);
            }
        }
    } else {
        // Transition to real-time display after initialization
        menuState = REALTIME_DISPLAY;
    }
}

void loop() {
    uint32_t currentMillis = millis();
    pox.update();

    // Check if currently locked out after exceeding password attempts
    if (currentMillis < lockoutEndTime) {
        Serial.println("Locked out due to too many failed attempts.");
        displayLockoutMessage(); // Display a lockout message on the display
        return; // Exit the loop to ignore any button presses
    } else if (failedAttempts == MAX_PASSWORD_ATTEMPTS) {
        // Reset failed attempts after the lockout period ends
        failedAttempts = 0;
        lockoutEndTime = 0;
    }

    // Continue sensor updates even in menu state
    if (currentMillis - tsLastReport > PULSE_OXIMETER_READ_INTERVAL) {
        pox.update();
        heartRate = pox.getHeartRate();
        spO2 = pox.getSpO2();
        // Serial.print("Raw Heart Rate: ");
        // Serial.print(heartRate);
        // Serial.print(" bpm, Raw SpO2: ");
        // Serial.print(spO2);
        // Serial.println(" %");
        tsLastReport = currentMillis;
        filteredHeartRate = filterData(heartRate, heartRateReadings, readingsIndex[0], emaValues[0], demaValues[0], 0);
        filteredSpO2 = filterData(spO2, spO2Readings, readingsIndex[1], emaValues[1], demaValues[1], 1);
        if (isReadingValid(filteredHeartRate, filteredSpO2)) {
            lastValidReading = currentMillis;
            if (stabilizationStart == 0) {
                stabilizationStart = currentMillis;  // Start stabilization period
            }
        } else {
            // Readings are invalid for a prolonged period
            if (currentMillis - lastValidReading > INVALID_READING_TIMEOUT) {
                filteredHeartRate = 0;
                filteredSpO2 = 0;
                stabilizationStart = 0;  // Reset stabilization start time
            }
        }

        // Round heart rate to nearest integer or half-point
        filteredHeartRate = roundToHalfPoint(filteredHeartRate);

        readingsIndex[0] = (readingsIndex[0] + 1) % AVERAGE_WINDOW;
        readingsIndex[1] = (readingsIndex[1] + 1) % AVERAGE_WINDOW;

        // Serial.print("Filtered Heart Rate: ");
        // Serial.print(filteredHeartRate);
        // Serial.print(" bpm, Filtered SpO2: ");
        // Serial.print(filteredSpO2);
        // Serial.println(" %");
    }

    pox.update();
    heartRate = pox.getHeartRate();
    spO2 = pox.getSpO2();
    // Read other sensor data every AIR_QUALITY_READ_INTERVAL
    if (currentMillis - lastSensorRead > AIR_QUALITY_READ_INTERVAL) {
        lastSensorRead = currentMillis;

        // Read PM2.5 data
        if (sensor.read_sensor_value(buf, 29)) {
            Serial.println("HM330X read result failed!!");
        } else {
            pm25 = (buf[4] << 8) | buf[5];
            filteredPM25 = filterData(pm25, pm25Readings, readingsIndex[2], emaValues[2], demaValues[2], 2);
            readingsIndex[2] = (readingsIndex[2] + 1) % AVERAGE_WINDOW;

            // Serial.print("Filtered PM2.5: ");
            // Serial.println(filteredPM25);
        }

        // Read MQ2 data
        mq2Value = analogRead(MQ2_PIN);
        // Map 10-bit analog value to 8-bit value
        mq2Value = map(mq2Value, 0, 1023, 0, 255);
        filteredMQ2 = filterData(mq2Value, mq2Readings, readingsIndex[3], emaValues[3], demaValues[3], 3);
        readingsIndex[3] = (readingsIndex[3] + 1) % AVERAGE_WINDOW;

        // Serial.print("Filtered MQ2: ");
        // Serial.println(filteredMQ2);

        // Read MQ7 data
        mq7Ppm = mq7.readPpm();
        filteredMQ7 = filterData(mq7Ppm, mq7Readings, readingsIndex[4], emaValues[4], demaValues[4], 4);
        readingsIndex[4] = (readingsIndex[4] + 1) % AVERAGE_WINDOW;

        // Serial.print("Filtered MQ7: ");
        // Serial.println(filteredMQ7);
    }

    pox.update();
    heartRate = pox.getHeartRate();
    spO2 = pox.getSpO2();
    // Handle menu state
    if (menuState != REALTIME_DISPLAY) {
        if (menuState == MAIN_MENU || menuState == PASSWORD_MENU || menuState == WAIT) {
            displayMenuOption();
            handleButtonPress();
        } else if (menuState == PASSWORD_CORRECT) {
            displayPasswordCorrect();
            if (currentMillis - passwordCorrectDisplayStart >= WAIT_TIME_MS) {
                menuState = MAIN_MENU;
                mainMenuOption = WELCOME_OPTION;
            }
        } else if (menuState == PASSWORD_CHANGED) {
            displayPasswordChanged();
            if (currentMillis - passwordChangedDisplayStart >= WAIT_TIME_MS) {
                menuState = MAIN_MENU;
                mainMenuOption = WELCOME_OPTION;
                softReset();
            }
        } else if (menuState == LOCKED_MESSAGE) {
            displayLockMessage();
            if (currentMillis - lockDataDisplayStart >= WAIT_TIME_MS) {
                menuState = REALTIME_DISPLAY;
            }
        } else if (menuState == UNLOCKED_MESSAGE) {
            displayUnlockMessage();
            if (currentMillis - unlockDataDisplayStart >= WAIT_TIME_MS) {
                menuState = REALTIME_DISPLAY;
            }
        } else if (menuState == LOGGING_ON_MESSAGE) {
            displayLoggingOnMessage();
            if (currentMillis - loggingDisplayStart >= WAIT_TIME_MS) {
                menuState = REALTIME_DISPLAY;
            }
        } else if (menuState == LOGGING_OFF_MESSAGE) {
            displayLoggingOffMessage();
            if (currentMillis - loggingDisplayStart >= WAIT_TIME_MS) {
                menuState = REALTIME_DISPLAY;
            }
        } else if (menuState == DATA_SAVED_MESSAGE) {
            displayDataSavedMessage();
            if (currentMillis - dataSavedDisplayStart >= WAIT_TIME_MS) {
                menuState = REALTIME_DISPLAY;
            }
        } else if (menuState == DELETE_RECORDS_MESSAGE) {
            if (currentMillis - deleteRecordsDisplayStart >= WAIT_TIME_MS) {
                menuState = MAIN_MENU;
                mainMenuOption = WELCOME_OPTION;
            }
        } else if (menuState == NO_RECORDS_MESSAGE) {
            displayNoRecordsMessage();
            if (currentMillis - noRecordsDisplayStart >= WAIT_TIME_MS) {
                menuState = REALTIME_DISPLAY;
            }
        } else if (menuState == WAIT_MESSAGE) {
            displayWaitMessage();
            if (currentMillis - waitMessageDisplayStart >= WAIT_TIME_MS) {
                if (!isAuthenticated) {
                    Serial.println("isAuthenticated = ");
                    Serial.println(isAuthenticated);
                    Serial.println("menuState = ");
                    Serial.println(menuState);
                    Serial.println("passwordMenuOption = ");
                    Serial.println(passwordMenuOption);
                    handleMenuStateTransition(PASSWORD_MENU);
                    passwordMenuOption = ENTER_PASSWORD_OPTION;
                    memset(currentPassword, 0, sizeof(currentPassword)); // Clear entered password
                    passwordIndex = 0;
                } else {
                    handleMenuStateTransition(MAIN_MENU);
                }
            }
        }
        return;  // Skip the rest of the loop if we're in the menu
    }

    pox.update();
    heartRate = pox.getHeartRate();
    spO2 = pox.getSpO2();
    // Check if user is at risk
    bool stabilized = currentMillis - stabilizationStart > STABILIZATION_PERIOD;
    RiskCause riskCause = determineRiskCause(filteredPM25, filteredMQ2, filteredMQ7, filteredHeartRate, filteredSpO2);
    bool risk = stabilized && (riskCause != NONE_RISK_CAUSE);
    riskStates[riskIndex] = risk;
    riskIndex = (riskIndex + 1) % (RISK_WINDOW_SECONDS * 1000 / PULSE_OXIMETER_READ_INTERVAL);

    // for 100 ms read interval and RISK_WINDOW_SECONDS = 30 sec, a persistent risk state gets detected
    // if half of the readings inside this time interval indicate risk state, which means for 30000/100 * 0.5 = 150 readings.
    // This corresponds to 150*100ms = 15 sec. Also the buzzer will sound at 15 sec + 5 sec = 20 sec during persistent risk state for this long.
    bool persistentRisk = checkRiskWindow();

    // In case a persistent risk state gets detected, update the buzzer time variable lastRiskDetection
    if (persistentRisk && (lastRiskDetection == 0)) {
        lastRiskDetection = currentMillis;
    }
    // Handle buzzer operation depending on risk state
    checkRiskAndSoundBuzzer(persistentRisk);

    // Non-blocking display updates
    if (persistentRisk) {
        static uint32_t lastAlertDisplayTime = 0;
        static int alertDisplayState = 0;

        if (currentMillis - lastAlertDisplayTime >= DISPLAY_PERIOD_MS) {
            lastAlertDisplayTime = currentMillis;

            switch (alertDisplayState) {
                case 0:
                    displayAlert();
                    break;
                case 1:
                    displayRiskCause(riskCause);
                    break;
                case 2:
                    displayAlert();
                    break;
                case 3:
                    if (!lockMode) {
                        displayHealthSensorValues();
                    } else {
                        displayAlert();
                    }
                    break;
                case 4:
                    displayAlert();
                    break;
                case 5:
                    displayAirQualityValues();
                    break;
            }

            alertDisplayState = (alertDisplayState + 1) % 6;
        }
    } else {
        lastRiskDetection = 0;
        if (currentMillis - lastDisplayUpdate > DISPLAY_PERIOD_MS) {
            lastDisplayUpdate = currentMillis;
            displayPage = (displayPage + 1) % 2;
        }

        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);

        if (lockMode) {
            displayAirQualityValues();
        } else {
            if (displayPage == 0) {
                displayHealthSensorValues();
            } else {
                displayAirQualityValues();
            }
        }

        u8g2.sendBuffer();
    }

    pox.update();
    heartRate = pox.getHeartRate();
    spO2 = pox.getSpO2();
    // Check for menu button hold
    if (digitalRead(BUTTON_MENU_PIN) == LOW) {
        if (!menuButtonHeld) {
            menuButtonHoldStart = millis();
            menuButtonHeld = true;
        } else if (millis() - menuButtonHoldStart >= MENU_HOLD_TIME_MS) {
            menuState = WAIT_MESSAGE;
            waitMessageDisplayStart = millis();
            menuButtonHeld = false;
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_ncenB08_tr);
            u8g2.drawStr(0, 10, "Please wait...");
            u8g2.sendBuffer();
        }
    } else {
        menuButtonHeld = false;
    }

    // Logging logic, either automatically or due to risk state
    if ((loggingEnabled && (currentMillis - lastLoggingTime >= LOGGING_INTERVAL_MS)) || (persistentRisk && (currentMillis - lastRiskStateLoggingTime >= RISK_STATE_LOGGING_INTERVAL_MS))) {
        addRecordToBuffer(filteredHeartRate, filteredSpO2, filteredPM25, filteredMQ2, filteredMQ7, risk, riskCause);
        lastLoggingTime = currentMillis;
        lastRiskStateLoggingTime = currentMillis;
        writeBufferToFlash();
    }
}
