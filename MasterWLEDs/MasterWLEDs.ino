//#define DEBUG_ENABLED

#include <Wire.h>
#include <Joystick.h>
#include <Adafruit_NeoPixel.h>

// ------------------------------------------------------------------
// MASTER:
//  - Reads 13 local buttons + 11 Slave inputs over I2C
//  - Latches exactly 1 location + 1 height at a time
//  - HOPPER is an actual ON/OFF switch on the Slave (index 0), no toggle logic
//  - Lights a single NeoPixel strip to show selection
//  - Sends corresponding joystick states
//
// Location + Height are only re-sent if a *new press* is detected.
//
// Hopper is updated whenever its switch changes (OFF -> ON or ON -> OFF).
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
// Indices [0..12], each has: pin, LED index, type (LOCATION/HEIGHT).
//   0 => pin=10 => LED=11 => HEIGHT
//   1 => pin=12 => LED=9  => HEIGHT
//   2 => pin=13 => LED=7  => HEIGHT
//   3 => pin=5  => LED=5  => HEIGHT
//   4 => pin=A0 => LED=3  => HEIGHT
//   5 => pin=9  => LED=14 => LOCATION
//   6 => pin=A1 => LED=0  => LOCATION
//   7 => pin=7  => LED=32 => LOCATION
//   8 => pin=8  => LED=15 => LOCATION
//   9 => pin=11 => LED=17 => LOCATION
//   10=> pin=4  => LED=18 => LOCATION
//   11=> pin=A2 => LED=20 => LOCATION
//   12=> pin=A3 => LED=21 => LOCATION

const int NUM_MASTER_BUTTONS = 13;
const int masterPins[NUM_MASTER_BUTTONS] = {
  10, 12, 13, 5, A0, 9, A1, 7, 8, 11, 4, A2, A3
};
const int masterLedIndex[NUM_MASTER_BUTTONS] = {
  11, 9, 7, 5, 3, 14, 0, 32, 15, 17, 18, 20, 21
};

enum ButtonType {
  LOCATION,
  HEIGHT,
  HOPPER // used only on Slave index 0
};

ButtonType masterButtonType[NUM_MASTER_BUTTONS] = {
  HEIGHT, HEIGHT, HEIGHT, HEIGHT, HEIGHT,   // 0..4
  LOCATION, LOCATION, LOCATION, LOCATION,   // 5..8
  LOCATION, LOCATION, LOCATION, LOCATION    // 9..12
};

// -------------------- SLAVE Buttons -----------------
// Indices [0..10]:
//   0 => Hopper (switch!)
//   1 => A2 (height)
//   2 => A1 (height)
//   3 => LeftCoral (location)
//   4 => Barge (location)
//   5 => H (location)
//   6 => I (location)
//   7 => J (location)
//   8 => K (location)
//   9 => L (location)
//   10=> A (location)

const int NUM_SLAVE_BUTTONS = 11;
const int slaveLedIndex[NUM_SLAVE_BUTTONS] = {
  37, 41, 43, 47, 33, 30, 29, 27, 26, 24, 23
};
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

// Joystick button assignment for each Slave button (13..23).
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
// 1 location + 1 height latched. Hopper is direct on/off from switch.
int  selectedLocationMaster = -1;
int  selectedLocationSlave  = -1;
int  selectedHeightMaster   = -1;
int  selectedHeightSlave    = -1;

// For the hopper switch:
bool hopperSelected   = false;  // actual on/off state

// For rising-edge detection on location/height:
bool lastMasterPressed[NUM_MASTER_BUTTONS] = {false};
bool lastSlavePressed[NUM_SLAVE_BUTTONS]    = {false};

// Keep track of the hopper's last state to detect changes
bool lastHopperSwitch = false;  

// -------------------- Color Constants --------------------
#define COLOR_LOC_NOT_PRESSED  0x993333 // deeper red
#define COLOR_LOC_SELECTED     0xFF0000 // bright red

#define COLOR_HT_NOT_PRESSED   0xFFFFFF // white-ish
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
  masterStrip.setBrightness(10); // global brightness
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

  // The hopper switch is Slave index 0
  // "pressed" = true => switch is ON
  // "not pressed" = false => switch is OFF
  bool currentHopper = slavePressed[0];

  // ----------------------------------------------------------------
  // 3) Detect "new" presses => update latched selections (LOC/HT)
  //    Hopper is not toggled - it's a switch. We'll handle it below.
  // ----------------------------------------------------------------
  bool newPressDetected = false;

  // Master
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    bool now = masterPressed[i];
    bool was = lastMasterPressed[i];
    if (now && !was) {
      newPressDetected = true;

      ButtonType btype = masterButtonType[i];
      if (btype == LOCATION) {
        // override old location
        selectedLocationMaster = i;
        selectedLocationSlave  = -1;
      }
      else if (btype == HEIGHT) {
        // override old height
        selectedHeightMaster = i;
        selectedHeightSlave  = -1;
      }
    }
    lastMasterPressed[i] = now;
  }

  // Slave (for location/height)
  for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
    if (i == 0) continue; // skip hopper index in this loop
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
      // hopper is not toggled here
    }
    lastSlavePressed[i] = now;
  }

  // ----------------------------------------------------------------
  // 4) Detect Hopper switch changes
  //    If the hopper changed from last loop, we treat that like a
  //    "new press" so the joystick state is updated.
  // ----------------------------------------------------------------
  if (currentHopper != lastHopperSwitch) {
    newPressDetected = true;
  }
  hopperSelected   = currentHopper;
  lastHopperSwitch = currentHopper;

  // ----------------------------------------------------------------
  // 5) Update Joystick Buttons
  //
  // Only CLEAR the joystick states if a new press was detected
  // or if the hopper state changed. Otherwise keep old states latched.
  // ----------------------------------------------------------------
  if (newPressDetected) {
    // 5a) Clear all joystick buttons
    for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
      Joystick.setButton(i, false);
    }
    for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
      Joystick.setButton(slaveJoystickIndex[i], false);
    }

    // 5b) Assert latched states
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

    // Hopper switch => joystick button for Slave index 0
    Joystick.setButton(slaveJoystickIndex[0], hopperSelected);

    // Send updated state
    Joystick.sendState();
  }
  // If no new press & no hopper change => do nothing here

  // ----------------------------------------------------------------
  // 6) Light the NeoPixels
  // ----------------------------------------------------------------
  // Master side
  for (int i = 0; i < NUM_MASTER_BUTTONS; i++) {
    ButtonType btype = masterButtonType[i];
    bool isSelected  = false;
    if (btype == LOCATION && i == selectedLocationMaster) {
      isSelected = true;
    }
    else if (btype == HEIGHT && i == selectedHeightMaster) {
      isSelected = true;
    }
    setButtonColor(masterLedIndex[i], btype, isSelected);
  }

  // Slave side
  for (int i = 0; i < NUM_SLAVE_BUTTONS; i++) {
    ButtonType btype = slaveButtonType[i];
    bool isSelected  = false;
    if (btype == LOCATION && i == selectedLocationSlave) {
      isSelected = true;
    }
    else if (btype == HEIGHT && i == selectedHeightSlave) {
      isSelected = true;
    }
    else if (btype == HOPPER) {
      // Switch isSelected if on
      isSelected = hopperSelected;
    }
    setButtonColor(slaveLedIndex[i], btype, isSelected);
  }
  masterStrip.show();

#ifdef DEBUG_ENABLED
  if (newPressDetected) {
    Serial.print("NEW EVENT => Joystick state updated.\n");
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
