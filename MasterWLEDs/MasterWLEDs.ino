/*
  Master Leonardo Code – Two‑Way I²C with Toggle Selections and Serial Output
  ----------------------------------------------------------------------------
  This sketch reads 13 local (right‑side) buttons using toggle logic and polls the slave (address 0x04)
  via I²C to obtain the slave’s toggle selections for location and height.
  
  Toggle behavior:
    • A rising edge on a button in a category (location or height) sets that button as the active selection.
    • The active selection remains “on” (reported as pressed by the Joystick library and lit on the LED)
      even after the physical button is released.
    • If a new button in the same category is toggled, it replaces the previous selection.
  
  Priority rule:
    • If a master toggle is active, it takes precedence. The master then commands the slave to clear its
      selection for that category.
    • Otherwise, if the slave has a toggle selection, that is used.
  
  The final (global) selection is reported via the USB Joystick interface and also printed over Serial.
*/

#define DEBUG_ENABLED

#include <Wire.h>
#include <Joystick.h>
#include <Adafruit_NeoPixel.h>

// ------------------ I2C & Command Definitions ------------------
#define SLAVE_ADDR         0x04
#define CMD_CLEAR_LOCATION 0x01
#define CMD_CLEAR_HEIGHT   0x02

// ------------------ NeoPixel Setup for Master ------------------
#define MASTER_NEOPIXEL_PIN 6
#define MASTER_NUM_LEDS     13  // one LED per master button
Adafruit_NeoPixel masterStrip(MASTER_NUM_LEDS, MASTER_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ------------------ Joystick Setup ------------------
#define NUM_BUTTONS 24   // Total: master (0–12) and slave (13–23)
Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID,
                   JOYSTICK_TYPE_GAMEPAD,
                   NUM_BUTTONS, 0,
                   false, false, false,
                   false, false, false,
                   false, false,
                   false, false, false);

// ------------------ Master Button Configuration ------------------
const int NUM_MASTER_BUTTONS = 13;
const int masterPins[NUM_MASTER_BUTTONS] = {
  13, 12, 11, 10, 9, 8, 7,  // indices 0–6
  A0, A1, A2, A3,          // indices 7–10
  4, 5                    // indices 11–12
};

// Define which master button indices (into masterPins) belong to which category:
// — Location group: buttons 0,1,2,5,7,8,9,11
const int numMasterLocation = 8;
int masterLocationGroup[numMasterLocation] = {0, 1, 2, 5, 7, 8, 9, 11};

// — Height group: buttons 3,4,6,10,12
const int numMasterHeight = 5;
int masterHeightGroup[numMasterHeight] = {3, 4, 6, 10, 12};

// Variables for master toggle state (persisting selection)
int toggleMasterLocation = -1;  // Holds the master button index for active location
int toggleMasterHeight   = -1;  // Holds the master button index for active height

// Array to detect rising edges on physical master buttons.
bool lastMasterPhysicalState[NUM_MASTER_BUTTONS] = {0};

// ------------------ Slave Mapping (for global Joystick report) ------------------
// In the slave code the location group is defined as slave physical buttons
// {0,1,2,5,6,7,9,10} and the height group as {3,4}. When reporting globally, we map:
const int slaveLocationMapping[8] = {13, 14, 15, 18, 19, 20, 22, 23};  // Slave location toggles
const int slaveHeightMapping[2]   = {16, 17};                           // Slave height toggles

// Global final selection (global joystick button number)
int finalLocationGlobal = -1;
int finalHeightGlobal   = -1;

// ------------------ Helper: Send Command to Slave ------------------
void sendSlaveCommand(uint8_t cmd) {
  Wire.beginTransmission(SLAVE_ADDR);
  Wire.write(cmd);
  Wire.endTransmission();
}

void setup() {
  // Initialize master pins as inputs with pull-ups.
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    pinMode(masterPins[i], INPUT_PULLUP);
  }
  
  // Initialize I2C as master.
  Wire.begin();
  
  // Initialize the Joystick.
  Joystick.begin();
  
  // Initialize the NeoPixel strip.
  masterStrip.begin();
  masterStrip.show();
  
#ifdef DEBUG_ENABLED
  Serial.begin(9600);
  while (!Serial) { }
  Serial.println("Master: Setup complete.");
#endif
}

