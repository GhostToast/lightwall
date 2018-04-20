#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>

#define PIN 6

/**
 * Setup the lightwall matrix.
 * 
 * Each panel is 8x8 pixels. There are 7 columns and 2 rows but they are arranged in a checkerboard pattern.
 */
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(8, 8, 7, 2, PIN,
  NEO_MATRIX_TOP     + NEO_MATRIX_LEFT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG +
  NEO_TILE_TOP       + NEO_TILE_LEFT +
  NEO_TILE_COLUMNS   + NEO_TILE_PROGRESSIVE,
  NEO_GRBW           + NEO_KHZ800
);

/**
 * Overall width of entire matrix in pixels.
 */
const uint16_t matrixWidth = 56;

/**
 * Overall height of entire matrix in pixels.
 */
const uint16_t matrixHeight = 16;

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
  Serial.begin(115200); 
  matrix.begin();
  matrix.setBrightness(40);
}

/**
 * Main loop.
 */
void loop() {
  pacmanAnimation(50);
}

/**
 * Make Pacman Move.
 */
void pacmanAnimation(uint8_t pacmanSpeed) {
  static unsigned long lastAnimation = 0;

  // Scroll across the screen's width.
  for ( uint16_t i = 0; i<matrixWidth;) {
    if(millis() - lastAnimation > pacmanSpeed) {
      i++;
      lastAnimation = millis();

      drawPacmanFrame(i);
    }
  }
}

/**
 * Advance Pacman's animation 1 frame.
 */
void drawPacmanFrame(uint8_t frame) {
  // Even/odd counter for whether to show mouth open or closed.
  if ( i & 1 ) {
    matrix.clear();
    matrix.drawRGBBitmap(i, 8, (const uint16_t *) pacmanOpen, 8, 8);
    matrix.show();
  } else {
    matrix.clear();
    matrix.drawRGBBitmap(i, 8, (const uint16_t *) pacmanShut, 8, 8);
    matrix.show();
  }
}

