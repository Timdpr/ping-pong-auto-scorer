/*
 * Ping Pong Auto Scorer - Tim Russell
 * TODO: 
 *  Increase rally timeout
 *  Decrease height
 *  Put on github
 *  Put on other project sites (instructables circuits, hackaday)
 *  Handle (as much as possible) point scored -> ball bounces on other side of table
 *  Allow 21? Allow proper ruleset?
 */

#include "LedControl.h"
#include "AceButton.h"
using namespace ace_button;

#define PLAYER_1 0
#define PLAYER_2 1

// -- DOT MATRIX DISPLAY --
// SPI interface - Pin 10: DATA IN-pin | Pin 8: CLK-pin | Pin 9: LOAD(/CS)-pin
// There are 4 MAX7221 attached to the arduino
LedControl lc = LedControl(10, 8, 9, 4);

// Data for displaying numbers, e.g. NUMBERS[3] = byte data for '3'
const byte NUMBERS[12][8] = {
  {B00000000,B00111100,B01100110,B01101110,B01110110,B01100110,B01100110,B00111100}, // 0...
  {B00000000,B00011000,B00011000,B00111000,B00011000,B00011000,B00011000,B01111110},
  {B00000000,B00111100,B01100110,B00000110,B00001100,B00110000,B01100000,B01111110},
  {B00000000,B00111100,B01100110,B00000110,B00011100,B00000110,B01100110,B00111100},
  {B00000000,B00001100,B00011100,B00101100,B01001100,B01111110,B00001100,B00001100},
  {B00000000,B01111110,B01100000,B01111100,B00000110,B00000110,B01100110,B00111100},
  {B00000000,B00111100,B01100110,B01100000,B01111100,B01100110,B01100110,B00111100},
  {B00000000,B01111110,B01100110,B00001100,B00001100,B00011000,B00011000,B00011000},
  {B00000000,B00111100,B01100110,B01100110,B00111100,B01100110,B01100110,B00111100},
  {B00000000,B00111100,B01100110,B01100110,B00111110,B00000110,B01100110,B00111100},
  {B00000000,B01000110,B11001001,B01001001,B01001001,B01001001,B01001001,B11100110},
  {B00000000,B00100010,B01100110,B00100010,B00100010,B00100010,B00100010,B01110111} // ...11
};

// -- Piezoelectric sensors --
const int PIEZO_PIN_L = A0;
const int PIEZO_PIN_R = A1;

int piezoLeft = 0;
int piezoRight = 0;
const byte PIEZO_THRESHOLD = 5;

// -- Undo button --
const int UNDO_BUTTON_PIN = 7;
AceButton undoButton(UNDO_BUTTON_PIN);

// -- Hit counters & detection delay --
// History is a bit representation, e.g. 10101011 = last 2 hits were on player 2's side
byte hitCounter = 0;
byte hitHistory = 0;
const int DETECTION_DELAY = 300; // ** Change delay between detections here (ms) **
unsigned long lastDetection = 0;

// -- Point counters & timeout --
byte points[2] = {0, 0};
byte previousPoints[2] = {0, 0};
bool pointWinner = 0;
const int TIMEOUT_LENGTH = 1250; // ** Change timeout length here (ms) **
unsigned long timeoutStartTime = 0;
bool timeoutStart = false;
const int FLASH_DELAY = 250; // ** Change screen flash delay here (ms) **

void handleEvent(AceButton*, uint8_t, uint8_t);

void setup() {
  // Display setup:
  for (byte disp = 0; disp < lc.getDeviceCount(); disp++) {
    lc.shutdown(disp, false); // MAX72XX starts in power-saving mode, so do a wakeup call,
    lc.setIntensity(disp, 1); // set the brightness to a medium value,
    lc.clearDisplay(disp);    // and clear the display
  }
  updateDisplay();
  
  // Undo button setup: configure pin & enable the internal pull-up resistor
  pinMode(UNDO_BUTTON_PIN, INPUT_PULLUP);
  // AceButton library setup, so we can check for click and double click
  ButtonConfig* buttonConfig = undoButton.getButtonConfig();
  buttonConfig->setEventHandler(handleEvent);
  buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureSuppressAfterClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureSuppressAfterDoubleClick);
//  Serial.begin(9600);
}

