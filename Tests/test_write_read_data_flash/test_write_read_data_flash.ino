#include <NanoBLEFlashPrefs.h>

#define FLASH_PAGE_SIZE 4000

typedef struct flashStruct {
  char flashBuffer[FLASH_PAGE_SIZE];
} flashPrefs;

NanoBLEFlashPrefs myFlashPrefs;
flashPrefs prefs;

void setup() {
    Serial.begin(115200);
    while (!Serial);
    int dataSize = 0;
    int rc = myFlashPrefs.readPrefs(&prefs, sizeof(prefs));
    for (int i=0; i<FLASH_PAGE_SIZE; i++)
    {
      if (prefs.flashBuffer[i] != 0xFF)
      {
          dataSize++;
      }
    }
    Serial.print("Total bytes read:");
    Serial.println(dataSize);
    // Print the contents of the flash buffer
    for (int i = 0; i < FLASH_PAGE_SIZE; i++) {
        Serial.print(prefs.flashBuffer[i], HEX);
        Serial.print(" ");
        // Print a new line every 16 bytes for better readability
        if ((i + 1) % 16 == 0) {
            Serial.println();
        }
    }
}

void loop() {
    // Nothing to do in loop
}
