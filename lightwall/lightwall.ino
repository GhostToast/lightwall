#include <OctoSK6812.h>
#include <EEPROM.h>
#include "rainColumn.h"
#include "utilities.h"

const int ledsPerStrip = 128;
#define NUM_LEDS 896
#define BRIGHTNESS 50
DMAMEM int displayMemory[ledsPerStrip*8];
int drawingMemory[ledsPerStrip*8];

OctoSK6812 leds(ledsPerStrip, displayMemory, drawingMemory, SK6812_GRBW);

/**
   LED order in a given block.
*/
const uint8_t block[8][8] = {
  {0,  15,  16,  31,  32,  47,  48,  63},
  {1,  14,  17,  30,  33,  46,  49,  62},
  {2,  13,  18,  29,  34,  45,  50,  61},
  {3,  12,  19,  28,  35,  44,  51,  60},
  {4,  11,  20,  27,  36,  43,  52,  59},
  {5,  10,  21,  26,  37,  42,  53,  58},
  {6,   9,  22,  25,  38,  41,  54,  57},
  {7,   8,  23,  24,  39,  40,  55,  56}
};

/**
   The entire grid, including fake panels (denoated with -1).
*/
const int grid[4][7] = {
  { 0,  -1,   4,  -1,   8,  -1,  12},
  {-1,   2,  -1,   6,  -1,  10,  -1},
  { 1,  -1,   5,  -1,   9,  -1,  13},
  {-1,   3,  -1,   7,  -1,  11,  -1}
};

#define maxChars 32
rainColumn allRainColumns[56]; // Array to hold all rainColumn structs.
uint8_t maxWidth = 56;
uint8_t maxHeight = 32;
char inputString[maxChars];
char currentCharacter;
int inputIndex = 0;
int displayFlag = 0;
char currentColorChannel;
char displayPattern;
char matrixColorMode = 'g';
byte rVal;
byte bVal;
byte gVal;
byte wVal;
unsigned long eepromThrottleInterval = 5000;
unsigned long eepromLastUpdate = 0;
unsigned long currentTime = 0;

// Setup it up.
void setup() {
  Serial.begin(9600);
  leds.begin();
  leds.show();

  currentTime = millis();
  //readEEPROM();
}

// The main loop.
void loop() {
  //processUserInput();
  //displayUserSelectedMode();
  //rainbowCycle(768, 1, 1 );
  //holidayLights();
  makeItRain();
  //Serial.println("hello server");
}

void makeItRain() {
  // Keep things randomized.
  reseedRandomness();

  // Initialize matrix if this is first run.
  static boolean matrixInitialized = false;
  if ( ! matrixInitialized ) {
    for ( byte i = 0; i < maxWidth; i++) {
      allRainColumns[i].column = i;
    }
    matrixInitialized = true;
  }

  // Loop through all rain columns.
  currentTime = millis();
  for ( byte i = 0; i < maxWidth; i++) {
    rainOneColumn( allRainColumns[i] );
  }

  // Render display.
  leds.show();
}

// Occasionally reseed our randomness. (30 seconds).
void reseedRandomness() {
  static long randomnessLastSeed = 0;
  currentTime = millis();
  if ( currentTime - randomnessLastSeed >= 30000 ) {
    randomSeed(analogRead(0));
    randomnessLastSeed = currentTime;
  }
}

// Assign rain Column Properties. Mostly random, maintain column, lastUpdated, and lastCompleted.
void assignColumnProperties( rainColumn &rainColumn ) {
  rainColumn.head = 0;
  rainColumn.headLightness = random(16, 32);
  rainColumn.height = random(8, 24);
  rainColumn.isRunning = 0;
  rainColumn.canGoBlack = random(0, 2);
  rainColumn.dimAmount = random(2, 8);

  switch (matrixColorMode) {
    case 'r':
      rainColumn.color = makeColor(random(192, 256), random(0, 8), random(0, 8), 0, BRIGHTNESS);
      break;
    case 'g':
      rainColumn.color = makeColor(random(0, 24), random(192, 256), random(0, 32), 0, BRIGHTNESS);
      break;
    case 'b':
      rainColumn.color = makeColor(random(0, 8), random(24, 96), random(192, 256), 0, BRIGHTNESS);
      break;
    case 'w':
      rainColumn.color = makeColor(0, 0, 0, random(96, 256));
      break;
    case 'o': // orange.
      rainColumn.color = makeColor(random(96, 128), random(96, 128), random(0, 32), 0, BRIGHTNESS);
      break;
    case 'p': // purple.
      rainColumn.color = makeColor(random(192, 256), random(0, 24), random(192, 256), 0, BRIGHTNESS);
      break;
    default:
      rainColumn.color = makeColor(random(0, 8), random(24, 96), random(192, 256), 0, BRIGHTNESS);
      matrixColorMode = 'b';
  }

  rainColumn.interval = random(25, 115);
  rainColumn.sleepTime = random(1000, 3000);
}

