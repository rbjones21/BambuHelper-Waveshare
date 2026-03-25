#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

void initButton();
bool wasButtonPressed();  // returns true once per tap (edge-detected, debounced)

#endif // BUTTON_H
