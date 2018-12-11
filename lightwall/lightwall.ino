#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include "rainColumn.h"
#include "ornament.h"
#include "utilities.h"

#ifdef __AVR__
  #include <avr/power.h>
#endif

#define PIN 10
#define NUM_LEDS 896
#define BRIGHTNESS 64
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRBW + NEO_KHZ800);

/**
 * LED order in a given block.
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
 * The entire grid, including fake panels (denoated with -1).
 */
const uint8_t grid[4][7] = {
  { 0,  -1,   4,  -1,   8,  -1,  12},
  {-1,   2,  -1,   6,  -1,  10,  -1},
  { 1,  -1,   5,  -1,   9,  -1,  13},
  {-1,   3,  -1,   7,  -1,  11,  -1}
};

#define maxChars 32
rainColumn allRainColumns[56]; // Array to hold all rainColumn structs.
ornament allOrnaments[14]; // Array to hold all ornament structs.
uint8_t maxWidth = 56;
uint8_t maxHeight = 32;
char inputString[maxChars];
char currentCharacter;
int index = 0;
int displayFlag = 0;
char currentColorChannel;
char displayPattern;
char matrixColorMode;
byte rVal;
byte bVal;
byte gVal;
byte wVal;
unsigned long eepromThrottleInterval = 5000;
unsigned long eepromLastUpdate = 0;
unsigned long currentTime = 0;

// Setup it up.
void setup() {
  Serial.begin(115200);
  strip.setBrightness(BRIGHTNESS);
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  currentTime = millis();
  readEEPROM();
}

// The main loop.
void loop() {
  //processUserInput();
  //displayUserSelectedMode();
  rainbowCycle(768, 1, 1 );
  //holidayLights();
}

void holidayLights() {
  const uint32_t arrayColors[4] = {
    strip.Color(255, 0, 0, 0),
    strip.Color(0, 255, 0, 0),
    strip.Color(0, 0, 255, 0),
    strip.Color(255, 128, 0, 0)
  };

  // Keep things randomized.
  reseedRandomness();
  
  // Initialize ornaments if this is first run.
  static boolean ornamentsInitialized = false;
  if ( ! ornamentsInitialized ) {
    for( byte i=0; i<14; i++) {
      allOrnaments[i].panel = i;
      allOrnaments[i].interval = random( 25, 300 );
    }
    ornamentsInitialized = true;
  }

  // Loop through all rain columns.
  for( byte i=0; i<14; i++) {
    renderOneOrnament( allOrnaments[i] );
  }

  // Render display.
  strip.show();

//  if(displayFlag == 0) {
//    displayFlag = 1;
//    index = 0;

//    for(uint16_t i=0; i<NUM_LEDS; i++) {
//      if ( i % 7 ) {
//        strip.setPixelColor(i, 0);
//      } else {
//        strip.setPixelColor(i, arrayColors[random(0, 3)]);
//      }
//    }
//
//    for( byte a = 0; a<8; a++ ) {
//      for( byte b = 0; b<8; b++ ) {
//
//        uint16_t pixelBlock = grid[a][b];
//        byte color = random(0,4);
//
//        for( byte z = 0; z<64; z++ ) {
//          strip.setPixelColor( pixelBlock * 64 + z, arrayColors[color]);
//        }
//      }
//    }

//      if ( x >= maxWidth || y >= maxHeight || x < 0 || y < 0 ) {
//        return -1;
//      }
//      uint16_t pixelBlock = grid[y/8][x/8];
//      if (-1 == pixelBlock) {
//        return -1;
//      }
//      pixelBlock = pixelBlock * 64 + block[y%8][x%8];
//      return pixelBlock;

//    strip.show();
//  }
}

void renderOneOrnament( ornament &ornament ) {
  currentTime = millis();

  // Only animate if enough time has passed. This allows each column to have its own speed.
  if( (currentTime - ornament.lastUpdated ) >= ornament.interval ) {

    // Run the animation!
    updateOrnamentFrame( ornament );
  }
}