void loop() {
  // -------- 1. Detect Toggle Events for Master Buttons --------
  // For each master button, detect a rising edge (transition from HIGH to LOW).
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    bool currentState = (digitalRead(masterPins[i]) == LOW);
    // Check if button i is in the Location group.
    for (int j = 0; j < numMasterLocation; j++) {
      if (masterLocationGroup[j] == i) {
        if (currentState && !lastMasterPhysicalState[i]) {
          // Rising edge detected.
          toggleMasterLocation = i;
#ifdef DEBUG_ENABLED
          Serial.print("Master Location Toggle: Button on pin ");
          Serial.print(masterPins[i]);
          Serial.print(" toggled active. (Index: ");
          Serial.print(i);
          Serial.println(")");
#endif
        }
      }
    }
    // Check if button i is in the Height group.
    for (int j = 0; j < numMasterHeight; j++) {
      if (masterHeightGroup[j] == i) {
        if (currentState && !lastMasterPhysicalState[i]) {
          // Rising edge detected.
          toggleMasterHeight = i;
#ifdef DEBUG_ENABLED
          Serial.print("Master Height Toggle: Button on pin ");
          Serial.print(masterPins[i]);
          Serial.print(" toggled active. (Index: ");
          Serial.print(i);
          Serial.println(")");
#endif
        }
      }
    }
    lastMasterPhysicalState[i] = currentState;
  }
  
  // -------- 2. Poll Slave for Its Current Toggle Selections --------
  // The slave sends two bytes:
  //   - First byte: slave location toggle (0–7) or 0xFF if none.
  //   - Second byte: slave height toggle (0–1) or 0xFF if none.
  uint8_t slaveLoc = 0xFF;
  uint8_t slaveHt  = 0xFF;
  Wire.requestFrom(SLAVE_ADDR, 2);
  unsigned long startTime = millis();
  while (Wire.available() < 2 && (millis() - startTime) < 50) {
    delay(5);
  }
  if (Wire.available() >= 2) {
    slaveLoc = Wire.read();
    slaveHt  = Wire.read();
#ifdef DEBUG_ENABLED
    Serial.print("Master: Received from Slave – Location: 0x");
    Serial.print(slaveLoc, HEX);
    Serial.print("  Height: 0x");
    Serial.println(slaveHt, HEX);
#endif
  }
  
  // -------- 3. Determine Final Global Selections (Toggle Behavior) --------
  // Priority: Master toggles take precedence.
  if (toggleMasterLocation != -1) {
    finalLocationGlobal = toggleMasterLocation;
    sendSlaveCommand(CMD_CLEAR_LOCATION);
  } else if (slaveLoc != 0xFF) {
    if (slaveLoc < 8)
      finalLocationGlobal = slaveLocationMapping[slaveLoc];
    else
      finalLocationGlobal = -1;
  } else {
    finalLocationGlobal = -1;
  }
  
  if (toggleMasterHeight != -1) {
    finalHeightGlobal = toggleMasterHeight;
    sendSlaveCommand(CMD_CLEAR_HEIGHT);
  } else if (slaveHt != 0xFF) {
    if (slaveHt < 2)
      finalHeightGlobal = slaveHeightMapping[slaveHt];
    else
      finalHeightGlobal = -1;
  } else {
    finalHeightGlobal = -1;
  }
  
  // -------- 4. Update the Joystick Report (Persistently Report Toggled Buttons) --------
  // First, clear all buttons in the location and height groups.
  for (int j = 0; j < numMasterLocation; j++) {
    Joystick.setButton(masterLocationGroup[j], false);
  }
  for (int j = 0; j < numMasterHeight; j++) {
    Joystick.setButton(masterHeightGroup[j], false);
  }
  for (int j = 0; j < 8; j++) {
    Joystick.setButton(slaveLocationMapping[j], false);
  }
  for (int j = 0; j < 2; j++) {
    Joystick.setButton(slaveHeightMapping[j], false);
  }
  
  // Then set the toggled buttons as pressed.
  if (finalLocationGlobal != -1)
    Joystick.setButton(finalLocationGlobal, true);
  if (finalHeightGlobal != -1)
    Joystick.setButton(finalHeightGlobal, true);
  
  Joystick.sendState();
  
  // -------- 5. Update Master LED Strip (for its own buttons) --------
  // Color definitions:
  //   - Selected: bright red (0xFF0000)
  //   - Default for location: light blue (0x3399FF)
  //   - Default for height: light yellow (0xFFFF99)
  for (int j = 0; j < numMasterLocation; j++) {
    int idx = masterLocationGroup[j];
    if (idx == toggleMasterLocation && finalLocationGlobal == toggleMasterLocation)
      masterStrip.setPixelColor(idx, 0xFF0000);
    else
      masterStrip.setPixelColor(idx, 0x3399FF);
  }
  for (int j = 0; j < numMasterHeight; j++) {
    int idx = masterHeightGroup[j];
    if (idx == toggleMasterHeight && finalHeightGlobal == toggleMasterHeight)
      masterStrip.setPixelColor(idx, 0xFF0000);
    else
      masterStrip.setPixelColor(idx, 0xFFFF99);
  }
  // Turn off any master LED not in either group.
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    bool inLoc = false, inHt = false;
    for (int j = 0; j < numMasterLocation; j++) {
      if (masterLocationGroup[j] == i) inLoc = true;
    }
    for (int j = 0; j < numMasterHeight; j++) {
      if (masterHeightGroup[j] == i) inHt = true;
    }
    if (!inLoc && !inHt)
      masterStrip.setPixelColor(i, 0);
  }
  masterStrip.show();
  
  // -------- 6. Print Final Selections to Serial --------
#ifdef DEBUG_ENABLED
  Serial.print("Final Global Selections: ");
  Serial.print("Location: ");
  if (toggleMasterLocation != -1) {
    Serial.print(toggleMasterLocation);
    Serial.print(" (Master)");
  } else if (slaveLoc != 0xFF) {
    Serial.print(slaveLoc);
    Serial.print(" (Slave)");
  } else {
    Serial.print("None");
  }
  
  Serial.print("  |  Height: ");
  if (toggleMasterHeight != -1) {
    Serial.print(toggleMasterHeight);
    Serial.print(" (Master)");
  } else if (slaveHt != 0xFF) {
    Serial.print(slaveHt);
    Serial.print(" (Slave)");
  } else {
    Serial.print("None");
  }
  Serial.println();
#endif
  
  delay(100);
}
