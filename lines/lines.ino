#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

#ifdef __AVR__
  #include <avr/power.h>
#endif

const int PIN        =   6;
const int WIDTH      =  56; // Actual row in pixels.
const int HEIGHT     =  32; // Fake: accounts for 2*8 additional empty pixels per column.
const int BRIGHTNESS =  50;
const int NUM_LEDS   = 896;

long lastAnimation = 0;

/**
 * Setup the lightwall matrix.
 * 
 * Each panel is 8x8 pixels. There are 7 columns and 2 rows but they are arranged in a checkerboard pattern.
 */
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(WIDTH, HEIGHT,PIN,
  NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRBW           + NEO_KHZ800
);

uint16_t remapXY(uint16_t x, uint16_t y) {

    // Compute panel position and offset of pixel within that panel.
    uint8_t pannel_x = x / 8;
    uint8_t offset_x = x % 8;
    uint8_t pannel_y = y / 8;
    uint8_t offset_y = y % 8;

    // Is this a missing panel?
    if ((pannel_x + pannel_y) & 1)
        return -1;

    // Compute panel number.
    uint8_t pannel = pannel_y / 2 + pannel_x * 2;

    // LED index within the panel.
    uint8_t led = 8 * offset_x;

    if (offset_x & 1)  // numbered bottom to top
        led += 7 - offset_y;
    else               // numbered top to bottom
        led += offset_y;

    return pannel * 64 + led;
}

/**
 * Setup.
 */
void setup() {
  matrix.setRemapFunction(remapXY);
  matrix.begin();
  matrix.setBrightness(50);
}

/**
 * Main loop.
 */
void loop() {
  matrix.fillScreen(0);
  theMatrix(100);
}

void theMatrix(uint8_t matrixSpeed){
  uint8_t x = random(0,matrix.width());
  for (uint8_t y=0;y<=matrix.height();y++){
    if(millis() - lastAnimation > matrixSpeed) {
      matrix.drawPixel(x,y, matrix.Color(0,200,0));
      matrix.show();
    }
  }
}

