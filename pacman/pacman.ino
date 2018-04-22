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
 * Pacman's open and shut frames, in both left and right variants.
 */
const unsigned short pacman[2][2][72] PROGMEM={
  // right open, right shut.
  {
    // open right.
    {
      0x0000, 0x0000, 0x39E0, 0x9CE0, 0xBDE0, 0xAD20, 0x5280, 0x0000,
      0x0000, 0x7BE0, 0xF780, 0xF780, 0xF780, 0xF780, 0xF780, 0x6B40,
      0x4220, 0xF780, 0xF780, 0xF780, 0x1080, 0xCE40, 0x8C60, 0x0000,
      0xA520, 0xF780, 0xF780, 0xF780, 0xE6E0, 0x7380, 0x0000, 0x0000,
      0xBDE0, 0xF780, 0xF780, 0xF780, 0xAD40, 0x0000, 0x0000, 0x0000,
      0x9CC0, 0xF780, 0xF780, 0xF780, 0xF780, 0x7380, 0x0000, 0x0000,
      0x3180, 0xEF60, 0xF780, 0xF780, 0xF780, 0xF760, 0x62E0, 0x0000,
      0x0000, 0x5AC0, 0xEF40, 0xF780, 0xF780, 0xF780, 0xEF60, 0x2120,
    },
    // shut right.
    {
      0x0000, 0x0000, 0x39E0, 0x9CE0, 0xBDE0, 0xA500, 0x5280, 0x0000,
      0x0000, 0x7BE0, 0xF780, 0xF780, 0xF780, 0xF780, 0xF780, 0x6B40,
      0x4220, 0xF780, 0xF780, 0xEF60, 0x0000, 0xE700, 0xE700, 0x2940,
      0xA520, 0xF780, 0xF780, 0xEF20, 0xEF40, 0xEF40, 0xEF40, 0xE700,
      0xBDE0, 0xF780, 0xF780, 0xA500, 0x9CA0, 0x8C40, 0x8C20, 0x94A0,
      0x9CC0, 0xF780, 0xF780, 0xF780, 0xEF40, 0xE720, 0xEF40, 0xE700,
      0x3180, 0xEF60, 0xF780, 0xF780, 0xF780, 0xF760, 0xE700, 0x4220,
      0x0000, 0x5AC0, 0xEF40, 0xF780, 0xF780, 0xF780, 0xEF40, 0x2120,
    },
  },
  // left open, left shut.
  {
    // open left.
    {
      0x0000, 0x5280, 0xAD20, 0xBDE0, 0x9CE0, 0x39E0, 0x0000, 0x0000,
      0x6B40, 0xF780, 0xF780, 0xF780, 0xF780, 0xF780, 0x7BE0, 0x0000,
      0x0000, 0x8C60, 0xCE40, 0x1080, 0xF780, 0xF780, 0xF780, 0x4220,
      0x0000, 0x0000, 0x7380, 0xE6E0, 0xF780, 0xF780, 0xF780, 0xA520,
      0x0000, 0x0000, 0x0000, 0xAD40, 0xF780, 0xF780, 0xF780, 0xBDE0,
      0x0000, 0x0000, 0x7380, 0xF780, 0xF780, 0xF780, 0xF780, 0x9CC0,
      0x0000, 0x62E0, 0xF760, 0xF780, 0xF780, 0xF780, 0xEF60, 0x3180,
      0x2120, 0xEF60, 0xF780, 0xF780, 0xF780, 0xEF40, 0x5AC0, 0x0000,
    },
    // shut left.
    {
      0x0000, 0x5280, 0xA500, 0xBDE0, 0x9CE0, 0x39E0, 0x0000, 0x0000,
      0x6B40, 0xF780, 0xF780, 0xF780, 0xF780, 0xF780, 0x7BE0, 0x0000,
      0x2940, 0xE700, 0xE700, 0x0000, 0xEF60, 0xF780, 0xF780, 0x4220,
      0xE700, 0xEF40, 0xEF40, 0xEF40, 0xEF20, 0xF780, 0xF780, 0xA520,
      0x94A0, 0x8C20, 0x8C40, 0x9CA0, 0xA500, 0xF780, 0xF780, 0xBDE0,
      0xE700, 0xEF40, 0xE720, 0xEF40, 0xF780, 0xF780, 0xF780, 0x9CC0,
      0x4220, 0xE700, 0xF760, 0xF780, 0xF780, 0xF780, 0xEF60, 0x3180,
      0x2120, 0xEF40, 0xF780, 0xF780, 0xF780, 0xEF40, 0x5AC0, 0x0000,
    }
  }
};

long lastAnimation = 0;

/**
 * Setup the lightwall matrix.
 * 
 * Each panel is 8x8 pixels. There are 7 columns and 2 rows but they are arranged in a checkerboard pattern.
 * We fake to the matrix that we have twice as much height to create a believable offset of negative space.
 */
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(WIDTH, HEIGHT, PIN,
  NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRBW           + NEO_KHZ800
);

/**
 * Setup.
 */
void setup() {
  matrix.setRemapFunction(remapXY);
  matrix.setBrightness(BRIGHTNESS);
  matrix.begin();
}

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
 * Main loop.
 */
void loop() {
  matrix.fillScreen(0);
  pacmanAnimation(30);
}

/**
 * Make Pacman Move.
 */
void pacmanAnimation(uint8_t pacmanSpeed) {
  uint8_t r = 0;

  // Run along each row.
  for (uint8_t y = 0; y<matrix.height()/8; y++) {

    // Scroll across the screen's width.
    for (uint16_t x = 0; x<matrix.width();) {
      // Timer to avoid using delay().
      if(millis() - lastAnimation > pacmanSpeed) {
        x++;
        lastAnimation = millis();
        if (y & 1) {
          // Run left.
          drawPacmanFrame(matrix.width()-x, y, 1);
        } else {
          // Run right.
          drawPacmanFrame(x, y, 0);
        }
      }
    }
  }
}

/**
 * Advance Pacman's animation 1 frame.
 */
void drawPacmanFrame(uint8_t x, uint8_t y, uint8_t r ) {
  uint8_t pattern;
  // Even/odd counter for whether to show mouth open or closed.
  if (x & 1) {
    pattern = 0;
  } else {
    pattern = 1;
  }
  matrix.clear();
  matrix.drawRGBBitmap(x, y*8, (const uint16_t *) pacman[r][pattern], 8, 8);
  matrix.show();
}