void processPixel( uint8_t x, uint8_t y, ornament &ornament, uint32_t color ) {
  uint16_t currentPixel = innerRemapXY(x, y, ornament.panel);
  uint32_t currentPixelColor = strip.getPixelColor( currentPixel );
  if ( 0 != currentPixelColor ) {
    color = dimColor( currentPixelColor, random(0, 17), 1 );
  }
  strip.setPixelColor( currentPixel, color );
}

void updateOrnamentFrame(ornament &ornament) {
  uint32_t core = strip.Color(255, 10, 0, 0);
  uint32_t dim1 = strip.Color(64, 6, 0, 0);
  uint32_t dim2 = strip.Color(32, 0, 0, 0);
  uint32_t fade = strip.Color(16, 0, 0, 0);

// TODO need init color
  // core.
  processPixel(3, 3, ornament, core );
  processPixel(4, 3, ornament, core );
  processPixel(3, 4, ornament, core );
  processPixel(4, 4, ornament, core );

  // dimmer.
  processPixel(3, 2, ornament, dim1 );
  processPixel(4, 2, ornament, dim1 );
  processPixel(2, 3, ornament, dim1 );
  processPixel(5, 3, ornament, dim1 );
  processPixel(2, 4, ornament, dim1 );
  processPixel(5, 4, ornament, dim1 );
  processPixel(3, 5, ornament, dim1 );
  processPixel(4, 5, ornament, dim1 );

  // dimmer still.
  processPixel(3, 1, ornament, dim2 );
  processPixel(4, 1, ornament, dim2 );
  processPixel(2, 2, ornament, dim2 );
  processPixel(5, 2, ornament, dim2 );
  processPixel(1, 3, ornament, dim2 );
  processPixel(6, 3, ornament, dim2 );
  processPixel(1, 4, ornament, dim2 );
  processPixel(6, 4, ornament, dim2 );
  processPixel(2, 5, ornament, dim2 );
  processPixel(5, 5, ornament, dim2 );
  processPixel(3, 6, ornament, dim2 );
  processPixel(4, 6, ornament, dim2 );

  // dimmest.
  processPixel(2, 1, ornament, fade );
  processPixel(5, 1, ornament, fade );
  processPixel(1, 2, ornament, fade );
  processPixel(6, 2, ornament, fade );
  processPixel(1, 5, ornament, fade );
  processPixel(6, 5, ornament, fade );
  processPixel(2, 6, ornament, fade );
  processPixel(5, 6, ornament, fade );

  ornament.lastUpdated = currentTime;
}

void makeItRain() {
  // Keep things randomized.
  reseedRandomness();
  
  // Initialize matrix if this is first run.
  static boolean matrixInitialized = false;
  if ( ! matrixInitialized ) {
    for( byte i=0; i<maxWidth; i++) {
      allRainColumns[i].column = i;
    }
    matrixInitialized = true;
  }

  // Loop through all rain columns.
  for( byte i=0; i<maxWidth; i++) {
    rainOneColumn( allRainColumns[i] );
  }

  // Render display.
  strip.show();
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
  rainColumn.headBrightness = random(24, 96);
  rainColumn.height = random(8,24);
  rainColumn.isRunning = 0;
  rainColumn.canGoBlack = random(0,2);
  rainColumn.dimAmount = random(8,32);

  switch (matrixColorMode) {
    case 'r':
      rainColumn.color = strip.Color(random(192, 256), random(0, 8), random(0, 8), 0);
      break;
    case 'g':
      rainColumn.color = strip.Color(random(0, 24), random(192, 256), random(0, 32), 0);
      break;
    case 'b':
      rainColumn.color = strip.Color(random(0, 8), random(24, 96), random(192, 256), 0);
      break;
    case 'w':
      rainColumn.color = strip.Color(0, 0, 0, random(96, 256));
      break;
    case 'o': // orange.
      rainColumn.color = strip.Color(random(96, 128), random(96, 128), random(0, 32), 0);
      break;
    case 'p': // purple.
      rainColumn.color = strip.Color(random(192, 256), random(0, 24), random(192, 256), 0);
      break;
    default:
      rainColumn.color = strip.Color(random(192, 256), random(0, 8), random(0, 8), 0);
  }
  
  rainColumn.interval = random(25,115);
  rainColumn.sleepTime = random(1000, 3000);
}

