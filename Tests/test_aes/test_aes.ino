#include <Arduino.h>
#include <myAES.h>

// Define your key and IV here
const char aes_key[] = "01234567890123456789012345678901"; // 32 bytes for AES-256
const char aes_iv[] = "0123456789012345"; // 16 bytes for AES

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

void printText(const char *label, const char *data, size_t length) {
    Serial.print(label);
    for (size_t i = 0; i < length; i++) {
        Serial.print(data[i]);
    }
    Serial.println();
}

void setup() {
    Serial.begin(9600);
    while (!Serial) {}

    // Message to encrypt
    char message[] = "Hello, AES-256 CBC!";
    size_t message_length = strlen(message);

    // Ensure the message is a multiple of 16 bytes for AES block size
    size_t padded_length = (message_length / 16 + 1) * 16;
    char padded_message[padded_length];
    strcpy(padded_message, message);

    char encryptedText[padded_length];
    char decryptedText[padded_length];

    // AES Encryption
    AES aes;
    aes.setup(aes_key, AES::KEY_256, AES::MODE_CBC, aes_iv);
    aes.encrypt((char*)padded_message, (char*)encryptedText, padded_length);
    aes.clear();

    // Print encrypted text
    printHex("Encrypted Text: ", encryptedText, padded_length);

    // AES Decryption
    aes.setup(aes_key, AES::KEY_256, AES::MODE_CBC, aes_iv);
    aes.decrypt((char*)encryptedText, (char*)decryptedText, padded_length);
    aes.clear();

    // Print decrypted text in hex and normal
    printHex("Decrypted Text (Hex): ", decryptedText, padded_length);
    printText("Decrypted Text (Normal): ", decryptedText, message_length);
}

void loop() {
    // Nothing here
}
