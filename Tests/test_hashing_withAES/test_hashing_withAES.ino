#include <Wire.h>
#include <Seeed_HM330X.h>
#include "MQ7.h"
#include "MAX30100_PulseOximeter.h"
#include <U8g2lib.h>
#include <NanoBLEFlashPrefs.h>
#include <Crypto.h>
#include <SHA256.h>
#include <myAES.h>

#define PASSWORD_LENGTH 8
#define HASH_LENGTH 32
#define SALT_LENGTH 16
#define AES_KEY_LENGTH 16 // Using the last 16 bytes of the hash in reverse order as the key
#define DATA_LENGTH 16 // Length of the data to encrypt (16 bytes for heart rate and 16 bytes for SpO2)
#define IV_LENGTH 16 // Length of the initialization vector for AES

char inputPassword[PASSWORD_LENGTH + 1] = "12345678"; // Example password

SHA256 sha256;
NanoBLEFlashPrefs myFlashPrefs;
AES aes;

// Preferences structure for storing hashed password and salt
typedef struct {
    char hashedPassword[HASH_LENGTH];
    char salt[SALT_LENGTH];
} FlashPrefs;

FlashPrefs prefs;

// Fixed IV for simplicity
const char aes_iv[IV_LENGTH + 1] = "RoboticsBME2024!";

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

// Function to derive AES key from hashed password
void deriveKey(const char* hashedPassword, char* key) {
    for (int i = 0; i < AES_KEY_LENGTH; i++) {
        key[i] = hashedPassword[HASH_LENGTH - 1 - i];
    }
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

// Function to encrypt data
void encryptData(const char* plaintext, char* ciphertext, const char* key, const char* iv) {
    aes.setup(key, AES::KEY_256, AES::MODE_CBC, iv);
    aes.encrypt(plaintext, ciphertext, DATA_LENGTH);
    aes.clear();
}

// Function to decrypt data
void decryptData(const char* ciphertext, char* plaintext, const char* key, const char* iv) {
    aes.setup(key, AES::KEY_256, AES::MODE_CBC, iv);
    aes.decrypt(ciphertext, plaintext, DATA_LENGTH);
    aes.clear();
}

// Function to print data in hexadecimal format
void printHex(const char *label, const char *data, size_t length) {
    Serial.print(label);
    for (size_t i = 0; i < length; i++) {
        if ((i % 16) == 0 && i != 0) {
            Serial.println();
        }
        if (data[i] < 0x10) {
            Serial.print("0");
        }
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}

void setup() {
    Serial.begin(9600);
    while (!Serial);

    char storedHash[HASH_LENGTH];
    char salt[SALT_LENGTH];
    char aesKey[AES_KEY_LENGTH];
    char heartRateData[DATA_LENGTH] = "70"; // Example heart rate data
    char spO2Data[DATA_LENGTH] = "95"; // Example SpO2 data
    char encryptedHeartRate[DATA_LENGTH];
    char encryptedSpO2[DATA_LENGTH];
    char decryptedHeartRate[DATA_LENGTH];
    char decryptedSpO2[DATA_LENGTH];

    // Generate and store salt, hash, and PIN (only for the first time)
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

        // Derive AES key from hashed password
        deriveKey(storedHash, aesKey);

        // Encrypt heart rate and SpO2 data
        encryptData(heartRateData, encryptedHeartRate, aesKey, aes_iv);
        encryptData(spO2Data, encryptedSpO2, aesKey, aes_iv);

        // Print encrypted data
        printHex("Encrypted Heart Rate: ", encryptedHeartRate, DATA_LENGTH);
        printHex("Encrypted SpO2: ", encryptedSpO2, DATA_LENGTH);

        // Decrypt heart rate and SpO2 data
        decryptData(encryptedHeartRate, decryptedHeartRate, aesKey, aes_iv);
        decryptData(encryptedSpO2, decryptedSpO2, aesKey, aes_iv);

        // Print decrypted data
        Serial.print("Decrypted Heart Rate: ");
        Serial.println(decryptedHeartRate);
        Serial.print("Decrypted SpO2: ");
        Serial.println(decryptedSpO2);
    } else {
        Serial.println("Password is incorrect.");
    }
}

void loop() {
    // Nothing to do in loop
}
