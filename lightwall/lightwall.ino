#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <Entropy.h>
#include "rainColumn.h"

#ifdef __AVR__
  #include <avr/power.h>
#endif

#define PIN 6
#define NUM_LEDS 896
#define BRIGHTNESS 50
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

#define maxChars 16
rainColumn allRainColumns[56]; // Array to hold all rainColumn structs.
uint8_t maxWidth = 56;
uint8_t maxHeight = 32;
char inputString[maxChars];
char currentCharacter;
int index = 0;
int displayFlag = 0;
char currentColorChannel;
String rVal;
String bVal;
String gVal;
String wVal;
unsigned long eepromThrottleInterval = 500;
unsigned long eepromLastUpdate = 0;
unsigned long currentTime = 0;

// Setup it up.
void setup() {
  Serial.begin(115200);
  strip.setBrightness(BRIGHTNESS);
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  //Entropy.initialize();

  currentTime = millis();

  for( byte i=0; i<maxWidth; i++) {
    allRainColumns[i].column = i;
    assignColumnProperties( allRainColumns[i] );
  }

  //readEEPROM();
}

// The main loop.
void loop() {
  //processUserInput();
  //displayUserSelectedMode();
  randomSeed();
  makeItRain();
}

void makeItRain() {
  for( byte i=0; i<maxWidth; i++) {
    rainOneColumn( allRainColumns[i] );
  }
  // Render.
  strip.show();
}

// Assign Column Properties. Mostly random, maintain column, lastUpdated, and lastCompleted.
void assignColumnProperties( rainColumn &rainColumn ) {
  Serial.println((String) "Processing: " + rainColumn.column);
  rainColumn = {
    rainColumn.column, // column. maintained.
    0, // head
    random(4,16), // height
    0, // isRunning
    random(5,64), // dimAmount
    strip.Color(random(0, 32), random(175, 256), random(0, 32), 0), // color
    random(15,150), // interval
    random(500,3000), // sleepTime
    rainColumn.lastUpdated, // lastUpdated. maintained.
    rainColumn.lastCompleted, // lastCompleted. maintained.
  };
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
    if( rainColumn.head >= maxHeight*2 ) {

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
  
    strip.setPixelColor(remapXY(rainColumn.column,rainColumn.head), rainColumn.color);

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
          if ( oldPixelColor != 0 ) {
            strip.setPixelColor(oldPixel, dimColor(oldPixelColor, rainColumn.dimAmount));  
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
  while(Serial.available() > 0 ) {
    displayFlag = 0;
    if(index < maxChars-1){
      currentCharacter = Serial.read();
      processCharacter();
      index++;
    }
  }
}

// Render user's choice.
void displayUserSelectedMode() {
  if(displayFlag == 0) {
    oneColor(strip.Color(rVal.toInt(), gVal.toInt(), bVal.toInt(), wVal.toInt()));
    displayFlag = 1;
    index = 0;
    writeEEPROM();
  }
}

// Processes current character, setting color variables accordingly.
void processCharacter() {
  if(currentCharacter == 'r') {
    currentColorChannel = currentCharacter;
    rVal = "";
  } else if(currentCharacter == 'g') {
    currentColorChannel = currentCharacter;
    gVal = "";
  } else if(currentCharacter == 'b') {
    currentColorChannel = currentCharacter;
    bVal = "";
  } else if(currentCharacter == 'w') {
    currentColorChannel = currentCharacter;
    wVal = "";
  } else if(currentColorChannel == 'r' && currentCharacter != 'r'){
    rVal += currentCharacter;
  } else if(currentColorChannel == 'g' && currentCharacter != 'g'){
    gVal += currentCharacter;
  } else if(currentColorChannel == 'b' && currentCharacter != 'b'){
    bVal += currentCharacter;
  } else if(currentColorChannel == 'w' && currentCharacter != 'w'){
    wVal += currentCharacter;
  }
}

// Occasionally write our color information to the EEPROM so it can survive a power cycle.
void writeEEPROM() {
  currentTime = millis();
  if( (currentTime - eepromLastUpdate ) >= eepromThrottleInterval ) {
    EEPROM.update(0, byte( rVal.toInt() ));
    EEPROM.update(1, byte( gVal.toInt() ));
    EEPROM.update(2, byte( bVal.toInt() ));
    EEPROM.update(3, byte( wVal.toInt() ));
    eepromLastUpdate = currentTime;
  }
}

// Attempt to read EEPOM state when booting up, to use last known color.
void readEEPROM() {
  rVal = EEPROM.read(0);
  gVal = EEPROM.read(1);
  bVal = EEPROM.read(2);
  wVal = EEPROM.read(3);
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
uint32_t dimColor(uint32_t color, byte dimAmount) {
    // Subtract R, G and B components until zero.
    uint32_t dimColor = strip.Color( max(0,red(color)-dimAmount), max(0,green(color)-dimAmount), max(0,blue(color)-dimAmount), 0);
    return dimColor;
}

// Returns red component or RGB color.
uint8_t red(uint32_t c) {
  return (c >> 16);
}

// Returns green component or RGB color.
uint8_t green(uint32_t c) {
  return (c >> 8);
}

// Returns blue component or RGB color.
uint8_t blue(uint32_t c) {
  return (c);
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
  pixelBlock = pixelBlock * 64 + block[y%8][x%8];
  return pixelBlock;
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

