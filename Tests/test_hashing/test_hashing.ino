#include <Wire.h>
#include <Seeed_HM330X.h>
#include "MQ7.h"
#include "MAX30100_PulseOximeter.h"
#include <U8g2lib.h>
#include <NanoBLEFlashPrefs.h>
#include <Crypto.h>
#include <SHA256.h>

#define PASSWORD_LENGTH 8
#define HASH_LENGTH 32
#define SALT_LENGTH 16

char inputPassword[PASSWORD_LENGTH + 1] = "12345678"; // Example password

SHA256 sha256;
NanoBLEFlashPrefs myFlashPrefs;

// Preferences structure for storing hashed password and salt
typedef struct {
    char hashedPassword[HASH_LENGTH];
    char salt[SALT_LENGTH];
} FlashPrefs;

FlashPrefs prefs;

// Function to generate a random salt
void generateSalt(char* salt, size_t length) {
    for (size_t i = 0; i < length; i++) {
        salt[i] = random(0, 256);
    }
}

// Function to hash a password with a salt
void hashPassword(const char* password, const char* salt, char* hash) {
    sha256.reset();
    sha256.update((const byte*)password, strlen(password));
    sha256.update((const byte*)salt, SALT_LENGTH);
    sha256.finalize((byte*)hash, HASH_LENGTH);
}

// Function to store hash and salt in flash memory
void storeCredentials(const char* hash, const char* salt) {
    memcpy(prefs.hashedPassword, hash, HASH_LENGTH);
    memcpy(prefs.salt, salt, SALT_LENGTH);
    int rc = myFlashPrefs.writePrefs(&prefs, sizeof(prefs));
    Serial.print("Write status: ");
    Serial.println(myFlashPrefs.errorString(rc));
}

// Function to load hash and salt from flash memory
bool loadCredentials(char* hash, char* salt) {
    int rc = myFlashPrefs.readPrefs(&prefs, sizeof(prefs));
    if (rc != FDS_SUCCESS) {
        Serial.print("Read status: ");
        Serial.println(myFlashPrefs.errorString(rc));
        return false;
    }
    memcpy(hash, prefs.hashedPassword, HASH_LENGTH);
    memcpy(salt, prefs.salt, SALT_LENGTH);
    return true;
}

// Function to verify the input password
bool verifyPassword(const char* inputPassword, const char* storedHash, const char* salt) {
    char inputHash[HASH_LENGTH];
    hashPassword(inputPassword, salt, inputHash);
    return memcmp(inputHash, storedHash, HASH_LENGTH) == 0;
}

void setup() {
    Serial.begin(9600);
    while (!Serial);

    char storedHash[HASH_LENGTH];
    char salt[SALT_LENGTH];

    // Generate and store salt and hash (only for the first time)
    if (!loadCredentials(storedHash, salt)) { // Check if flash is empty or read failed
        generateSalt(salt, SALT_LENGTH);
        hashPassword(inputPassword, salt, storedHash);
        storeCredentials(storedHash, salt);
        Serial.println("Stored hash and salt in flash memory.");
    } else {
        Serial.println("Loaded hash and salt from flash memory.");
    }

    // Verify the password
    if (verifyPassword(inputPassword, storedHash, salt)) {
        Serial.println("Password is correct.");
    } else {
        Serial.println("Password is incorrect.");
    }
}

void loop() {
    // Nothing to do in loop
}
