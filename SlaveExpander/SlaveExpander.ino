/*
  Slave Leonardo Code (I²C Communication)
  -----------------------------------------
  Reads 11 buttons (“left‑side”) with the following assignments:
  
    Updated Mapping (each button is wired so that pressing it pulls the pin LOW):
      Index 0: A          → Digital 7
      Index 1: L          → Digital 8
      Index 2: Left Coral → Digital 9
      Index 3: A1         → Digital 10
      Index 4: A2         → Digital 11
      Index 5: K          → Digital 12
      Index 6: H          → Digital 13
      Index 7: I          → Digital 4    (changed from A4)
      Index 8: Hopper     → Digital 5    (changed from A5)
      Index 9: Barge      → Digital A3
      Index 10: J         → Digital A0
  
  When the master requests data via I²C, the slave immediately reads the pins,
  packs the states into two bytes, and sends them:
    - Byte 1: Bits 0–6 for indices 0–6.
    - Byte 2: Bits 0–3 for indices 7–10.
  
  Uncomment DEBUG_ENABLED to enable USB Serial debugging.
*/

#define DEBUG_ENABLED  // Comment this out to disable debug output

#include <Wire.h>
#include <Arduino.h>

// Number of slave buttons.
#define NUM_SLAVE_BUTTONS 11
// I²C slave address (must match the master's request address).
#define SLAVE_ADDR 0x04

// Digital pins for each button (active LOW).
const int slavePins[NUM_SLAVE_BUTTONS] = {
  7,    // Index 0: A
  8,    // Index 1: L
  9,    // Index 2: Left Coral
  10,   // Index 3: A1
  11,   // Index 4: A2
  12,   // Index 5: K
  13,   // Index 6: H
  4,    // Index 7: I     (changed from A4)
  5,    // Index 8: Hopper (changed from A5)
  A3,   // Index 9: Barge
  A0    // Index 10: J
};

#ifdef DEBUG_ENABLED
// Array to track the previous state of each button (for debug transition printing)
bool lastSlaveState[NUM_SLAVE_BUTTONS] = {0};
#endif

// This function is called when the master requests data.
void requestEvent() {
  uint8_t byte1 = 0;
  uint8_t byte2 = 0;
  
  // Pack the states for indices 0–6 into byte1.
  for (int i = 0; i < 7; i++) {
    if (digitalRead(slavePins[i]) == LOW) { // Button is pressed.
      byte1 |= (1 << i);
    }
  }
  
  // Pack the states for indices 7–10 into byte2.
  for (int i = 7; i < NUM_SLAVE_BUTTONS; i++) {
    if (digitalRead(slavePins[i]) == LOW) {
      byte2 |= (1 << (i - 7));
    }
  }
  
  // Send the two bytes over I²C.
  Wire.write(byte1);
  Wire.write(byte2);
  
#ifdef DEBUG_ENABLED
  // Note: Debug prints in an I²C request event may affect timing.
  Serial.print("Sent Slave Bytes: 0x");
  Serial.print(byte1, HEX);
  Serial.print("  0x");
  Serial.println(byte2, HEX);
#endif
}

void setup() {
  // Initialize each slave button pin as INPUT_PULLUP.
  for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
    pinMode(slavePins[i], INPUT_PULLUP);
  }
  
  // Begin I²C as a slave with the given address.
  Wire.begin(SLAVE_ADDR);
  Wire.onRequest(requestEvent);
  
#ifdef DEBUG_ENABLED
  Serial.begin(9600);
  while (!Serial) { } // Wait for Serial Monitor (Leonardo-specific)
  Serial.println("Slave: Debug Serial enabled (I2C mode).");
#endif
}

void loop() {
#ifdef DEBUG_ENABLED
  // Optional: print debug messages on button press transitions.
  for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
    bool pressed = (digitalRead(slavePins[i]) == LOW);
    if (pressed && !lastSlaveState[i]) {
      Serial.print("Slave Button (Pin ");
      Serial.print(slavePins[i]);
      Serial.print(", Index ");
      Serial.print(i);
      Serial.println(") PRESSED");
    }
    lastSlaveState[i] = pressed;
  }
#endif
  delay(50);  // Debounce/update delay.
}
