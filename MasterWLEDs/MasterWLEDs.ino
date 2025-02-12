//#define DEBUG_ENABLED

#include <Wire.h>
#include <Joystick.h>
#include <Adafruit_NeoPixel.h>

// ------------------------------------------------------------------
// MASTER: 
//  - Reads its own 13 buttons + 11 Slave buttons via I2C
//  - Latches exactly 1 location + 1 height at a time
//  - Toggles Hopper (on/off)
//  - Lights a single NeoPixel strip to show selection
//  - Sends the corresponding joystick button states
// ------------------------------------------------------------------

// -------------------- I2C Setup --------------------
#define SLAVE_ADDR  0x04

// -------------------- NeoPixel Setup ----------------
#define MASTER_NEOPIXEL_PIN  6
#define MASTER_NUM_LEDS      48
Adafruit_NeoPixel masterStrip(MASTER_NUM_LEDS, MASTER_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// -------------------- Joystick Setup ----------------
#define NUM_BUTTONS 24  // Enough for 13 master + 11 slave
Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID,
                   JOYSTICK_TYPE_GAMEPAD,
                   NUM_BUTTONS, 0,
                   false, false, false,
                   false, false, false,
                   false, false,
                   false, false, false);

// -------------------- MASTER Buttons ----------------
//
// We'll label them 0..12. We'll also classify each as either LOCATION or HEIGHT.
//
// Physical pins and LED indices (your mapping):
//   idx => pin => LED => type
//   0 => 10 => 11 => HEIGHT
//   1 => 12 => 9  => HEIGHT
//   2 => 13 => 7  => HEIGHT
//   3 => 5  => 5  => HEIGHT
//   4 => A0 => 3  => HEIGHT
//   5 => 9  => 14 => LOCATION
//   6 => A1 => 0  => LOCATION
//   7 => 7  => 32 => LOCATION
//   8 => 8  => 15 => LOCATION
//   9 => 11 => 17 => LOCATION
//   10=> 4  => 18 => LOCATION
//   11=> A2 => 20 => LOCATION
//   12=> A3 => 21 => LOCATION

const int NUM_MASTER_BUTTONS = 13;

const int masterPins[NUM_MASTER_BUTTONS] = {
  10, //0
  12, //1
  13, //2
  5,  //3
  A0, //4
  9,  //5
  A1, //6
  7,  //7
  8,  //8
  11, //9
  4,  //10
  A2, //11
  A3  //12
};

const int masterLedIndex[NUM_MASTER_BUTTONS] = {
  11, //0
  9,  //1
  7,  //2
  5,  //3
  3,  //4
  14, //5
  0,  //6
  32, //7
  15, //8
  17, //9
  18, //10
  20, //11
  21  //12
};

enum ButtonType {
  LOCATION,
  HEIGHT,
  HOPPER // not used on Master
};

// Classify the 13 Master buttons:
ButtonType masterButtonType[NUM_MASTER_BUTTONS] = {
  HEIGHT, //0
  HEIGHT, //1
  HEIGHT, //2
  HEIGHT, //3
  HEIGHT, //4
  LOCATION,//5
  LOCATION,//6
  LOCATION,//7
  LOCATION,//8
  LOCATION,//9
  LOCATION,//10
  LOCATION,//11
  LOCATION //12
};

// -------------------- SLAVE Buttons -----------------
// 11 total
// Index => meaning
//   0 => Hopper
//   1 => A2 (height)
//   2 => A1 (height)
//   3 => LeftCoral (location)
//   4 => Barge (location)
//   5 => H (location)
//   6 => I (location)
//   7 => J (location)
//   8 => K (location)
//   9 => L (location)
//   10 => A (location)

// LED indices for the single strip
const int NUM_SLAVE_BUTTONS = 11;
const int slaveLedIndex[NUM_SLAVE_BUTTONS] = {
  37, //0 => Hopper
  41, //1 => A2
  43, //2 => A1
  47, //3 => LeftCoral
  33, //4 => Barge
  30, //5 => H
  29, //6 => I
  27, //7 => J
  26, //8 => K
  24, //9 => L
  23  //10=> A
};

// Classify each slave button:
ButtonType slaveButtonType[NUM_SLAVE_BUTTONS] = {
  HOPPER,   //0
  HEIGHT,   //1
  HEIGHT,   //2
  LOCATION, //3
  LOCATION, //4
  LOCATION, //5
  LOCATION, //6
  LOCATION, //7
  LOCATION, //8
  LOCATION, //9
  LOCATION  //10
};