void rainOneColumn( rainColumn &rainColumn ) {
  if ( rainColumn.isRunning == 1 ) {

    // Only animate if enough time has passed. This allows each column to have its own speed.
    if ( (currentTime - rainColumn.lastUpdated ) > rainColumn.interval ) {

      // Run the animation!
      updateRainColumnFrame( rainColumn );
    }

    // Draw further down than our canvas so things don't end abruptly.
    if ( rainColumn.head > maxHeight + (rainColumn.height * 2) ) {

      // Inform that the animation has terminated, and set lastCompleted time.
      rainColumn.isRunning = 0;
      rainColumn.lastCompleted = millis();

      // Build new properties so the next streamer in this column will not be identical to this one.
      assignColumnProperties( rainColumn );
    }
  }

  // Is it time to start a new sequence?
  if ( (currentTime - rainColumn.lastCompleted ) >= rainColumn.sleepTime ) {
    rainColumn.isRunning = 1;
  }
}

void updateRainColumnFrame(rainColumn &rainColumn) {

  // Lighten the front.
  leds.setPixel(remapXY(rainColumn.column, rainColumn.head), lightenColor(rainColumn.color, rainColumn.headLightness));
  leds.setPixel(remapXY(rainColumn.column, rainColumn.head - 1), lightenColor(rainColumn.color, round(rainColumn.headLightness * .66)));
  leds.setPixel(remapXY(rainColumn.column, rainColumn.head - 2), lightenColor(rainColumn.color, round(rainColumn.headLightness * .33)));

  // Standard color for behind the head.
  leds.setPixel(remapXY(rainColumn.column, rainColumn.head - 3), rainColumn.color);

  // Dim the tail.
  if ( rainColumn.head > rainColumn.height ) {
    for (byte tail = rainColumn.head - rainColumn.height; tail >= 0; tail--) {
      // Prevent wraparound.
      if ( tail == 255 ) {
        break;
      }
      uint16_t oldPixel = remapXY(rainColumn.column, tail);
      if ( -1 != oldPixel ) {
        uint32_t oldPixelColor = leds.getPixel(oldPixel);
        if ( 0 != oldPixelColor ) {
          leds.setPixel(oldPixel, dimColor(leds.getPixel(oldPixel), rainColumn.dimAmount, rainColumn.canGoBlack));
        }
      }
    }
  }

  // Increase head of the streamer, and set lastUpdated time.
  rainColumn.head++;
  rainColumn.lastUpdated = currentTime;
}

// Listen for user input and process it, populating variables.
void processUserInput() {
  while (Serial.available()) {
    displayFlag = 0;
    if (inputIndex < maxChars - 1) {
      currentCharacter = Serial.read();
      inputString[inputIndex] = currentCharacter;
      processCharacter();
      inputIndex++;
      inputString[inputIndex] = '\0'; // Always null terminate.
    }
  }
}

// Render user's choice.
void displayUserSelectedMode() {
  if (displayFlag == 0) {
    displayFlag = 1;
    inputIndex = 0;
    Serial.print(inputString); // Send debug back to phone.

    // We only need to fire the "oneColor" once per instruction set, not continuously.
    if (displayPattern == 's') {
      oneColor(makeColor(rVal, gVal, bVal, wVal));
    }
  }
  if (displayFlag == 1 && displayPattern == 'm') {
    makeItRain();
  }
  //writeEEPROM();
}

// Processes current character, setting mode and colors accordingly.
void processCharacter() {
  if (currentCharacter == 's' || currentCharacter == 'm') {
    displayPattern = currentCharacter;
  } else if (displayPattern == 's') {
    processSingleColorCharacter();
  } else if (displayPattern == 'm') {
    matrixColorMode = currentCharacter;
  }
}

// Processes current character, setting up color for single color display.
void processSingleColorCharacter() {
  if (currentCharacter == 'r') {
    currentColorChannel = currentCharacter;
    rVal = 0;
  } else if (currentCharacter == 'g') {
    currentColorChannel = currentCharacter;
    gVal = 0;
  } else if (currentCharacter == 'b') {
    currentColorChannel = currentCharacter;
    bVal = 0;
  } else if (currentCharacter == 'w') {
    currentColorChannel = currentCharacter;
    wVal = 0;
  } else if (currentColorChannel == 'r') {
    rVal *= 10;
    rVal += currentCharacter - '0';
  } else if (currentColorChannel == 'g') {
    gVal *= 10;
    gVal += currentCharacter - '0';
  } else if (currentColorChannel == 'b') {
    bVal *= 10;
    bVal += currentCharacter - '0';
  } else if (currentColorChannel == 'w') {
    wVal *= 10;
    wVal += currentCharacter - '0';
  }
}

