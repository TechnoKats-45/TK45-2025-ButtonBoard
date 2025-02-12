//#define DEBUG_ENABLED

#include <Wire.h>
#include <Joystick.h>
#include <Adafruit_NeoPixel.h>

// ------------------------------------------------------------------
// MASTER:
//  - Reads 13 local buttons + 11 Slave buttons (via I2C)
//  - Latches exactly 1 location + 1 height at a time
//  - Toggles Hopper (on/off)
//  - Lights a single NeoPixel strip to show selection
//  - Sends the corresponding joystick button states
//
// Now modified so that joystick states are only cleared if a *new*
// button press was detected. If not, the previously latched selection
// remains pressed without re-clearing.
// ------------------------------------------------------------------

// -------------------- I2C Setup --------------------
#define SLAVE_ADDR  0x04

// -------------------- NeoPixel Setup ----------------
#define MASTER_NEOPIXEL_PIN  6
#define MASTER_NUM_LEDS      48
Adafruit_NeoPixel masterStrip(MASTER_NUM_LEDS, MASTER_NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// -------------------- Joystick Setup ----------------
#define NUM_BUTTONS 24  // 13 Master + 11 Slave
Joystick_ Joystick(JOYSTICK_DEFAULT_REPORT_ID,
                   JOYSTICK_TYPE_GAMEPAD,
                   NUM_BUTTONS, 0,
                   false, false, false,
                   false, false, false,
                   false, false,
                   false, false, false);

// -------------------- MASTER Buttons ----------------
//
// Indices [0..12]:
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

ButtonType masterButtonType[NUM_MASTER_BUTTONS] = {
  HEIGHT,    //0
  HEIGHT,    //1
  HEIGHT,    //2
  HEIGHT,    //3
  HEIGHT,    //4
  LOCATION,  //5
  LOCATION,  //6
  LOCATION,  //7
  LOCATION,  //8
  LOCATION,  //9
  LOCATION,  //10
  LOCATION,  //11
  LOCATION   //12
};

// -------------------- SLAVE Buttons -----------------
//  Indices [0..10]:
//    0 => Hopper
//    1 => A2 (height)
//    2 => A1 (height)
//    3 => LeftCoral (location)
//    4 => Barge (location)
//    5 => H (location)
//    6 => I (location)
//    7 => J (location)
//    8 => K (location)
//    9 => L (location)
//    10=> A (location)

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

ButtonType slaveButtonType[NUM_SLAVE_BUTTONS] = {
  HOPPER,    //0
  HEIGHT,    //1
  HEIGHT,    //2
  LOCATION,  //3
  LOCATION,  //4
  LOCATION,  //5
  LOCATION,  //6
  LOCATION,  //7
  LOCATION,  //8
  LOCATION,  //9
  LOCATION   //10
};

// Joystick button assignment for each Slave button
// We'll place them after Master 13 => indices 13..23
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

// -------------------- Latch State --------------------
// Exactly 1 location + 1 height at a time. Hopper toggles independently.

int selectedLocationMaster = -1; // which Master button index for location
int selectedLocationSlave  = -1; // which Slave button index for location
int selectedHeightMaster   = -1; // which Master button index for height
int selectedHeightSlave    = -1; // which Slave button index for height
bool hopperSelected        = false; // toggled on/off

// For rising-edge detection
bool lastMasterPressed[NUM_MASTER_BUTTONS] = {false};
bool lastSlavePressed[NUM_SLAVE_BUTTONS]    = {false};

// -------------------- Color Constants --------------------
// "Deeper" colors if not selected, bright if selected
#define COLOR_LOC_NOT_PRESSED  0x993333 // deeper red
#define COLOR_LOC_SELECTED     0xFF0000 // bright red

#define COLOR_HT_NOT_PRESSED   0xFFFFFF // white-ish for unselected
#define COLOR_HT_SELECTED      0x0000FF // bright blue

#define COLOR_HOP_NOT_PRESSED  0x00FF00 // green
#define COLOR_HOP_SELECTED     0xFF0000 // red

// Set one pixel color based on category & selection
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
  // Master button pins
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    pinMode(masterPins[i], INPUT_PULLUP);
  }

  // I2C as master
  Wire.begin();

  // USB Joystick
  Joystick.begin();

  // NeoPixel
  masterStrip.begin();
  masterStrip.setBrightness(10); // global brightness (0..255)
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
  for (int i = 0; i < 8; i++) {
    slavePressed[i] = ((byte1 & (1 << i)) != 0);
  }
  for (int i = 8; i < NUM_SLAVE_BUTTONS; i++) {
    slavePressed[i] = ((byte2 & (1 << (i - 8))) != 0);
  }

  // ----------------------------------------------------------------
  // 3) Detect "new" presses => update latched selections
  // ----------------------------------------------------------------
  bool newPressDetected = false; // We'll track if *anything* was newly pressed

  // Master
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    bool now = masterPressed[i];
    bool was = lastMasterPressed[i];
    if (now && !was) {
      // Rising edge => new press
      newPressDetected = true;

      ButtonType btype = masterButtonType[i];
      if (btype == LOCATION) {
        selectedLocationMaster = i; // override old selection
        selectedLocationSlave  = -1;
      }
      else if (btype == HEIGHT) {
        selectedHeightMaster = i; // override old selection
        selectedHeightSlave  = -1;
      }
      // no hopper on master
    }
    lastMasterPressed[i] = now;
  }

  // Slave
  for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
    bool now = slavePressed[i];
    bool was = lastSlavePressed[i];
    if (now && !was) {
      newPressDetected = true;

      ButtonType btype = slaveButtonType[i];
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
    lastSlavePressed[i] = now;
  }

  // ----------------------------------------------------------------
  // 4) Update Joystick Buttons
  //
  // Only CLEAR the joystick states if a new press was detected!
  // Otherwise, keep the old states as-is, so the latched selection
  // remains pressed in the OS.
  // ----------------------------------------------------------------

  if (newPressDetected) {
    // 4a) Clear all joystick buttons
    for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
      Joystick.setButton(i, false);
    }
    for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
      Joystick.setButton(slaveJoystickIndex[i], false);
    }

    // 4b) Assert the new latched states
    if (selectedLocationMaster != -1) {
      Joystick.setButton(selectedLocationMaster, true);
    }
    if (selectedLocationSlave != -1) {
      Joystick.setButton(slaveJoystickIndex[selectedLocationSlave], true);
    }

    if (selectedHeightMaster != -1) {
      Joystick.setButton(selectedHeightMaster, true);
    }
    if (selectedHeightSlave != -1) {
      Joystick.setButton(slaveJoystickIndex[selectedHeightSlave], true);
    }

    // Hopper toggled => slave index 0 => joystick button #13
    Joystick.setButton(slaveJoystickIndex[0], hopperSelected);

    // Send updated state
    Joystick.sendState();
  }
  // If NO new press => we do nothing here, so last pressed states remain
  // latched in the Joystick library’s internal state.

  // ----------------------------------------------------------------
  // 5) Light the NeoPixels
  // ----------------------------------------------------------------
  // We still want to update LED visuals every loop
  // so the user can see what's selected.

  // 5a) Master side
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    ButtonType btype     = masterButtonType[i];
    bool isLocationMatch = (btype == LOCATION && i == selectedLocationMaster);
    bool isHeightMatch   = (btype == HEIGHT   && i == selectedHeightMaster);
    bool isSelected      = (isLocationMatch || isHeightMatch);

    setButtonColor(masterLedIndex[i], btype, isSelected);
  }

  // 5b) Slave side
  for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
    ButtonType btype     = slaveButtonType[i];
    bool isLocationMatch = (btype == LOCATION && i == selectedLocationSlave);
    bool isHeightMatch   = (btype == HEIGHT   && i == selectedHeightSlave);
    bool isHopper        = (btype == HOPPER);

    bool isSelected = (isLocationMatch || isHeightMatch || (isHopper && hopperSelected));

    setButtonColor(slaveLedIndex[i], btype, isSelected);
  }

  masterStrip.show();

#ifdef DEBUG_ENABLED
  if (newPressDetected) {
    Serial.print("NEW PRESS DETECTED --> Rewrote Joystick states.\n");
  }
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