void loop() {
  // Check whether undo button was pressed
  undoButton.check();
  
  // Read piezo ADC values into filter, and get output ('Analog-to-Digital Converter')
  piezoLeft = analogRead(PIEZO_PIN_L);
  piezoRight = analogRead(PIEZO_PIN_R);
//  Serial.print(piezoLeft);
//  Serial.print(',');
//  Serial.println(piezoRight);

  // Check for hits
  if (piezoLeft >= PIEZO_THRESHOLD && (millis() >= lastDetection + DETECTION_DELAY)) {
    registerHit(PLAYER_1);
  } else if (piezoRight >= PIEZO_THRESHOLD && (millis() >= lastDetection + DETECTION_DELAY)) {
    registerHit(PLAYER_2);
  }
  
  // Check for timeout since last hit
  if ((timeoutStart && (millis() >= timeoutStartTime + TIMEOUT_LENGTH))) {
    timeoutStart = false; // there won't be 0 hits because of timeoutStart
    if (hitCounter == 1) { // if just 1 hit, reset hits
      resetHits();
    } else if (hitCounter >= 2) {
      // Check for all hits since start of rally being on one side!!
      if (hitCounter <= 8 && checkForBotchedServe()) {
        resetHits();
      // If not, then **point win!**
      } else {
        memcpy(previousPoints, points, 2); // update previousPoints with current points
        pointWinner = !(hitHistory & 1);  // update with who won the point
        points[pointWinner]++;           // add point to opposite side of last hit
        resetHits();                    // reset hits
        flash(1, pointWinner);         // flash & update display
        checkForWin(pointWinner);     // and check for a win
      }
    }
  }

//  Serial.print(points[PLAYER_1]);
//  Serial.print(" - ");
//  Serial.print(points[PLAYER_2]);
//  Serial.print(" -- ");
//  Serial.println(hitCounter);

}

bool checkForBotchedServe() {
  for (byte i = 0; i < hitCounter-1; i++) {
    if (bitRead(hitHistory, i) != bitRead(hitHistory, i+1)) {
      return false;
    }
  }
  return true;
}

/**
 * For each 8x8 dot matrix display, find the score that should be displayed on it,
 * and write to it row by row!
 */
void updateDisplay() {
  for (byte disp = 0; disp < lc.getDeviceCount(); disp++) {
    for (byte row = 0; row < 8; row++) {
      // disp % 2 gives 0, 1, 0, 1, which is then flipped because the display is mounted wrong!
      lc.setRow(disp, row, NUMBERS[points[!(disp % 2)]][row]);
    }
  }
}

/** Register a table hit, reset/start counters, and add to history */
void registerHit(bool player) {
  hitCounter++;                // add to counter
  lastDetection = millis();    // update detection
  timeoutStartTime = millis(); // and timeout start times
  timeoutStart = true;         // start timeout, and...
  // Left shift 1 and bitwise or w/the player (0 or 1), i.e. 'push' a bit into hitHistory
  hitHistory = (hitHistory << 1) | player; // update hit history
}

void resetHits() {
  hitHistory = 0;
  hitCounter = 0;
}

void resetAll() {
  resetHits();
  points[PLAYER_1] = 0;
  points[PLAYER_2] = 0;
}

void checkForWin(bool player) {
  if (points[player] == 11) {
    flash(4, player);
    resetAll();
    updateDisplay();
  }
}

/** Flash the given player's points n times, with FLASH_DELAY ms between each state */
void flash(byte n, bool player) {
  for (byte i = 0; i < n; i++) {
    lc.clearDisplay(!player);
    lc.clearDisplay((!player) + 2);
    delay(FLASH_DELAY);
    updateDisplay();
    delay(FLASH_DELAY);
  }
}

/** The event handler for the undo button */
void handleEvent(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  switch (eventType) {
    case AceButton::kEventClicked:
    case AceButton::kEventReleased:
      // If button was single-clicked, undo last point
      memcpy(points, previousPoints, 2);
      updateDisplay();
      break;
    case AceButton::kEventDoubleClicked:
      // If button was double-clicked, undo last point & give it to opposite player
      memcpy(points, previousPoints, 2);
      points[pointWinner]++;
      pointWinner = !pointWinner;
      updateDisplay();
      checkForWin(pointWinner);
      checkForWin(PLAYER_2);
      break;
  }
}

float adcToVoltage(int adc) {
  return adc / 1023.0 * 5.0;
}