void rainOneColumn( rainColumn &rainColumn ) {
  currentTime = millis();

  if( rainColumn.isRunning == 1 ) {

    // Only animate if enough time has passed. This allows each column to have its own speed.
    if( (currentTime - rainColumn.lastUpdated ) >= rainColumn.interval ) {

      // Run the animation!
      updateRainColumnFrame( rainColumn );
    }

    // Draw further down than our canvas so things don't end abruptly.
    if( rainColumn.head > maxHeight+(rainColumn.height*2) ) {

      // Inform that the animation has terminated, and set lastCompleted time.
      rainColumn.isRunning = 0;
      rainColumn.lastCompleted = millis();
  
      // Build new properties so the next streamer in this column will not be identical to this one.
      assignColumnProperties( rainColumn );  
    }
  }

    // Is it time to start a new sequence?
  if( (currentTime - rainColumn.lastCompleted ) >= rainColumn.sleepTime ) {
    rainColumn.isRunning = 1;
  }
}

void updateRainColumnFrame(rainColumn &rainColumn) {

    // Brighten the front.
    strip.setPixelColor(remapXY(rainColumn.column,rainColumn.head), brightenColor(rainColumn.color, rainColumn.headBrightness));
    strip.setPixelColor(remapXY(rainColumn.column,rainColumn.head-1), brightenColor(rainColumn.color, round(rainColumn.headBrightness*.66)));
    strip.setPixelColor(remapXY(rainColumn.column,rainColumn.head-2), brightenColor(rainColumn.color, round(rainColumn.headBrightness*.33)));

    // Standard color for behind the head.
    strip.setPixelColor(remapXY(rainColumn.column,rainColumn.head-3), rainColumn.color);

    // Dim the tail.
    if ( rainColumn.head > rainColumn.height ) {
      for(byte tail=rainColumn.head-rainColumn.height; tail>=0; tail--) {
        // Prevent wraparound.
        if ( tail == 255 ) {
          break;
        }
        uint16_t oldPixel = remapXY(rainColumn.column,tail);
        if ( -1 != oldPixel ) {
          uint32_t oldPixelColor = strip.getPixelColor(oldPixel);
          if ( 0 != oldPixelColor ) {
            strip.setPixelColor(oldPixel, dimColor(strip.getPixelColor(oldPixel), rainColumn.dimAmount, rainColumn.canGoBlack));              
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
  while(Serial.available()) {
    displayFlag = 0;
    if(index < maxChars-1){
      currentCharacter = Serial.read();
      inputString[index] = currentCharacter;
      processCharacter();
      index++;
      inputString[index] = '\0'; // Always null terminate.
    }
  }
}

// Render user's choice.
void displayUserSelectedMode() {
  if(displayFlag == 0) {
    displayFlag = 1;
    index = 0;
    Serial.print(inputString); // Send debug back to phone.

    // We only need to fire the "oneColor" once per instruction set, not continuously.
    if(displayPattern == 's') {
      oneColor(strip.Color(rVal, gVal, bVal, wVal));
    }
  }
  if(displayFlag == 1 && displayPattern == 'm') {
    makeItRain();
  }
  writeEEPROM();
}

// Processes current character, setting mode and colors accordingly.
void processCharacter() {
  if(currentCharacter == 's' || currentCharacter == 'm') {
    displayPattern = currentCharacter;
  } else if(displayPattern == 's') {
    processSingleColorCharacter();
  } else if(displayPattern == 'm') {
    matrixColorMode = currentCharacter;
  }
}

// Processes current character, setting up color for single color display.
void processSingleColorCharacter() {
  if(currentCharacter == 'r') {
    currentColorChannel = currentCharacter;
    rVal = 0;
  } else if(currentCharacter == 'g') {
    currentColorChannel = currentCharacter;
    gVal = 0;
  } else if(currentCharacter == 'b') {
    currentColorChannel = currentCharacter;
    bVal = 0;
  } else if(currentCharacter == 'w') {
    currentColorChannel = currentCharacter;
    wVal = 0;
  } else if(currentColorChannel == 'r'){
    rVal *= 10;
    rVal += currentCharacter - '0';
  } else if(currentColorChannel == 'g'){
    gVal *= 10;
    gVal += currentCharacter - '0';
  } else if(currentColorChannel == 'b'){
    bVal *= 10;
    bVal += currentCharacter - '0';
  } else if(currentColorChannel == 'w'){
    wVal *= 10;
    wVal += currentCharacter - '0';
  }
}

// Occasionally write our color information to the EEPROM so it can survive a power cycle.
void writeEEPROM() {
  currentTime = millis();
  if( (currentTime - eepromLastUpdate ) >= eepromThrottleInterval ) {
      EEPROM.update(0, displayPattern);
    if(displayPattern == 's') {
      EEPROM.update(1, rVal);
      EEPROM.update(2, gVal);
      EEPROM.update(3, bVal);
      EEPROM.update(4, wVal);
    } else if(displayPattern == 'm') {
      EEPROM.update(1, matrixColorMode);
    }
    
    eepromLastUpdate = currentTime;
  }
}

// Attempt to read EEPOM state when booting up, to use last known pattern and color.
void readEEPROM() {
  displayPattern = EEPROM.read(0);

  if(displayPattern == 's') {
    rVal = EEPROM.read(1);
    gVal = EEPROM.read(2);
    bVal = EEPROM.read(3);
    wVal = EEPROM.read(4);
  } else if(displayPattern == 'm') {
    matrixColorMode = EEPROM.read(1);
  }
}

/**
 * Color Utilities.
 */
 // Instructs all LED to display the same color, then renders.
void oneColor(uint32_t color) {
  for(uint16_t i=0; i<strip.numPixels(); i++) {
      strip.setPixelColor(i, color);
  }
  strip.show();
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
    if (matrixColorMode == 'r' && r <= dimAmount ) {
      r = r + dimAmount;
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
  
  uint32_t dimColor = strip.Color( r, g, b, w);
  
  return dimColor;
}

// Brighten a color by adding white.
uint32_t brightenColor(uint32_t color, byte whiteAmount) {
    uint32_t brightenColor = strip.Color( red(color), green(color), blue(color), whiteAmount);
    return brightenColor;
}

// Remap coordinates to an actual pixel number. Or a non-existent one.
uint16_t remapXY(uint8_t x, uint8_t y) {
  if ( x >= maxWidth || y >= maxHeight || x < 0 || y < 0 ) {
    return -1;
  }
  uint16_t pixelBlock = grid[y/8][x/8];
  if (-1 == pixelBlock) {
    return -1;
  }
  return innerRemapXY(x, y, pixelBlock);
}

// Remap coordinates to an actual pixel number within a panel.
uint16_t innerRemapXY(uint8_t x, uint8_t y, uint16_t pixelBlock) {
  return pixelBlock * 64 + block[y%8][x%8];
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3,0);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3,0);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0,0);
}

void rainbowCycle(uint32_t frames , uint32_t frameAdvance, uint32_t pixelAdvance ) {
  
  // Hue is a number between 0 and 3*256 than defines a mix of r->g->b where
  // hue of 0 = Full red
  // hue of 128 = 1/2 red and 1/2 green
  // hue of 256 = Full Green
  // hue of 384 = 1/2 green and 1/2 blue
  // ...
  unsigned int firstPixelHue = 0;     // Color for the first pixel in the string
  
  for(unsigned int j=0; j<frames; j++) {                                  
    unsigned int currentPixelHue = firstPixelHue;

    for(unsigned int i=0; i< NUM_LEDS; i++) {
      
      if (currentPixelHue>=(3*256)) {                  // Normalize back down incase we incremented and overflowed
        currentPixelHue -= (3*256);
      }
            
      unsigned char phase = currentPixelHue >> 8;
      unsigned char step = currentPixelHue & 0xff;
                 
      switch (phase) {
        case 0: 
          strip.setPixelColor( i, ~step, step,  0 );
          break;
          
        case 1: 
          strip.setPixelColor( i, 0, ~step, step );
          break;

        case 2: 
          strip.setPixelColor( i, step, 0, ~step );
          break;
      }

      currentPixelHue+=pixelAdvance;                                      

    } 
    strip.show();
    firstPixelHue += frameAdvance;
  }
}