// Joystick button assignment for each Slave button
// We'll place them after the 13 Master buttons: 13..23
int slaveJoystickIndex[NUM_SLAVE_BUTTONS] = {
  13, //0 => Hopper
  14, //1 => A2
  15, //2 => A1
  16, //3 => LeftCoral
  17, //4 => Barge
  18, //5 => H
  19, //6 => I
  20, //7 => J
  21, //8 => K
  22, //9 => L
  23  //10=> A
};

// -------------------- Toggle/Latch State  --------------------
//
// We want exactly 1 location and 1 height latched at a time, 
// across both Master and Slave. If a new location is pressed, 
// it overrides the old selection. If a new height is pressed, 
// it overrides the old selection.
//
// The Hopper is toggled on/off independently (only on the Slave).
//
// We'll store -1 if none is selected yet, or the index that is selected.
//
// For location and height, we also store a "whichBoard" to know 
// if it's selected from Master or Slave.
int selectedLocationMaster = -1;   // which Master button index is location?
int selectedLocationSlave = -1;    // which Slave button index is location?
int selectedHeightMaster = -1;     // which Master button index is height?
int selectedHeightSlave = -1;      // which Slave button index is height?
bool hopperSelected = false;       // toggled on/off

// We'll track "last iteration" pressed states to detect new presses
bool lastMasterPressed[NUM_MASTER_BUTTONS] = {false};
bool lastSlavePressed[NUM_SLAVE_BUTTONS] = {false};

// -------------------- Color Constants --------------------
// The user requested deeper red/blue for unselected, bright red/blue for selected.
#define COLOR_LOC_NOT_PRESSED  0x993333 // deeper red
#define COLOR_LOC_SELECTED     0xFF0000 // bright red

#define COLOR_HT_NOT_PRESSED   0xFFFFFF // deeper blue
#define COLOR_HT_SELECTED      0x0000FF // bright blue

#define COLOR_HOP_NOT_PRESSED  0x00FF00 // green
#define COLOR_HOP_SELECTED     0xFF0000 // red

// Helper: set a single pixel color according to the button type + isSelected
void setButtonColor(int ledIndex, ButtonType btype, bool isSelected) {
  uint32_t color = 0;

  switch(btype) {
    case LOCATION:
      color = isSelected ? COLOR_LOC_SELECTED : COLOR_LOC_NOT_PRESSED;
      break;
    case HEIGHT:
      color = isSelected ? COLOR_HT_SELECTED : COLOR_HT_NOT_PRESSED;
      break;
    case HOPPER:
      color = isSelected ? COLOR_HOP_SELECTED : COLOR_HOP_NOT_PRESSED;
      break;
  }
  masterStrip.setPixelColor(ledIndex, color);
}

void setup() {
  // Set Master button pins
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    pinMode(masterPins[i], INPUT_PULLUP);
  }

  // I2C master
  Wire.begin();

  // USB Joystick
  Joystick.begin();

  // NeoPixel
  masterStrip.begin();
  masterStrip.setBrightness(10); // Adjust brightness 0..255
  masterStrip.show();

#ifdef DEBUG_ENABLED
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println("Master setup complete.");
#endif
}

void loop() {
  // ----------------------------------------------------------------
  // 1) Read current pressed states from Master
  // ----------------------------------------------------------------
  bool masterPressed[NUM_MASTER_BUTTONS];
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    masterPressed[i] = (digitalRead(masterPins[i]) == LOW);
  }

  // ----------------------------------------------------------------
  // 2) Read current pressed states from Slave (I2C)
  // ----------------------------------------------------------------
  uint8_t byte1 = 0;
  uint8_t byte2 = 0;

  Wire.requestFrom(SLAVE_ADDR, 2);
  unsigned long t0 = millis();
  while ((Wire.available() < 2) && ((millis() - t0) < 50)) {
    delay(1);
  }
  if (Wire.available() >= 2) {
    byte1 = Wire.read();
    byte2 = Wire.read();
  }

#ifdef DEBUG_ENABLED
  Serial.print("Master read Slave => 0x");
  Serial.print(byte1, HEX);
  Serial.print(" 0x");
  Serial.println(byte2, HEX);
