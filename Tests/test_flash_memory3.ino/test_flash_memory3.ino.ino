#include <Wire.h>
#include <Seeed_HM330X.h>
#include "MQ7.h"
#include "MAX30100_PulseOximeter.h"
#include <U8g2lib.h>
#include <NanoBLEFlashPrefs.h>
#include <Crypto.h>
#include <SHA256.h>
#include <myAES.h>

#define FLASH_PAGE_SIZE 1024

NanoBLEFlashPrefs myFlashPrefs;

void setup() {
    Serial.begin(115200);
    while (!Serial);

    Serial.println("Flash memory test starting...");

    // Perform garbage collection before any other operation
    performGarbageCollection();

    // Write test data to flash
    writeTestData();

    // Read test data from flash
    readTestData();
}

void loop() {
    // Do nothing in loop
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

void writeTestData() {
    uint8_t buffer[FLASH_PAGE_SIZE];
    memset(buffer, 0xAB, FLASH_PAGE_SIZE); // Fill with test data

    int rc = myFlashPrefs.writePrefs(buffer, FLASH_PAGE_SIZE);
    if (rc != 0) {
        Serial.print("Failed to write data to flash memory. Error code: ");
        Serial.println(rc);

        // If there's no space, perform garbage collection and retry
        if (rc == FDS_ERR_NO_SPACE_IN_FLASH) {
            Serial.println("No space in flash, performing garbage collection...");
            performGarbageCollection();
            rc = myFlashPrefs.writePrefs(buffer, FLASH_PAGE_SIZE);
            if (rc != 0) {
                Serial.print("Failed to write data to flash memory after garbage collection. Error code: ");
                Serial.println(rc);
            } else {
                Serial.println("Data successfully written to flash memory after garbage collection.");
            }
        }
    } else {
        Serial.println("Data successfully written to flash memory.");
    }
}

void readTestData() {
    uint8_t buffer[FLASH_PAGE_SIZE];
    int dataSize = myFlashPrefs.readPrefs(buffer, FLASH_PAGE_SIZE);
    if (dataSize < 0) {
        Serial.println("Error reading flash memory.");
        return;
    }

    Serial.println("Data read from flash memory:");
    for (int i = 0; i < dataSize; i++) {
        Serial.print(buffer[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}
