#include "button.h"
#include "settings.h"

static bool lastRaw = false;
static bool stableState = false;
static unsigned long lastChangeMs = 0;
static const unsigned long DEBOUNCE_MS = 50;

void initButton() {
  if (buttonType == BTN_DISABLED || buttonPin == 0) return;
  if (buttonType == BTN_PUSH) {
    pinMode(buttonPin, INPUT_PULLUP);
  } else {  // BTN_TOUCH (TTP223)
    pinMode(buttonPin, INPUT);
  }
  lastRaw = false;
  stableState = false;
  lastChangeMs = 0;
}

bool wasButtonPressed() {
  if (buttonType == BTN_DISABLED || buttonPin == 0) return false;

  bool raw;
  if (buttonType == BTN_PUSH) {
    raw = (digitalRead(buttonPin) == LOW);   // active LOW with pull-up
  } else {
    raw = (digitalRead(buttonPin) == HIGH);  // TTP223: active HIGH
  }

  // Debounce
  if (raw != lastRaw) {
    lastChangeMs = millis();
    lastRaw = raw;
  }
  if ((millis() - lastChangeMs) < DEBOUNCE_MS) return false;

  // Rising edge detection
  bool result = false;
  if (raw && !stableState) {
    result = true;
  }
  stableState = raw;

  return result;
}