// Occasionally write our color information to the EEPROM so it can survive a power cycle.
void writeEEPROM() {
  currentTime = millis();
  if ( (currentTime - eepromLastUpdate ) >= eepromThrottleInterval ) {
    EEPROM.update(0, displayPattern);
    if (displayPattern == 's') {
      EEPROM.update(1, rVal);
      EEPROM.update(2, gVal);
      EEPROM.update(3, bVal);
      EEPROM.update(4, wVal);
    } else if (displayPattern == 'm') {
      EEPROM.update(1, matrixColorMode);
    }

    eepromLastUpdate = currentTime;
  }
}

// Attempt to read EEPOM state when booting up, to use last known pattern and color.
void readEEPROM() {
  displayPattern = EEPROM.read(0);

  if (displayPattern == 's') {
    rVal = EEPROM.read(1);
    gVal = EEPROM.read(2);
    bVal = EEPROM.read(3);
    wVal = EEPROM.read(4);
  } else if (displayPattern == 'm') {
    matrixColorMode = EEPROM.read(1);
  }
}

/**
   Color Utilities.
*/
// Instructs all LED to display the same color, then renders.
void oneColor(uint32_t color) {
  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    leds.setPixel(i, color);
  }
  leds.show();
}

// Calculate diminishing version of a color.
uint32_t dimColor(uint32_t color, byte dimAmount, bool canGoBlack) {
  // Subtract R, G and B components until zero, except dominant color.
  uint8_t r = max( 0, red(color) - dimAmount );
  uint8_t g = max( 0, green(color) - dimAmount );
  uint8_t b = max( 0, blue(color) - dimAmount );
  uint8_t w = 0;

  if ( ! canGoBlack ) {
    // Red.
    if (matrixColorMode == 'r' && b <= dimAmount ) {
      b = b + dimAmount;
    }

    // Green.
    if (matrixColorMode == 'g' && g <= dimAmount ) {
      g = g + dimAmount;
    }

    // Blue.
    if (matrixColorMode == 'b' && b <= dimAmount ) {
      b = b + dimAmount;
    }

    // White.
    if (matrixColorMode == 'w' && b <= dimAmount ) {
      w = w + dimAmount;
    }

    // Orange.
    if (matrixColorMode == 'o') {
      if (r <= dimAmount) {
        r = r + dimAmount;
      }
      if (g <= dimAmount) {
        g = g + dimAmount;
      }
    }

    // Purple.
    if (matrixColorMode == 'p') {
      if (r <= dimAmount) {
        r = r + dimAmount;
      }
      if (b <= dimAmount) {
        b = b + dimAmount;
      }
    }
  }

  uint32_t dimColor = makeColor(r, g, b, w, 0);

  return dimColor;
}

// Brighten a color by adding white.
uint32_t lightenColor(uint32_t color, byte whiteAmount) {
  uint32_t lightenColor = makeColor( red(color), green(color), blue(color), whiteAmount);
  return lightenColor;
}

// Remap coordinates to an actual pixel number. Or a non-existent one.
uint16_t remapXY(uint8_t x, uint8_t y) {
  if ( x >= maxWidth || y >= maxHeight || x < 0 || y < 0 ) {
    return -1;
  }
  uint16_t pixelBlock = grid[y / 8][x / 8];
  if (-1 == pixelBlock) {
    return -1;
  }
  return innerRemapXY(x, y, pixelBlock);
}

// Remap coordinates to an actual pixel number within a panel.
uint16_t innerRemapXY(uint8_t x, uint8_t y, uint16_t pixelBlock) {
  return pixelBlock * 64 + block[y % 8][x % 8];
}


void rainbowCycle(uint32_t frames , uint32_t frameAdvance, uint32_t pixelAdvance ) {

  // Hue is a number between 0 and 3*256 than defines a mix of r->g->b where
  // hue of 0 = Full red
  // hue of 128 = 1/2 red and 1/2 green
  // hue of 256 = Full Green
  // hue of 384 = 1/2 green and 1/2 blue
  // ...
  unsigned int firstPixelHue = 0;     // Color for the first pixel in the string

  for (unsigned int j = 0; j < frames; j++) {
    unsigned int currentPixelHue = firstPixelHue;

    for (unsigned int i = 0; i < NUM_LEDS; i++) {

      if (currentPixelHue >= (3 * 256)) {              // Normalize back down incase we incremented and overflowed
        currentPixelHue -= (3 * 256);
      }

      unsigned char phase = currentPixelHue >> 8;
      unsigned char step = currentPixelHue & 0xff;

      switch (phase) {
        case 0:
          leds.setPixel( i, makeColor( ~step, step,  0 ) );
          break;

        case 1:
          leds.setPixel( i, makeColor( 0, ~step, step ) );
          break;

        case 2:
          leds.setPixel( i, makeColor( step, 0, ~step ) );
          break;
      }

      currentPixelHue += pixelAdvance;

    }
    leds.show();
    firstPixelHue += frameAdvance;
  }
}
