#include <OctoSK6812.h>
#include "rainColumn.h"
#include "utilities.h"

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

rainColumn allRainColumns[56]; // Array to hold all rainColumn structs.
uint8_t maxWidth = 56;
uint8_t maxHeight = 32;
byte fireStart = 0;
byte fireSpeed = 100;
char matrixColorMode = 'g';
byte matrixPaused = 0;
uint8_t matrixColors[4][2];
unsigned long currentTime = 0;
unsigned long globalLastTime = 0;
byte fadeSteps = 32;
byte fadeIndex = 0;
byte fadeInterval = 10;
const byte buffSize = 40;
char inputBuffer[buffSize];
const char startMarker = '<';
const char endMarker = '>';
byte bytesRecvd = 0;
boolean readInProgress = false;
boolean newDataFromServer = false;
char messageFromServer[buffSize] = {0};
byte userMode = 0;
byte gradientProcessed = 1;
byte rVal = 0;
byte gVal = 0;
byte bVal = 0;
byte wVal = 0;
byte rVal2 = 0;
byte gVal2 = 0;
byte bVal2 = 0;
byte wVal2 = 0;

const int ledsPerStrip = 128;
#define NUM_LEDS 896
#define BRIGHTNESS 50
DMAMEM int displayMemory[ledsPerStrip * 8];
int drawingMemory[ledsPerStrip * 8];

OctoSK6812 leds(ledsPerStrip, displayMemory, drawingMemory, SK6812_GRBW);

void setup() {
  Serial.begin(9600);
  leds.begin();
  leds.show();
}

void loop() {
  currentTime = millis();
  processUserInput();
  respondToServer();
  displayUserSelectedMode();
}

void processUserInput() {
  if ( Serial.available() > 0) {
    char x = Serial.read();

    if (x == endMarker) {
      readInProgress = false;
      newDataFromServer = true;
      inputBuffer[bytesRecvd] = 0;
      parseData();
    }

    if (readInProgress) {
      inputBuffer[bytesRecvd] = x;
      bytesRecvd ++;
      if (bytesRecvd == buffSize) {
        bytesRecvd = buffSize - 1;
      }
    }

    if (x == startMarker) {
      bytesRecvd = 0;
      readInProgress = true;
    }
  }
}

void parseData() {
  // Used as index by strtok().
  char * strtokIndex;

  // Get first part, should inform what mode this will be.
  strtokIndex = strtok(inputBuffer, ",");
  if ( strcmp(strtokIndex, "state") == 0) {
    // Do not change user mode, as this request is attempting to receive current state.
    processState();
  } else if (strcmp(strtokIndex, "rgbw") == 0) {
    userMode = 1;
    processRGBW(strtokIndex);
  } else if (strcmp(strtokIndex, "matrix") == 0) {
    userMode = 2;
    processMatrix(strtokIndex);
  } else if (strcmp(strtokIndex, "grade") == 0) {
    userMode = 3;
    processGrade(strtokIndex);
  } else if (strcmp(strtokIndex, "pause") == 0) {
    userMode = 4;
    processMatrixPause(strtokIndex);
  } else if (strcmp(strtokIndex, "fire") == 0) {
    userMode = 5;
    processFire(strtokIndex);
  }
}

void processState() {
  if (1 == userMode){
    Serial.print("<rgbw,");
    Serial.print(rVal);
    Serial.print(",");
    Serial.print(gVal);
    Serial.print(",");
    Serial.print(bVal);
    Serial.print(",");
    Serial.print(wVal);
    Serial.println(">");
  } else if (2 == userMode) {
    Serial.print("<matrix,");
    Serial.print(matrixColorMode);
    Serial.print(">");
  } else if (3 == userMode) {
    Serial.print("<grade,");
    Serial.print(rVal);
    Serial.print(",");
    Serial.print(gVal);
    Serial.print(",");
    Serial.print(bVal);
    Serial.print(",");
    Serial.print(wVal);
    Serial.println(">");
  } else if (4 == userMode) {
    Serial.print("<pause,");
    Serial.print(matrixPaused);
    Serial.println(">");
  } else if (5 == userMode) {
    Serial.print("<fire,");
    Serial.print(fireStart);
    Serial.println(">");
  } else {
    //Serial.print("<fail>");
    Serial.println(1);
  }
}

