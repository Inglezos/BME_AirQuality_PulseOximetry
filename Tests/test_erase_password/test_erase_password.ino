#include <NanoBLEFlashPrefs.h>

NanoBLEFlashPrefs myFlashPrefs;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // Delete the stored password
  deleteStoredPassword();

  Serial.println("Password deleted.");
}

void loop() {
  // Nothing to do here
}

void deleteStoredPassword() {
  myFlashPrefs.deletePrefs();
}
