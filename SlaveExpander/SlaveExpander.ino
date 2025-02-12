//#define DEBUG_ENABLED

#include <Wire.h>
#include <Arduino.h>

// ------------------------------------------------------------------
// SLAVE: Purely acts as a DIO extender
// Reads 11 digital inputs, packs them into 2 bytes, and returns them
// to the Master upon request.
// ------------------------------------------------------------------

// I2C address for this Slave
#define SLAVE_ADDR 0x04

// List of pins for the 11 buttons
// Index meaning (example):
//   0 => Hopper
//   1 => A2 (height)
//   2 => A1 (height)
//   3 => LeftCoral
//   4 => Barge
//   5 => H
//   6 => I
//   7 => J
//   8 => K
//   9 => L
//   10 => A
const int NUM_SLAVE_BUTTONS = 11;
const int slavePins[NUM_SLAVE_BUTTONS] = {
  9,   // index 0 => Hopper
  8,   // index 1 => A2
  7,   // index 2 => A1
  A2,  // index 3 => LeftCoral
  11,  // index 4 => Barge
  13,  // index 5 => H
  12,  // index 6 => I
  10,  // index 7 => J
  A3,  // index 8 => K
  A1,  // index 9 => L
  A0   // index 10 => A
};

// Called when the Master requests data
void requestEvent() {
  // We'll pack the 11 button states into 2 bytes:
  //   byte1: bits 0..7 for slavePins[0..7]
  //   byte2: bits 0..2 for slavePins[8..10]

  uint8_t byte1 = 0;
  uint8_t byte2 = 0;

  // First 8 buttons -> byte1
  for (int i = 0; i < 8; i++) {
    if (i < NUM_SLAVE_BUTTONS) {
      bool pressed = (digitalRead(slavePins[i]) == LOW);
      if (pressed) {
        byte1 |= (1 << i);
      }
    }
  }

  // Next 3 buttons -> bits 0..2 of byte2 (indices 8..10)
  for (int i = 8; i < NUM_SLAVE_BUTTONS; i++) {
    bool pressed = (digitalRead(slavePins[i]) == LOW);
    if (pressed) {
      byte2 |= (1 << (i - 8));
    }
  }

#ifdef DEBUG_ENABLED
  Serial.print("Slave sending bytes: 0x");
  Serial.print(byte1, HEX);
  Serial.print(" 0x");
  Serial.println(byte2, HEX);
#endif

  // Send the two bytes back to the Master
  Wire.write(byte1);
  Wire.write(byte2);
}

void setup() {
  // Configure each pin as INPUT_PULLUP
  for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
    pinMode(slavePins[i], INPUT_PULLUP);
  }

  // Start I2C as slave
  Wire.begin(SLAVE_ADDR);
  Wire.onRequest(requestEvent);

#ifdef DEBUG_ENABLED
  Serial.begin(9600);
  while (!Serial) { /* Wait for USB Serial on some boards */ }
  Serial.println("Slave: I2C DIO Extender ready.");
#endif
}

void loop() {
  // No toggling or logic here; purely digital reads in requestEvent().
  delay(50);
}