void processRGBW(char * strtokIndex) {
  rVal2 = rVal;
  gVal2 = gVal;
  bVal2 = bVal;
  wVal2 = wVal;
  fadeIndex = 0;

  // Get the next part, which should be Red value.
  strtokIndex = strtok(NULL, ",");
  rVal = atoi(strtokIndex);

  // Get next part, which should be Green value.
  strtokIndex = strtok(NULL, ",");
  gVal = atoi(strtokIndex);

  // Get next part, which should be Blue value.
  strtokIndex = strtok(NULL, ",");
  bVal = atoi(strtokIndex);

  // Get next part, which should be White value.
  strtokIndex = strtok(NULL, ",");
  wVal = atoi(strtokIndex);
}

void processGrade(char * strtokIndex) {

  // Store old values.
  rVal2 = rVal;
  gVal2 = gVal;
  bVal2 = bVal;
  wVal2 = wVal;

  // Get the next part, which should be Red value.
  strtokIndex = strtok(NULL, ",");
  rVal = atoi(strtokIndex);
  
  // Get next part, which should be Green value.
  strtokIndex = strtok(NULL, ",");
  gVal = atoi(strtokIndex);
  
  // Get next part, which should be Blue value.
  strtokIndex = strtok(NULL, ",");
  bVal = atoi(strtokIndex);

  // Get next part, which should be White value.
  strtokIndex = strtok(NULL, ",");
  wVal = atoi(strtokIndex);

  gradientProcessed = 0;
}

void processFire(char * strtokIndex) {
  // Get fire boolean status.
  strtokIndex = strtok(NULL, ",");
  fireStart = atoi(strtokIndex);
}

void processMatrixPause(char * strtokIndex) {
  // Get paused status (boolean).
  strtokIndex = strtok(NULL, ",");
  matrixPaused = atoi(strtokIndex);
}

void processMatrix(char * strtokIndex) {
  matrixPaused = 0;
  // Fill up matrix colors.
  for (byte i = 0; i < 4; i++) {
    for (byte z = 0; z <2; z++) {
      strtokIndex = strtok(NULL, ",");
      matrixColors[i][z] = atoi(strtokIndex);
    }
  }
}

void respondToServer() {
  if (newDataFromServer) {
    newDataFromServer = false;
    Serial.println(userMode);
  }
}

