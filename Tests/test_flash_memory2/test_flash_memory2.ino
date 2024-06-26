#include <NanoBLEFlashPrefs.h>

// Preferences structure. Arbitrary, but must not exeed 1019 words (4076 byte)
typedef struct flashStruct {
	char someString[64];
	bool aSetting;
	int  someNumber;
	float anotherNumber;
} flashPrefs;

NanoBLEFlashPrefs myFlashPrefs;
flashPrefs prefsRead;
flashPrefs prefsWrite;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  strcpy(prefsWrite.someString, "NanoBLEFlashPrefs Test");
  prefsWrite.aSetting = true;
  prefsWrite.someNumber = 42;
  prefsWrite.anotherNumber = 3.14;
  // Write it to flash memory
  myFlashPrefs.writePrefs(&prefsWrite, sizeof(prefsWrite));
  int rc = myFlashPrefs.readPrefs(&prefsRead, sizeof(prefsRead));
  Serial.print("sizeRead = ");
  Serial.println(rc);
  Serial.print("someStringRead = ");
  Serial.println(prefsRead.someString);
  Serial.print("aRead = ");
  Serial.println(prefsRead.aSetting);
  Serial.print("someNumberRead = ");
  Serial.println(prefsRead.someNumber);
  Serial.print("anotherNumberRead = ");
  Serial.println(prefsRead.anotherNumber);
}

void loop() {}