#endif

  bool slavePressed[NUM_SLAVE_BUTTONS];
  // For the first 8 slave buttons (bits in byte1)
  for (int i = 0; i < 8; i++) {
    bool isPressed = ((byte1 & (1 << i)) != 0);
    slavePressed[i] = isPressed;
  }
  // For the last 3 slave buttons (bits in byte2)
  for (int i = 8; i < NUM_SLAVE_BUTTONS; i++) {
    bool isPressed = ((byte2 & (1 << (i - 8))) != 0);
    slavePressed[i] = isPressed;
  }

  // ----------------------------------------------------------------
  // 3) Detect new button presses => update latched selections
  //    We do Master first, then Slave; if both are pressed in same 
  //    loop for the same category, the Slave press overrides.
  // ----------------------------------------------------------------

  // 3a) MASTER
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    ButtonType btype = masterButtonType[i];
    bool now = masterPressed[i];
    bool was = lastMasterPressed[i];

    // A "new press" = now==true && was==false
    if (now && !was) {
      if (btype == LOCATION) {
        // Overwrite existing location selection
        selectedLocationMaster = i;
        selectedLocationSlave  = -1;
      } 
      else if (btype == HEIGHT) {
        // Overwrite existing height selection
        selectedHeightMaster = i;
        selectedHeightSlave  = -1;
      }
      // Master has no hopper
    }
  }

  // 3b) SLAVE
  for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
    ButtonType btype = slaveButtonType[i];
    bool now = slavePressed[i];
    bool was = lastSlavePressed[i];

    if (now && !was) {
      if (btype == LOCATION) {
        selectedLocationMaster = -1;
        selectedLocationSlave  = i;
      } 
      else if (btype == HEIGHT) {
        selectedHeightMaster = -1;
        selectedHeightSlave  = i;
      }
      else if (btype == HOPPER) {
        // Toggle on/off
        hopperSelected = !hopperSelected;
      }
    }
  }

  // Update "last pressed" states
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    lastMasterPressed[i] = masterPressed[i];
  }
  for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
    lastSlavePressed[i] = slavePressed[i];
  }

  // ----------------------------------------------------------------
  // 4) Update Joystick buttons
  //    EXACTLY one location, one height latched => those are pressed
  //    The hopper is toggled on/off => that is pressed or not
  // ----------------------------------------------------------------

  // Clear all Master + Slave joystick buttons
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    Joystick.setButton(i, false);
  }
  for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
    Joystick.setButton(slaveJoystickIndex[i], false);
  }

  // If we have a selected location on Master
  if (selectedLocationMaster != -1) {
    Joystick.setButton(selectedLocationMaster, true);
  }
  // If we have a selected location on Slave
  if (selectedLocationSlave != -1) {
    Joystick.setButton(slaveJoystickIndex[selectedLocationSlave], true);
  }

  // If we have a selected height on Master
  if (selectedHeightMaster != -1) {
    Joystick.setButton(selectedHeightMaster, true);
  }
  // If we have a selected height on Slave
  if (selectedHeightSlave != -1) {
    Joystick.setButton(slaveJoystickIndex[selectedHeightSlave], true);
  }

  // Hopper toggled => slave index 0 => joystick button = slaveJoystickIndex[0]
  Joystick.setButton(slaveJoystickIndex[0], hopperSelected);

  Joystick.sendState();

  // ----------------------------------------------------------------
  // 5) Light the NeoPixels
  //    - If location button is latched => bright red, else deeper red
  //    - If height button is latched => bright blue, else deeper blue
  //    - Hopper => green if off, red if on
  // ----------------------------------------------------------------

  // 5a) Master side
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    ButtonType btype = masterButtonType[i];
    bool isSelected = false;
    if (btype == LOCATION) {
      isSelected = (i == selectedLocationMaster);
    } else if (btype == HEIGHT) {
      isSelected = (i == selectedHeightMaster);
    }
    // Master has no hopper

    setButtonColor(masterLedIndex[i], btype, isSelected);
  }

  // 5b) Slave side
  for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
    ButtonType btype = slaveButtonType[i];
    bool isSelected = false;

    if (btype == LOCATION) {
      isSelected = (i == selectedLocationSlave);
    } else if (btype == HEIGHT) {
      isSelected = (i == selectedHeightSlave);
    } else if (btype == HOPPER) {
      // If it's the hopper button, it's "selected" if hopperSelected == true
      isSelected = hopperSelected;
    }

    setButtonColor(slaveLedIndex[i], btype, isSelected);
  }

  masterStrip.show();

#ifdef DEBUG_ENABLED
  Serial.print("LocM=");
  Serial.print(selectedLocationMaster);
  Serial.print(", LocS=");
  Serial.print(selectedLocationSlave);
  Serial.print(" | HtM=");
  Serial.print(selectedHeightMaster);
  Serial.print(", HtS=");
  Serial.print(selectedHeightSlave);
  Serial.print(" | Hopper=");
  Serial.println(hopperSelected ? "ON" : "OFF");
#endif

  delay(50);
}
