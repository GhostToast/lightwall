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

/**
 * Remap X and Y to accomodate spacers.
 */
uint16_t remapXY(uint16_t x, uint16_t y) {
  uint16_t pixelBlock = grid[y/8][x/8];
  if (-1 == pixelBlock) {
    return pixelBlock;
  }
  pixelBlock = pixelBlock * 64 + block[y%8][x%8];
  return pixelBlock;
}

/**
 * Pacman (mouth) open.
 */
const unsigned short pacmanOpen[72] PROGMEM={
0x0000, 0x0000, 0x39E0, 0x9CE0, 0xBDE0, 0xAD20, 0x5280, 0x0000, 0x0000, 0x7BE0, 0xF780, 0xF780, 0xF780, 0xF780, 0xF780, 0x6B40,   // 0x0010 (16) pixels
0x4220, 0xF780, 0xF780, 0xF780, 0x6B60, 0xE700, 0x8C60, 0x0000, 0xA520, 0xF780, 0xF780, 0xF780, 0xE700, 0x7380, 0x0000, 0x0000,   // 0x0020 (32) pixels
0xBDE0, 0xF780, 0xF780, 0xF780, 0xAD40, 0x0000, 0x0000, 0x0000, 0x9CC0, 0xF780, 0xF780, 0xF780, 0xF780, 0x7380, 0x0000, 0x0000,   // 0x0030 (48) pixels
0x3180, 0xEF60, 0xF780, 0xF780, 0xF780, 0xF760, 0x62E0, 0x0000, 0x0000, 0x5AC0, 0xEF40, 0xF780, 0xF780, 0xF780, 0xEF60, 0x2120,   // 0x0040 (64) pixels
};

/**
 * Pacman (mouth) shut.
 */
const unsigned short pacmanShut[72] PROGMEM={
0x0000, 0x0000, 0x39E0, 0x9CE0, 0xBDE0, 0xA500, 0x5280, 0x0000, 0x0000, 0x7BE0, 0xF780, 0xF780, 0xF780, 0xF780, 0xF780, 0x6B40,   // 0x0010 (16) pixels
0x4220, 0xF780, 0xF780, 0xF780, 0x6B60, 0xE700, 0xE700, 0x2940, 0xA520, 0xF780, 0xF780, 0xEF20, 0xEF40, 0xEF40, 0xEF40, 0xE700,   // 0x0020 (32) pixels
0xBDE0, 0xF780, 0xF780, 0xA500, 0x9CA0, 0x8C40, 0x8C20, 0x94A0, 0x9CC0, 0xF780, 0xF780, 0xF780, 0xEF40, 0xE720, 0xEF40, 0xE700,   // 0x0030 (48) pixels
0x3180, 0xEF60, 0xF780, 0xF780, 0xF780, 0xF760, 0xE700, 0x4220, 0x0000, 0x5AC0, 0xEF40, 0xF780, 0xF780, 0xF780, 0xEF40, 0x2120,   // 0x0040 (64) pixels
};

/**
 * Setup.
 */
void setup() {
  matrix.setRemapFunction(remapXY);
  matrix.begin();
  matrix.setBrightness(40);
}

/**
 * Main loop.
 */
void loop() {
  matrix.fillScreen(0);
  pacmanAnimation(50);
}

/**
 * Make Pacman Move.
 */
void pacmanAnimation(uint8_t pacmanSpeed) {
  static unsigned long lastAnimation = 0;

  for (uint8_t y = 0; y<HEIGHT/8;) {
    // Scroll across the screen's width.
    for ( uint16_t x = 0; x<WIDTH;) {
      if(millis() - lastAnimation > pacmanSpeed) {
        x++;
        lastAnimation = millis();
        drawPacmanFrame(x, y);
      }
    }
    y++;
  }
}

/**
 * Advance Pacman's animation 1 frame.
 */
void drawPacmanFrame(uint8_t x, uint8_t y ) {
  short pattern;
  // Even/odd counter for whether to show mouth open or closed.
  if ( x & 1 ) {
    pattern = pacmanOpen;
  } else {
    pattern = pacmanShut;
  }
  matrix.clear();
  matrix.drawRGBBitmap(x, y*8, (const uint16_t *) pattern, 8, 8);
  matrix.show();
}