void makeItRain() {
  if ( matrixPaused ) {
    return;
  }

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

// Assign rain Column Properties. Mostly random, maintain column, lastUpdated, and lastCompleted.
void assignColumnProperties( rainColumn &rainColumn ) {
  rainColumn.head = 0;
  rainColumn.headLightness = random(16, 32);
  rainColumn.height = random(8, 24);
  rainColumn.isRunning = 0;
  rainColumn.canGoBlack = random(0, 2);
  rainColumn.dimAmount = random(2, 8);

  rainColumn.color = makeColor(
    random(matrixColors[0][0], matrixColors[0][1]),
    random(matrixColors[1][0], matrixColors[1][1]),
    random(matrixColors[2][0], matrixColors[2][1]),
    random(matrixColors[3][0], matrixColors[3][1]),
    BRIGHTNESS
  );

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
      rainColumn.lastCompleted = currentTime;

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



// Calculate diminishing version of a color.
uint32_t dimColor(uint32_t color, byte dimAmount, bool canGoBlack) {
  uint8_t r1 = red(color);
  uint8_t g1 = green(color);
  uint8_t b1 = blue(color);
  uint8_t w1 = white(color);
  
  // Subtract R, G and B components until zero, except dominant color.
  uint8_t r2 = max( 0, r1 - dimAmount );
  uint8_t g2 = max( 0, g1 - dimAmount );
  uint8_t b2 = max( 0, b1 - dimAmount );
  uint8_t w2 = max( 0, w1 - dimAmount );

  uint32_t dimColor = makeColor(r2, g2, b2, w2);

  if ( ! canGoBlack && dimColor <= 0) {
    if (r2 == 0) {
      r2 = r1;
    }
    if (g2 == 0) {
      g2 = g1;
    }
    if (b2 == 0) {
      b2 = b1;
    }
    if (w2 == 0) {
      w2 = w1;
    }
    dimColor = makeColor(r2, g2, b2, w2);
  }

  return dimColor;
}

// Brighten a color by adding white.
uint32_t lightenColor(uint32_t color, byte whiteAmount) {
  uint32_t lightenColor = makeColor( red(color), green(color), blue(color), whiteAmount);
  return lightenColor;
}

// Instructs all LED to display the same color, then renders.
void oneColor(uint32_t color, uint32_t fadeColor = -1) {
  if (fadeColor >= 0 && color != fadeColor && fadeIndex <= fadeSteps) {
      uint8_t r = ((red(fadeColor) * (fadeSteps - fadeIndex)) + (red(color) * fadeIndex)) / fadeSteps;
      uint8_t g = ((green(fadeColor) * (fadeSteps - fadeIndex)) + (green(color) * fadeIndex)) / fadeSteps;
      uint8_t b = ((blue(fadeColor) * (fadeSteps - fadeIndex)) + (blue(color) * fadeIndex)) / fadeSteps;
      uint8_t w = ((white(fadeColor) * (fadeSteps - fadeIndex)) + (white(color) * fadeIndex)) / fadeSteps;
      color = makeColor( r, g, b, w );
      fadeIndex++;
  }
  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    leds.setPixel(i, color);
  }
  leds.show();
}

// Create a gradient fade between two colors.
void gradient() {
  if ( gradientProcessed == 0 ) {
    for (uint8_t y = 0; y < maxHeight; y++) {
      uint8_t r = ((rVal * (maxHeight - y)) + (rVal2 * y)) / maxHeight; // 255 * 56 + 0
      uint8_t g = ((gVal * (maxHeight - y)) + (gVal2 * y)) / maxHeight;
      uint8_t b = ((bVal * (maxHeight - y)) + (bVal2 * y)) / maxHeight;
      uint8_t w = ((wVal * (maxHeight - y)) + (wVal2 * y)) / maxHeight;
      uint32_t color = makeColor( r, g, b, w );
  
      for (uint8_t x = 0; x < maxWidth; x++) {
        leds.setPixel(remapXY(x, y), color);
      }
    } 
    gradientProcessed = 1;
  }

  leds.show();
}

void fireStarter() {
  if ( ! fireStart ) {
    return;
  }

  if ( (currentTime - globalLastTime) < fireSpeed ) {
    return;
  }

  // Color bottom row.
  for (uint8_t x = 0; x < maxWidth; x++) {
    for (uint8_t y = 0; y < maxHeight; y++) {
     leds.setPixel(remapXY(x, y), hsvRgb(random(0,32), 255, 32));
    }
  }
  leds.show();

  globalLastTime = currentTime;
}

void displayUserSelectedMode() {
  switch (userMode) {
    case 0: // None, dim white.
      oneColor(0x00000010);
      break;

    case 1: // RGBW.
      if (currentTime - globalLastTime >= fadeInterval) {
        globalLastTime = currentTime;
        oneColor( makeColor( rVal, gVal, bVal, wVal ), makeColor( rVal2, gVal2, bVal2, wVal2 ));
      }
      break;

    case 2: // Matrix.
      makeItRain();
      break;

    case 3: // Gradient.
      gradient();
      break;

    case 4: // Pause matrix.
      makeItRain();
      break;

    case 5: // Fire starter.
      fireStarter();
      break;

    default:
      oneColor(0x00000010);
      break;
  }
}
