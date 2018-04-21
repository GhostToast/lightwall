#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

#define PIN 6

#define WIDTH 56
#define HEIGHT 32

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

/**
 * LED order in a given block.
 */
const uint16_t block[][8] = {
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
const uint16_t grid[][7] = {
  { 0,  -1,   4,  -1,   8,  -1,  12},
  {-1,   2,  -1,   6,  -1,  10,  -1},
  { 1,  -1,   5,  -1,   9,  -1,  13},
  {-1,   3,  -1,   7,  -1,  11,  -1}
};

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
  matrix.clear();
  for (uint8_t i = 0; i<=WIDTH; i++) {
    if ( i%2 ) {
      matrix.drawFastVLine(i, 0, HEIGHT, matrix.Color(0,min(4.5*i,255),0)); 
    } else {
      matrix.drawFastVLine(WIDTH-i, 0, HEIGHT, matrix.Color(0,0,min(4.5*i,255))); 
    }
  }
  matrix.show();
}


