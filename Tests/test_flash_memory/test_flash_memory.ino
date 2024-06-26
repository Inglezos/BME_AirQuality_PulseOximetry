#include <Arduino.h>
#include <NanoBLEFlashPrefs.h>

// Define the preferences structure
typedef struct {
    char password[16];
} FlashPrefs;

NanoBLEFlashPrefs myFlashPrefs;
FlashPrefs prefs;

void setup() {
    Serial.begin(115200);
    while (!Serial);

    // Read the stored preferences
    int rc = myFlashPrefs.readPrefs(&prefs, sizeof(prefs));
    Serial.print("Read status: ");
    Serial.println(myFlashPrefs.errorString(rc));

    // Check if a password is already stored
    if (rc == FDS_ERR_NOT_FOUND || strlen(prefs.password) == 0) {
        Serial.println("No password found. Enter a new password (up to 15 characters): ");
        while (Serial.available() == 0);  // Wait for user input
        int length = Serial.readBytesUntil('\n', prefs.password, sizeof(prefs.password) - 1);
        prefs.password[length] = '\0';  // Null-terminate the string

        // Write the new password to flash memory
        rc = myFlashPrefs.writePrefs(&prefs, sizeof(prefs));
        Serial.print("Write status: ");
        Serial.println(myFlashPrefs.errorString(rc));
        if (rc == FDS_SUCCESS) {
            Serial.println("Password stored in flash memory.");
        } else {
            Serial.println("Failed to store password.");
        }
    } else {
        Serial.println("Password already set in flash memory.");
        Serial.print("Stored Password: ");
        Serial.println(prefs.password);
    }
}

void loop() {
    // Just keep the loop empty for this simple example
}
