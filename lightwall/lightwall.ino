#include <OctoSK6812.h>
#include <Entropy.h>
#include "rainColumn.h"
#include "cell.h"
#include "utilities.h"
#include "font.h"
#include "sprites.h"

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

// Just the outer rim.
const uint8_t perimeter[30] = {
  0,  1,   2,  3,  4,  5,  6,  7,
  8,  15, 16, 23, 24, 31, 32, 39,
  40, 47, 48, 55, 56, 57, 58, 59,
  60, 61, 62, 63
};

/**
   The entire grid.
*/
const int grid[4][4] = {
  {  0,  1,  2,  3, },
  {  4,  5,  6,  7, },
  {  8,  9, 10, 11, },
  { 12, 13, 14, 15  }
};

rainColumn allRainColumns[38]; // Array to hold all rainColumn structs.
uint8_t maxWidth = 38; // 32 + gaps
uint8_t maxHeight = 41; // 32 + gaps
cell allCells[38][41];  // Array to hold all "cell" structs.
byte lifeInitialized = 0;
byte lifePaused = 0;
byte lifeNewColor = 0;
uint16_t lifeSpeed = 370;             // Milliseconds per generation; UI-adjustable, see processLifeTiming().
const uint16_t lifeMinSpeed = 80;
const uint16_t lifeMaxSpeed = 1500;
byte lifeOrganic = 50;                // 0-100: how much each cell's fade start is staggered.
uint8_t lifeFadeInterval = 16;        // ~60fps display cadence, held fixed so smoothness doesn't change with speed.
// The two below are recomputed together by recomputeLifeTiming() whenever
// lifeSpeed or lifeOrganic changes -- see that function for the derivation.
// Both are real milliseconds (not a step count quantized to lifeFadeInterval)
// so that a per-cell fadeDelay drawn from [0, lifeStaggerMaxMs] has as many
// distinct possible values as the generation can afford, rather than the
// handful a coarse step count would allow -- the difference between a smooth
// ripple and a few cells visibly moving in unison.
uint16_t lifeStaggerMaxMs = 0;        // Widest per-cell fade start delay.
uint16_t lifeTotalMs = 1;             // Full per-generation time budget (after the display-tick margin); each cell's own fade span is lifeTotalMs minus its own fadeDelay, computed per-cell in the fade loop -- see lifeStart().
uint16_t lifeReviveThreshold = 12; // Below this many live cells, gently inject a glider rather than dying out.

// A minority of deaths leave a faint, slowly-decaying ember behind instead of
// going straight to black -- a cell that stays barely lit while the colony
// moves on around it. Deliberately stores no per-cell state (see cell.h's
// zero-padding-slack comment: one more byte per cell would cost 6.2KB across
// 1558 cells) -- see lifeCellEmbers() below for how membership is decided
// without a stored flag, and the ember decay pass in lifeStart() for how the
// glow is carried entirely in the LED framebuffer instead.
const uint8_t lifeEmberChance   = 25;  // Percent of deaths that leave an ember behind.
const uint8_t lifeEmberLevel    = 40;  // 0-255 brightness an ember holds at when its death fade lands.
const uint8_t lifeEmberDim      = 1;   // Brightness steps removed per ember decay tick.
const uint8_t lifeEmberInterval = 60;  // ms between ember decay ticks -- independent of lifeSpeed.
uint16_t lifeGeneration = 0;            // Bumped once per commit; mixed into lifeCellEmbers() so membership varies generation to generation.
unsigned long lifeEmberLastTime = 0;   // Ember decay clock, independent of lifeLastTime/lifeFadeLastTime.
byte fireInitialized = 0;
byte firePaused = 0;
byte fireSpeed = 80;
uint8_t fireBuffer[38][41];
uint32_t firePalette[256];
uint16_t fireHueShift = 0;
byte specialFire = 0;
char matrixColorMode = 'g';
byte matrixPaused = 0;
uint8_t matrixColors[4][2];
unsigned long currentTime = 0;
unsigned long globalLastTime = 0;
unsigned long lifeLastTime = 0;
// Life's fade cadence used to piggyback on globalLastTime, which doRGBW(),
// doSpecialHSL(), fireStarter(), and case 7 also write -- switching modes
// could leave a stale timestamp that stalled or fast-forwarded the next
// mode's first frame. A dedicated clock removes that cross-mode coupling.
unsigned long lifeFadeLastTime = 0;
unsigned long hslLastTime = 0;
byte fadeSteps = 32;
byte fadeIndex = 0;
uint16_t fadeInterval = 15;
// The stock frame is the longest command at 52 payload characters, so this has
// to clear it with room to spare. Every other command is well under 40.
const byte buffSize = 64;
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
byte rgbwShape = 0;
byte specialHSL = 0;
uint16_t hslInterval = 300;
uint16_t hVal = 0;
byte sVal = 0;
byte lVal = 0;

// Stock chart mode. The server does all the arithmetic and hands us finished
// rows, so these are just the decoded frame: one chart row per trading day, the
// baseline everything is measured against, and the two 4-glyph text bands.
const uint8_t chartWidth = 32;
const uint8_t chartHeight = 32;
uint8_t stockSeries[chartWidth];
uint8_t stockBaseline = chartHeight / 2;
char stockTicker[5] = {0};
char stockPrice[5] = {0};
byte stockFlags = 0;
byte stockReceived = 0; // Nothing to draw until the first frame lands.
// This chart is static between updates, so it is drawn once when something
// actually changes rather than on a timer. Redrawing a still image 25 times a
// second is not just wasted work -- any variation between refreshes shows up as
// shimmer on a display this bright.
byte stockDirty = 0;

// The LED strips are powered separately from the Teensy, so they can be switched
// off and on while this sketch keeps running. SK6812s hold their frame in their
// own registers and come back dark, so a mode that only draws on change would
// stay dark until its next update -- which for the stock chart could be the next
// trading day. Re-sending the buffer we already hold on a slow timer fixes that
// without recomputing anything: about five transfers a second against the 25 a
// continuous redraw would cost.
const uint16_t staticRefreshInterval = 200;
unsigned long staticRefreshTime = 0;

// The wall is 16 discrete 8x8 panels, so the usable virtual coordinates are
// interrupted by strut gaps -- 2 columns wide but 3 rows tall. These tables map
// a clean 32x32 chart space onto them, letting the render code below stay
// completely free of gap arithmetic. Compare the gap tests in remapXY().
const uint8_t chartCol[chartWidth] = {
   0,  1,  2,  3,  4,  5,  6,  7,
  10, 11, 12, 13, 14, 15, 16, 17,
  20, 21, 22, 23, 24, 25, 26, 27,
  30, 31, 32, 33, 34, 35, 36, 37
};
const uint8_t chartRow[chartHeight] = {
   0,  1,  2,  3,  4,  5,  6,  7,
  11, 12, 13, 14, 15, 16, 17, 18,
  22, 23, 24, 25, 26, 27, 28, 29,
  33, 34, 35, 36, 37, 38, 39, 40
};

// Text bands. Both sit inside a panel row so no glyph is bisected by a strut.
const uint8_t stockTickerRow = 0;                   // Chart rows 0-6.
const uint8_t stockPriceRow = chartHeight - GLYPH_HEIGHT; // Chart rows 25-31.

/*
   Palette. Every colour this mode draws is defined here, so it can be tuned by
   editing these numbers and reflashing -- nothing below hardcodes a colour.

   Green for gains and red for losses, which at this pixel density is worth
   keeping -- there is a lot of information in a small space, and up-is-green is
   read instantly in a way any other pairing has to be learned.

   What made an earlier version look like Christmas decoration was not the hues
   but the lightness: at saturation 100 and lightness 38 the green came out
   (0, 193, 0), a fully saturated LED at almost full output. Keeping saturation
   high but lightness low gives deep jewel tones instead of primaries, which
   reads as considered rather than festive.

   Saturation is 100 and the hues are exactly 120 and 0, which is what keeps the
   off-channels at zero. Anything else bleeds blue, and blue bleed is far more
   visible here than the numbers suggest, because the fill covers most of the
   display: an attempt at "emerald" hue 142 produced a fill of (1, 33, 13), where
   blue is 39% of green, and it read as aquamarine. Hue 355 for "crimson" gave
   (33, 1, 4) and read as magenta. Both looked correct in the web preview only
   because its colours were hand-picked rather than derived from these hues.

   Note that saturation is not the knob for subduing this. Desaturating a dark
   colour does not make it subtler, it makes it grey -- an amber tried at
   saturation 55 and lightness 7 came out (27, 19, 8) and read as cream. Hue
   survives darkness; saturation does not. Lightness is the knob.

   Hues follow hsl2rgb() in utilities.h: 0 red, 60 yellow, 120 green, 240 blue.
   Note the comment there claiming 120 is yellow is wrong -- tracing h2rgb()
   shows the scale is standard.

   Text and the baseline use the W channel alone. These are SK6812 RGBW, so
   white comes from a dedicated LED rather than mixing RGB -- it stays neutral
   and cannot be mistaken for part of the chart. It is also the third of the
   red/green/white trio, so it is kept well down: 80 here against the 180 the
   first version used.
*/
// Exact primaries. Do not nudge these off 120 and 0 -- see the note above about
// blue bleed. Subdue with the lightness values, not the hue or saturation.
const uint16_t stockGainHue = 120;
const uint16_t stockLossHue = 0;
const uint8_t stockFillSat = 100;
const uint8_t stockFillLight = 7;
const uint8_t stockGainLineSat = 100;
const uint8_t stockGainLineLight = 18;
// Red reads dimmer than green at equal lightness, so it gets a little more.
const uint8_t stockLossLineSat = 100;
const uint8_t stockLossLineLight = 21;
const uint8_t stockBaselineWhite = 22;
const uint8_t stockTextWhite = 80;

// Scales the whole mode as the very last step, without disturbing the relative
// weights above. Sent with every frame rather than fixed here: what is pleasant
// to look at in person is far too bright for a webcam, which has roughly half
// the dynamic range of an eye, and switching between the two should not need a
// reflash. Only the starting value lives here, for the window between boot and
// the first frame.
uint8_t stockBrightness = 155;

// Stale data is dimmed to this fraction of the above, so a dead poller is
// visible rather than quietly presenting month-old prices as current.
const uint8_t stockStaleDivisor = 2;

// Sprite mode. One 8x8 sprite per panel, 16 in all -- the panels are physically
// 8x8 with struts between them, so a sprite lands exactly inside one and the
// strut frames it. Like the chart, this is a still image redrawn only on change.
uint8_t spriteChoice[16] = {0};
uint8_t spriteBrightness = 155;
byte spritesReceived = 0;
byte spritesDirty = 0;

const int ledsPerStrip = 128;
#define NUM_LEDS 1024
#define BRIGHTNESS 50
DMAMEM int displayMemory[ledsPerStrip * 8];
int drawingMemory[ledsPerStrip * 8];

OctoSK6812 leds(ledsPerStrip, displayMemory, drawingMemory, SK6812_GRBW);

// remapXY returns this for gap struts and out-of-bounds coordinates.
#define NO_PIXEL 0xFFFF

// OctoSK6812::setPixel does NO bounds checking, so any draw originating from a
// remapXY result must be guarded -- an invalid index writes into (or past) the
// draw buffer. A single range check catches both NO_PIXEL and any stray index.
inline void setPixelSafe(uint16_t pixel, uint32_t color) {
  if (pixel < NUM_LEDS) leds.setPixel(pixel, color);
}

void setup() {
  Serial.begin(9600);

  // Seed the PRNG from the on-chip hardware RNG so the animations don't replay
  // the same sequence on every boot. Reseeded again on user input (see parseData).
  Entropy.Initialize();
  randomSeed(Entropy.random());

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
  // Drain the whole RX buffer each pass rather than one byte per loop(). At one
  // byte per iteration a 54-byte stock frame would take 54 frames to arrive,
  // and the hardware buffer is only 64 bytes deep.
  while ( Serial.available() > 0) {
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

  // A real user command (not a passive state poll) just arrived -- a fine,
  // clock-free moment to pull a fresh hardware seed so each new mode starts
  // from a different sequence.
  if ( strcmp(strtokIndex, "state") != 0 ) {
    randomSeed(Entropy.random());
  }

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
  } else if (strcmp(strtokIndex, "pausematrix") == 0) {
    userMode = 4;
    processMatrixPause(strtokIndex);
  } else if (strcmp(strtokIndex, "fire") == 0) {
    userMode = 5;
    processFire(strtokIndex);
  } else if (strcmp(strtokIndex, "firepause") == 0) {
    userMode = 6;
    processFirePause(strtokIndex);
  } else if (strcmp(strtokIndex, "hsl") == 0) {
    userMode = 7;
    processHSL(strtokIndex);
  } else if (strcmp(strtokIndex, "specialhsl") == 0) {
    userMode = 8;
    processSpecialHSL(strtokIndex);
  } else if (strcmp(strtokIndex, "specialfire") == 0) {
    userMode = 9;
    processSpecialFire(strtokIndex);
  } else if (strcmp(strtokIndex, "life") == 0) {
    userMode = 10;
    processLife(strtokIndex);
  } else if (strcmp(strtokIndex, "lifepause") == 0) {
    userMode = 11;
    processLifePause(strtokIndex);
  } else if (strcmp(strtokIndex, "lifetiming") == 0) {
    // Deliberately does not touch userMode -- this only adjusts Life's speed
    // and fade stagger, so it must be safe to send while paused and must not
    // yank the wall into Life from another mode.
    processLifeTiming(strtokIndex);
  } else if (strcmp(strtokIndex, "stock") == 0) {
    userMode = 12;
    processStock(strtokIndex);
  } else if (strcmp(strtokIndex, "sprites") == 0) {
    userMode = 14;
    processSprites(strtokIndex);
  }
}

void processState() {
  if (1 == userMode) {
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
    Serial.print(",");
    Serial.print(rgbwShape);
    Serial.println(">");
  } else if (4 == userMode) {
    Serial.print("<matrixpause,");
    Serial.print(matrixPaused);
    Serial.println(">");
  } else if (5 == userMode) {
    Serial.print("<fire,");
    Serial.print(fireHueShift);
    Serial.println(">");
  } else if (6 == userMode) {
    Serial.print("<firepause,");
    Serial.print(firePaused);
    Serial.println(">");
  } else if (7 == userMode) {
    Serial.print("<hsl,");
    Serial.print(hVal);
    Serial.print(",");
    Serial.print(sVal);
    Serial.print(",");
    Serial.print(lVal);
    Serial.println(">");
  } else if (8 == userMode) {
    Serial.print("<specialhsl,");
    Serial.print(specialHSL);
    Serial.println(">");
  } else if (9 == userMode) {
    Serial.print("<specialfire,");
    Serial.print(specialFire);
    Serial.println(">");
  } else if (10 == userMode) {
    Serial.print("<life,");
    Serial.print(hVal);
    Serial.print(",");
    Serial.print(sVal);
    Serial.print(",");
    Serial.print(lVal);
    Serial.print(",");
    Serial.print(lifeSpeed);
    Serial.print(",");
    Serial.print(lifeOrganic);
    Serial.println(">");
  } else if (11 == userMode) {
    Serial.print("<lifepause,");
    Serial.print(lifePaused);
    Serial.print(",");
    Serial.print(lifeSpeed);
    Serial.print(",");
    Serial.print(lifeOrganic);
    Serial.println(">");
  } else if (12 == userMode) {
    // Deliberately terse: the server is the source of truth for this mode and
    // caches the series itself, so there is no reason to echo 32 rows back up
    // the wire. It only needs to know the wall is on stock, and which symbol.
    Serial.print("<stock,");
    Serial.print(stockTicker);
    Serial.println(">");
  } else if (14 == userMode) {
    Serial.println("<sprites>");
  } else {
    //Serial.print("<fail>");
    Serial.println("x");
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

  // Get next part, which should be RGBW mode (shape).
  strtokIndex = strtok(NULL, ",");
  rgbwShape = atoi(strtokIndex);
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
  // Get the next part, which should be fireHueShift value.
  strtokIndex = strtok(NULL, ",");
  fireHueShift = atoi(strtokIndex);
  firePaused = 0;
  specialFire = 0;
}

void processFirePause(char * strtokIndex) {
  // Get fire boolean status.
  strtokIndex = strtok(NULL, ",");
  firePaused = atoi(strtokIndex);
  if (firePaused) {
    fireInitialized = 0;
  }
  specialFire = 0;
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
    for (byte z = 0; z < 2; z++) {
      strtokIndex = strtok(NULL, ",");
      matrixColors[i][z] = atoi(strtokIndex);
    }
  }
}

void processLife(char * strtokIndex) {
  rVal2 = rVal;
  gVal2 = gVal;
  bVal2 = bVal;
  lifePaused = 0;
  lifeNewColor = 1;

  // Get the next part, which should be Hue value.
  strtokIndex = strtok(NULL, ",");
  hVal = atoi(strtokIndex);

  // Get next part, which should be Saturation value.
  strtokIndex = strtok(NULL, ",");
  sVal = atoi(strtokIndex);

  // Get next part, which should be Lightness value.
  strtokIndex = strtok(NULL, ",");
  lVal = atoi(strtokIndex);
}

void processLifePause(char * strtokIndex) {
  // Get paused status (boolean).
  strtokIndex = strtok(NULL, ",");
  lifePaused = atoi(strtokIndex);
  if (lifePaused) {
    lifeInitialized = 0;
  }
}

/**
   <lifetiming,SSSS,OOO> -- adjust Life's generation speed and per-cell fade
   stagger only. Kept separate from <life,h,s,l>, which sets lifeNewColor and
   recolors every live cell -- dragging a speed slider must not destroy the
   genetic drift the colony has accumulated. Must not touch lifeNewColor or
   lifeInitialized either, so this is safe to send at any time, paused or not.
*/
void processLifeTiming(char * strtokIndex) {
  strtokIndex = strtok(NULL, ",");
  int requestedSpeed = atoi(strtokIndex);
  if (requestedSpeed < lifeMinSpeed) requestedSpeed = lifeMinSpeed;
  if (requestedSpeed > lifeMaxSpeed) requestedSpeed = lifeMaxSpeed;
  lifeSpeed = requestedSpeed;

  strtokIndex = strtok(NULL, ",");
  int requestedOrganic = atoi(strtokIndex);
  if (requestedOrganic < 0) requestedOrganic = 0;
  if (requestedOrganic > 100) requestedOrganic = 100;
  lifeOrganic = requestedOrganic;

  recomputeLifeTiming();
}

/**
   Decode a stock frame:

     <stock,SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS,B,TTTT,PPPP,F>

   S  32 chart rows, one character per trading day, encoded as 'A' + row
   B  baseline row, same encoding
   T  ticker, 4 glyph characters, '_' for blank
   P  price, 4 glyph characters
   F  flag bitfield; bit 0 means the data is stale

   Single characters rather than comma-separated integers is what keeps this
   inside one atomic frame -- as CSV it would be roughly 120 bytes and need a
   chunked protocol with ordering state.

   The server already constrains what it sends, but a malformed or truncated
   frame must not leave us drawing off the end of the wall, so every decoded
   value is clamped and a short frame is rejected outright.
*/
void processStock(char * strtokIndex) {
  // Series: one character per column.
  strtokIndex = strtok(NULL, ",");
  if (strtokIndex == NULL || strlen(strtokIndex) < chartWidth) {
    return; // Truncated frame; keep showing whatever we had.
  }
  for (uint8_t x = 0; x < chartWidth; x++) {
    uint8_t row = strtokIndex[x] - 'A';
    stockSeries[x] = (row < chartHeight) ? row : chartHeight - 1;
  }

  // Baseline row.
  strtokIndex = strtok(NULL, ",");
  if (strtokIndex == NULL) return;
  uint8_t baseline = strtokIndex[0] - 'A';
  stockBaseline = (baseline < chartHeight) ? baseline : chartHeight - 1;

  // Ticker and price, 4 glyphs each.
  strtokIndex = strtok(NULL, ",");
  if (strtokIndex == NULL) return;
  strncpy(stockTicker, strtokIndex, 4);
  stockTicker[4] = 0;

  strtokIndex = strtok(NULL, ",");
  if (strtokIndex == NULL) return;
  strncpy(stockPrice, strtokIndex, 4);
  stockPrice[4] = 0;

  // Flags are optional, so an older server still works.
  strtokIndex = strtok(NULL, ",");
  stockFlags = (strtokIndex == NULL) ? 0 : atoi(strtokIndex);

  // Brightness likewise: absent means keep whatever we are already using, so a
  // server predating this field does not black the wall out.
  strtokIndex = strtok(NULL, ",");
  if (strtokIndex != NULL) {
    int requested = atoi(strtokIndex);
    if (requested < 5) requested = 5;      // Fully off reads as a fault.
    if (requested > 255) requested = 255;
    stockBrightness = requested;
  }

  stockReceived = 1;
  stockDirty = 1;
}

/**
   Decode a sprite frame:

     <sprites,IIIIIIIIIIIIIIII,NNN>

   I  16 sprite choices, one per panel, encoded as 'A' + index. Panels run left
      to right then top to bottom, so the first four are the top row.
   N  overall brightness, 5-255 as decimal

   Only choices are sent, never pixels: 16 sprites of 64 pixels would be 1024
   values, far past what one serial frame can carry. The bitmaps live in
   sprites.h and the server just says which to place where.
*/
void processSprites(char * strtokIndex) {
  strtokIndex = strtok(NULL, ",");
  if (strtokIndex == NULL || strlen(strtokIndex) < 16) {
    return; // Truncated frame; keep showing whatever we had.
  }
  for (uint8_t panel = 0; panel < 16; panel++) {
    uint8_t choice = strtokIndex[panel] - 'A';
    spriteChoice[panel] = (choice < SPRITE_COUNT) ? choice : 0;
  }

  strtokIndex = strtok(NULL, ",");
  if (strtokIndex != NULL) {
    int requested = atoi(strtokIndex);
    if (requested < 5) requested = 5;
    if (requested > 255) requested = 255;
    spriteBrightness = requested;
  }

  spritesReceived = 1;
  spritesDirty = 1;
}

void processHSL(char * strtokIndex) {
  rVal2 = rVal;
  gVal2 = gVal;
  bVal2 = bVal;
  wVal2 = wVal;
  fadeIndex = 0;

  // Get the next part, which should be Hue value.
  strtokIndex = strtok(NULL, ",");
  hVal = atoi(strtokIndex);

  // Get next part, which should be Saturation value.
  strtokIndex = strtok(NULL, ",");
  sVal = atoi(strtokIndex);

  // Get next part, which should be Lightness value.
  strtokIndex = strtok(NULL, ",");
  lVal = atoi(strtokIndex);

  uint32_t color = hsl2rgb(hVal, sVal, lVal);
  rVal = red(color);
  gVal = green(color);
  bVal = blue(color);
  wVal = 0;
}

void processSpecialHSL(char * strtokIndex) {
  strtokIndex = strtok(NULL, ",");
  specialHSL = atoi(strtokIndex);
}

void processSpecialFire(char * strtokIndex) {
  strtokIndex = strtok(NULL, ",");
  specialFire = atoi(strtokIndex);
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
    if ( rainColumn.head > maxHeight + (rainColumn.height * 4) ) {

      // Inform that the animation has terminated, and set lastCompleted time.
      rainColumn.isRunning = 0;
      rainColumn.lastCompleted = currentTime;

      // Snap the column to its resting state so no partially-faded tail remnant
      // is left frozen at the bottom (short/slow streamers can run off the
      // canvas before their fade completes). Must run before assignColumnProperties,
      // which overwrites canGoBlack and color with new random values.
      finalizeRainColumn( rainColumn );

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
  setPixelSafe(remapXY(rainColumn.column, rainColumn.head), lightenColor(rainColumn.color, rainColumn.headLightness));
  setPixelSafe(remapXY(rainColumn.column, rainColumn.head - 1), lightenColor(rainColumn.color, round(rainColumn.headLightness * .66)));
  setPixelSafe(remapXY(rainColumn.column, rainColumn.head - 2), lightenColor(rainColumn.color, round(rainColumn.headLightness * .33)));

  // Standard color for behind the head.
  setPixelSafe(remapXY(rainColumn.column, rainColumn.head - 3), rainColumn.color);

  // Dim the tail.
  if ( rainColumn.head > rainColumn.height ) {
    for (byte tail = rainColumn.head - rainColumn.height; tail >= 0; tail--) {
      // Prevent wraparound.
      if ( tail == 255 ) {
        break;
      }
      uint16_t oldPixel = remapXY(rainColumn.column, tail);
      if ( oldPixel < NUM_LEDS ) {
        uint32_t oldPixelColor = leds.getPixel(oldPixel);
        if ( 0 != oldPixelColor ) {
          leds.setPixel(oldPixel, fadeTailColor(leds.getPixel(oldPixel), rainColumn.color, rainColumn.dimAmount, rainColumn.canGoBlack));
        }
      }
    }
  }

  // Increase head of the streamer, and set lastUpdated time.
  rainColumn.head++;
  rainColumn.lastUpdated = currentTime;
}

// Force a finished column to its final resting value across the whole canvas:
// pure black for canGoBlack columns, or the faint hue-correct ember otherwise.
// This guarantees the streamer appears to fall off-screen cleanly with no
// frozen partial-fade remnant, independent of its height or dim speed.
void finalizeRainColumn( rainColumn &rainColumn ) {
  uint32_t rest = 0;
  if ( ! rainColumn.canGoBlack ) {
    rest = makeColor( red(rainColumn.color)   >> 3,
                      green(rainColumn.color) >> 3,
                      blue(rainColumn.color)  >> 3,
                      white(rainColumn.color) >> 3 );
  }

  for ( uint8_t y = 0; y < maxHeight; y++ ) {
    uint16_t pixel = remapXY( rainColumn.column, y );
    if ( pixel < NUM_LEDS ) {
      leds.setPixel( pixel, rest );
    }
  }
}

// Remap coordinates to an actual pixel number. Or a non-existent one.
uint16_t remapXY(uint8_t x, uint8_t y) {
  // Boundary defense and gap handling to map 38x41 to physical 32x32
  if (x >= maxWidth || y >= maxHeight || x < 0 || y < 0) return NO_PIXEL;
  // Gap detection (8-9, 18-19, 28-29 for X; 8-10, 19-21, 30-32 for Y)
  if (x == 8 || x == 9 || x == 18 || x == 19 || x == 28 || x == 29) return NO_PIXEL;
  if ((y >= 8 && y <= 10) || (y >= 19 && y <= 21) || (y >= 30 && y <= 32)) return NO_PIXEL;

  // Normalize, handle 180-degree flip for left panels, and lookup grid
  uint8_t rx = x;
  if (rx > 29) rx -= 6; else if (rx > 19) rx -= 4; else if (rx > 9) rx -= 2;
  uint8_t ry = y;
  if (ry > 32) ry -= 9; else if (ry > 21) ry -= 6; else if (ry > 10) ry -= 3;
  if (rx < 16) {
    rx = 15 - rx;
    ry = 7 - (ry % 8) + ((ry / 8) * 8);
  }

  int pixelBlock = grid[ry / 8][rx / 8];
  return (pixelBlock < 0) ? NO_PIXEL : innerRemapXY(rx, ry, pixelBlock);
}

// Remap coordinates to an actual pixel number within a panel.
uint16_t innerRemapXY(uint8_t x, uint8_t y, uint16_t pixelBlock) {
  return pixelBlock * 64 + block[y % 8][x % 8];
}

// Fade a matrix streamer tail pixel while preserving its hue.
//
// Unlike dimColor() (which subtracts a fixed amount from each channel
// independently and is used by the fire pattern for single-pass flicker), this
// fades by stepping the brightest channel down and scaling the other channels
// to match. Holding the channel ratios fixed keeps the color correct -- amber
// (high R + mid G) fades as amber instead of collapsing toward its dominant
// primary -- while the constant step reproduces the original linear fade
// timing, so the tail leaves the scene on the same schedule as before.
//
// To smooth the most visible part of the fade, the step eases out near the
// bottom: above EASE_KNEE it is the full dimAmount, below it shrinks with
// brightness (floored at 1) so the last few levels glide to black instead of
// popping out. The eye is logarithmic, so a constant step looks harsh only
// down here at the low end.
//
// canGoBlack controls where the fade settles:
//   true  -> the column fades all the way to true black.
//   false -> the column holds a faint, hue-correct ember (~1/8 of its true
//            color, from baseColor) so the trail stays softly lit.
uint32_t fadeTailColor(uint32_t color, uint32_t baseColor, byte dimAmount, bool canGoBlack) {
  uint8_t r1 = red(color);
  uint8_t g1 = green(color);
  uint8_t b1 = blue(color);
  uint8_t w1 = white(color);

  uint8_t m1 = max( max(r1, g1), max(b1, w1) );
  if ( m1 == 0 ) return 0;

  const uint8_t EASE_KNEE = 24;
  uint8_t step = dimAmount;
  if ( m1 < EASE_KNEE ) {
    step = (uint8_t)( (uint16_t)m1 * dimAmount / EASE_KNEE );
    if ( step < 1 ) step = 1;
  }

  uint8_t m2 = (m1 > step) ? (m1 - step) : 0;

  // Scale every channel by m2/m1 to preserve hue.
  uint8_t r2 = (uint16_t)r1 * m2 / m1;
  uint8_t g2 = (uint16_t)g1 * m2 / m1;
  uint8_t b2 = (uint16_t)b1 * m2 / m1;
  uint8_t w2 = (uint16_t)w1 * m2 / m1;

  if ( ! canGoBlack ) {
    // Floor at a uniform fraction of the streamer's true color. Using the same
    // fraction for every channel keeps the ember the correct hue.
    uint8_t rf = red(baseColor)   >> 3;
    uint8_t gf = green(baseColor) >> 3;
    uint8_t bf = blue(baseColor)  >> 3;
    uint8_t wf = white(baseColor) >> 3;
    if (r2 < rf) r2 = rf;
    if (g2 < gf) g2 = gf;
    if (b2 < bf) b2 = bf;
    if (w2 < wf) w2 = wf;
  }

  return makeColor(r2, g2, b2, w2);
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

// Instructs all panels to display a color around just the perimeter.
void perimeterColor(uint32_t color, uint32_t fadeColor = -1) {
  if (fadeColor >= 0 && color != fadeColor && fadeIndex <= fadeSteps) {
    uint8_t r = ((red(fadeColor) * (fadeSteps - fadeIndex)) + (red(color) * fadeIndex)) / fadeSteps;
    uint8_t g = ((green(fadeColor) * (fadeSteps - fadeIndex)) + (green(color) * fadeIndex)) / fadeSteps;
    uint8_t b = ((blue(fadeColor) * (fadeSteps - fadeIndex)) + (blue(color) * fadeIndex)) / fadeSteps;
    uint8_t w = ((white(fadeColor) * (fadeSteps - fadeIndex)) + (white(color) * fadeIndex)) / fadeSteps;
    color = makeColor( r, g, b, w );
    fadeIndex++;
  }
  // For each panel.
  for (uint8_t panel = 0; panel < 16; panel++) {
    // For each pixel within the perimeter.
    for (uint8_t pixel = 0; pixel < 30; pixel++) {
      leds.setPixel(panel * 64 + perimeter[pixel], color);
    }
  }
  leds.show();
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

void doRGBW() {
  if (currentTime - globalLastTime >= fadeInterval) {
    globalLastTime = currentTime;
    if (rgbwShape == 0) {
      oneColor( makeColor( rVal, gVal, bVal, wVal ), makeColor( rVal2, gVal2, bVal2, wVal2 ));
    } else if (rgbwShape == 1) {
      perimeterColor( makeColor( rVal, gVal, bVal, wVal ), makeColor( rVal2, gVal2, bVal2, wVal2 ));
    }
  }
}

// Special HSL such as rainbow.+
void doSpecialHSL() {
  if (1 == specialHSL) {
    if (currentTime - globalLastTime >= fadeInterval) {
      globalLastTime = currentTime;
      if ( (currentTime - hslLastTime ) > hslInterval ) {
        hslLastTime = currentTime;
        hVal++;
        if (hVal == 360) {
          hVal = 0;
        }
        rVal2 = rVal;
        gVal2 = gVal;
        bVal2 = bVal;
        wVal2 = wVal;

        uint32_t newColor = hsl2rgb(hVal, 100, 10);
        rVal = red(newColor);
        gVal = green(newColor);
        bVal = blue(newColor);
        wVal = 0;
      }

      oneColor(makeColor(rVal, gVal, bVal, wVal), makeColor( rVal2, gVal2, bVal2, wVal2 ));
    }
  }
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
        setPixelSafe(remapXY(x, y), color);
      }
    }
    gradientProcessed = 1;
  }

  leds.show();
}

// Lay a glider into nextColor at (x,y). A glider is alive and mobile, so it
// reliably revives a dwindling board (and travels the torus) instead of fizzling.
// Writing nextColor (with currentColor left at 0) lets the fade pipeline ease it in.
void seedGlider(uint8_t x, uint8_t y) {
  static const uint8_t glider[5][2] = {{0, 1}, {1, 2}, {2, 0}, {2, 1}, {2, 2}};
  for ( byte i = 0; i < 5; i++ ) {
    uint8_t gx = (x + glider[i][0]) % maxWidth;
    uint8_t gy = (y + glider[i][1]) % maxHeight;
    allCells[gx][gy].hVal = hVal;
    allCells[gx][gy].nextColor = hsl2rgb(hVal, sVal, lVal);
    allCells[gx][gy].fadeDelay = random(0, lifeStaggerMaxMs + 1);
  }
}

// Derive the per-generation fade schedule from lifeSpeed and lifeOrganic, in
// real milliseconds. lifeFadeInterval (the ~60fps display cadence) stays
// fixed, so redraw cost is constant across the whole speed range -- only
// lifeStaggerMaxMs (how widely start times spread) and lifeTotalMs (the
// budget those start times spread across) scale.
//
// One display tick's worth of time is always held back for the commit at the
// top of the next generation (see lifeStart()): a cell's own fade span is
// computed there, per cell, as lifeTotalMs minus that cell's own fadeDelay --
// so a cell starting at the maximum stagger delay still has exactly enough
// span left to reach full resolution right as lifeTotalMs elapses, and every
// cell that started earlier keeps animating for its own longer remaining
// span instead of finishing early and sitting idle. Every fade normally
// completes inside its own generation by construction, at any speed, rather
// than by clamping against the clock. "Normally" -- that held-back tick is a
// fixed margin, and a stall elsewhere in loop() can still eat it; the
// finalize step at the top of lifeStart()'s commit block is what actually
// guarantees a cell's resting value gets drawn regardless, so this margin
// only has to be good enough that fades look complete on their own most of
// the time, not perfect under worst-case jitter.
void recomputeLifeTiming() {
  lifeTotalMs = (lifeSpeed > lifeFadeInterval) ? (lifeSpeed - lifeFadeInterval) : lifeFadeInterval;
  // Up to 60% of the generation can go to stagger -- spreading start times
  // wider than that starts eating into the average cell's remaining fade
  // span faster than it buys visible spread, and that span is what makes
  // Organic feel gentle rather than snappy. This bounds the worst case (the
  // cell that draws the maximum delay still gets lifeTotalMs - lifeStaggerMaxMs
  // to fade in, floored per-cell against lifeFadeInterval in the fade loop),
  // but every other cell gets more than that, up to the full lifeTotalMs for
  // a cell that starts immediately.
  lifeStaggerMaxMs = (uint16_t)( ( (uint32_t) lifeTotalMs * 6 / 10 ) * lifeOrganic / 100 );
}

// Integer easing for Life's per-cell fade -- no floats, no fmod, no lookup
// table; a Cortex-M4 divide is a few cycles and ~1558 cells at 60fps is well
// inside budget. Birth rises fast and eases into full brightness, a bloom,
// with a quadratic. Death drops fast and then crawls the last stretch toward
// black with a cubic: the eye is roughly logarithmic, so a constant step in
// linear PWM only looks harsh down at the low end (the same reasoning behind
// fadeTailColor()'s EASE_KNEE above), and the cubic spends noticeably more of
// its travel lingering at low brightness than birth's quadratic does. This is
// the dying tail of a single fade, not the persistent ember below -- an
// ember-selected cell's death floors this curve partway down (see the fade
// loop in lifeStart()) and hands off to its own decay clock instead of
// riding this curve down to true black.
uint8_t easeLifeBirth(uint8_t t) {
  uint16_t inv = 255 - t;
  return 255 - (uint8_t)( (inv * inv) / 255 );
}

uint8_t easeLifeDeath(uint8_t t) {
  uint32_t inv = 255 - t;
  return (uint8_t)( (inv * inv * inv) / 65025UL ); // 255^2, so the result still lands in 0..255
}

// Whether (w, h) is one of the lifeEmberChance% of cells whose death, in the
// generation currently finishing, leaves an ember rather than going straight
// to black. A hash rather than a stored per-cell flag -- cell.h's struct is
// exactly 12 bytes with no padding slack, so any new field costs 4 padded
// bytes x 1558 cells. Mixing lifeGeneration in means the same cell isn't
// perpetually ember-eligible or perpetually not; the multiply/xor/shift mix
// (not a bare sum) keeps the selection free of the diagonal/periodic stripes
// a simpler hash of adjacent grid coordinates would produce. Called from both
// the commit block (to seed an ember) and the per-frame fade loop (to floor
// that cell's fade at lifeEmberLevel instead of easeLifeDeath's 0), which
// must agree on membership for as long as that generation's fade is playing
// out -- lifeGeneration is bumped only once, after the commit block finishes
// with the generation being finalized.
bool lifeCellEmbers(uint8_t w, uint8_t h) {
  uint32_t x = ( (uint32_t) lifeGeneration * 2654435761UL ) ^ ( (uint32_t) w * 40503UL ) ^ ( (uint32_t) h * 2246822519UL );
  x ^= x >> 15;
  x *= 2246822519UL;
  x ^= x >> 13;
  return ( x % 100 ) < lifeEmberChance;
}

void lifeStart() {
  if ( lifePaused ) {
    oneColor(0);
    return;
  }

  // Initialize cells if this is first run.
  if ( ! lifeInitialized ) {
    oneColor(0);
    recomputeLifeTiming();
    for ( byte w = 0; w < maxWidth; w++) {
      for ( byte h = 0; h < maxHeight; h++) {
        // Gap (strut) cells are seeded and simulated like any other cell; they
        // simply aren't drawn (setPixelSafe drops their NO_PIXEL index).
        allCells[w][h].hVal = hVal;
        allCells[w][h].fadeDelay = 0;

        // Populate life randomly to 20% of board.
        if ( random(1, 101) > 80 ) {
          allCells[w][h].currentColor = hsl2rgb(hVal, sVal, lVal);
          allCells[w][h].nextColor = hsl2rgb(hVal, sVal, lVal);
          setPixelSafe( remapXY(w, h), hsl2rgb(hVal, sVal, lVal) );
        }
      }
    }
    lifeInitialized = true;
  } else if ( lifeNewColor ) {
    // Set new color immediately.
    for ( byte w = 0; w < maxWidth; w++) {
      for ( byte h = 0; h < maxHeight; h++) {
        if ( allCells[w][h].currentColor ) {
          allCells[w][h].hVal = hVal;
        }
      }
    }
    lifeNewColor = 0;
  }

  uint8_t neighborCount = 0;
  uint16_t currentLifeCount = 0;

  // Advance to the next generation. The commit -- currentColor becomes the
  // nextColor computed last time -- happens first, at the top of this block,
  // rather than after the fade loop below (the old order). The old order
  // gated the commit on "lifeFadeIndex > lifeFadeSteps || currentTime -
  // lifeLastTime >= lifeSpeed": at a fast enough lifeSpeed that second clause
  // could fire before the fade had run all its steps, truncating a
  // transition mid-flight. Committing here instead means the fade drawn
  // below is *usually* for the generation computed on the previous pass
  // through this block, which -- by construction, see recomputeLifeTiming()
  // -- normally finishes fading before this commit runs again, at any speed.
  // "Usually" and "normally": that margin is a fixed lifeFadeInterval
  // (~16ms), a large fraction of a fast generation and a tiny one of a slow
  // one, and a stall elsewhere in loop() can still eat it -- the finalize
  // step immediately below exists because relying on the fade loop alone to
  // have already written a cell's resting value was exactly that gap: a
  // stall spanning a generation boundary could leave a pixel frozen
  // mid-fade forever once currentColor/nextColor became equal below and the
  // fade loop's "nothing changed" skip took over.
  if ( currentTime - lifeLastTime >= lifeSpeed ) {
    lifeLastTime = currentTime;

    // Finalize the fade that just finished, before committing to the next
    // generation -- snap every transitioning cell to its true resting value
    // rather than trusting that the fade loop already drew it. A birth
    // resolves to nextColor exactly. A death resolves to true black, unless
    // lifeCellEmbers() selects this cell as one of lifeEmberChance% that
    // instead holds at lifeEmberLevel and hands off to the ember decay pass
    // near the bottom of this function, which fades it out on its own clock.
    for ( byte w = 0; w < maxWidth; w++) {
      for ( byte h = 0; h < maxHeight; h++) {
        cell &thisCell = allCells[w][h];
        if ( thisCell.currentColor == thisCell.nextColor ) {
          continue;
        }
        uint16_t pixel = remapXY(w, h);
        if ( thisCell.nextColor != 0 ) {
          setPixelSafe( pixel, thisCell.nextColor );
        } else if ( lifeCellEmbers(w, h) ) {
          uint8_t rTemp = (uint8_t)( ( (uint16_t) red(thisCell.currentColor)   * lifeEmberLevel ) / 255 );
          uint8_t gTemp = (uint8_t)( ( (uint16_t) green(thisCell.currentColor) * lifeEmberLevel ) / 255 );
          uint8_t bTemp = (uint8_t)( ( (uint16_t) blue(thisCell.currentColor)  * lifeEmberLevel ) / 255 );
          setPixelSafe( pixel, makeColor( rTemp, gTemp, bTemp ) );
        } else {
          setPixelSafe( pixel, 0 );
        }
      }
    }

    for ( byte w = 0; w < maxWidth; w++) {
      for ( byte h = 0; h < maxHeight; h++) {
        allCells[w][h].currentColor = allCells[w][h].nextColor;
      }
    }

    for ( byte w = 0; w < maxWidth; w++) {
      for ( byte h = 0; h < maxHeight; h++) {

        neighborCount = getNeighborCount(w, h);
        if ( allCells[w][h].currentColor && neighborCount < 2 ) {
          // Cell dies with less than 2 neighbors.
          allCells[w][h].nextColor = 0;
          allCells[w][h].fadeDelay = random(0, lifeStaggerMaxMs + 1);
        } else if ( allCells[w][h].currentColor && ( neighborCount == 2 || neighborCount == 3 ) ) {
          // Cell continues living if 2 or 3 neighbors. Keep same color (no mutation).
          allCells[w][h].nextColor = allCells[w][h].currentColor;
          currentLifeCount++;
        } else if ( allCells[w][h].currentColor && neighborCount > 3 ) {
          // Cell dies if more than 3 neighbors.
          allCells[w][h].nextColor = 0;
          allCells[w][h].fadeDelay = random(0, lifeStaggerMaxMs + 1);
        } else if ( ! allCells[w][h].currentColor && neighborCount == 3 ) { // 3 || 6 = high life.
          // New life spawns if exactly 3 neighbors.
          currentLifeCount++;
          allCells[w][h].nextColor = hsl2rgb(allCells[w][h].hVal, sVal, lVal);
          allCells[w][h].fadeDelay = random(0, lifeStaggerMaxMs + 1);
        } else if ( ! allCells[w][h].currentColor && neighborCount > 0 && ( random(1, 101) > 99 ) ) {
          // Chance of spontaneous life to keep from going stagnant.
          currentLifeCount++;
          allCells[w][h].nextColor = hsl2rgb(allCells[w][h].hVal, sVal, lVal);
          allCells[w][h].fadeDelay = random(0, lifeStaggerMaxMs + 1);
        }
      }
    }
    // Gentle prevention: when the colony is dwindling (but not yet dead), drop in
    // a glider rather than letting it collapse. Probabilistic so it stays subtle
    // and doesn't flood a small stable population.
    if ( currentLifeCount > 0 && currentLifeCount < lifeReviveThreshold && random(1, 101) > 50 ) {
      seedGlider( random(0, maxWidth), random(0, maxHeight) );
      currentLifeCount += 5;
    }

    // Soft reset: if the board still went extinct, shift the hue and lay a fresh
    // ~20% seed into nextColor of the (now empty) cells. Leaving currentColor at 0
    // lets the existing fade pipeline crossfade the new generation in from black,
    // instead of the old hard reset that snapped to full brightness.
    if ( currentLifeCount == 0 ) {
      hVal = (hVal + 1) % 360;
      for ( byte w = 0; w < maxWidth; w++) {
        for ( byte h = 0; h < maxHeight; h++) {
          allCells[w][h].hVal = hVal;
          if ( ! allCells[w][h].currentColor && random(1, 101) > 80 ) {
            allCells[w][h].nextColor = hsl2rgb(hVal, sVal, lVal);
            allCells[w][h].fadeDelay = random(0, lifeStaggerMaxMs + 1);
          }
        }
      }
    }

    // Advance last, so lifeCellEmbers() still answers for the generation the
    // finalize step above just resolved, and only starts answering for the
    // new one once this generation's fadeDelay/nextColor targets (assigned
    // above) are the ones about to animate.
    lifeGeneration++;
  }

  // Fade every cell toward the next generation, at a constant ~60fps display
  // cadence independent of lifeSpeed -- this is what keeps the fade smooth at
  // any speed setting instead of jumping in coarse steps when slow or in one
  // jump when fast.
  if ( currentTime - lifeFadeLastTime >= lifeFadeInterval ) {
    lifeFadeLastTime = currentTime;
    for ( byte w = 0; w < maxWidth; w++) {
      for ( byte h = 0; h < maxHeight; h++) {
        cell &thisCell = allCells[w][h];
        if ( thisCell.currentColor == thisCell.nextColor ) {
          // Nothing changed for this cell -- most of the board, most
          // generations. Skipping here avoids a three-channel blend that
          // would just resolve back to the color already on screen.
          continue;
        }

        // Cells start fading at their own moment within the generation --
        // fadeDelay, drawn from [0, lifeStaggerMaxMs] when the transition was
        // assigned above -- so births and deaths ripple across the board
        // instead of landing in lockstep. The delay is a real millisecond
        // offset rather than a count of lifeFadeInterval-sized steps, so
        // there are as many distinct start times as the generation can
        // afford instead of only a handful -- with few enough steps, most of
        // the cells changing in a generation would land on the same one and
        // move as a visible clump rather than a spread-out ripple. Rather
        // than sharing one fixed span across every changing cell, each
        // cell's span is however much of lifeTotalMs is left after its own
        // fadeDelay -- so every cell, not just the one that happened to draw
        // the maximum delay, keeps animating right up to the close of the
        // generation's fade window instead of finishing early and sitting
        // idle. That also means fade duration itself varies cell to cell,
        // proportional to how early or late each one started, instead of
        // being one fixed number board-wide.
        uint16_t elapsedMs = (uint16_t)( currentTime - lifeLastTime );
        uint16_t cellSpanMs = ( lifeTotalMs > thisCell.fadeDelay ) ? ( lifeTotalMs - thisCell.fadeDelay ) : lifeFadeInterval;
        if ( cellSpanMs < lifeFadeInterval ) cellSpanMs = lifeFadeInterval;
        uint8_t t;
        if ( elapsedMs <= thisCell.fadeDelay ) {
          t = 0;
        } else {
          uint16_t progressMs = elapsedMs - thisCell.fadeDelay;
          if ( progressMs > cellSpanMs ) progressMs = cellSpanMs;
          t = (uint8_t)( ( (uint32_t) progressMs * 255 ) / cellSpanMs );
        }

        // A cell fading here always has exactly one of currentColor/nextColor
        // at 0 -- a living cell that stays alive keeps its color unchanged
        // (handled above) and gets skipped by the check above, so there is
        // only ever one real color to scale here: a birth scales nextColor up
        // from black, a death scales currentColor down to it. See
        // easeLifeBirth()/easeLifeDeath() above for the two curves.
        uint32_t baseColor;
        uint8_t f;
        bool isBirth = ( thisCell.currentColor == 0 );
        if ( isBirth ) {
          baseColor = thisCell.nextColor;
          f = easeLifeBirth(t);
        } else {
          baseColor = thisCell.currentColor;
          f = easeLifeDeath(t);
          // This cell is one of the lifeEmberChance% selected to leave an
          // ember (see lifeCellEmbers() and the finalize step above) --
          // floor its death here at lifeEmberLevel instead of riding
          // easeLifeDeath() down to true black, so the fade loop and the
          // finalize step agree on where this generation's fade actually
          // lands. The ember decay pass below takes it the rest of the way
          // to black on its own clock, after this generation commits.
          if ( f < lifeEmberLevel && lifeCellEmbers(w, h) ) f = lifeEmberLevel;
        }

        uint8_t rTemp = (uint8_t)( ( (uint16_t) red(baseColor)   * f ) / 255 );
        uint8_t gTemp = (uint8_t)( ( (uint16_t) green(baseColor) * f ) / 255 );
        uint8_t bTemp = (uint8_t)( ( (uint16_t) blue(baseColor)  * f ) / 255 );

        uint16_t pixel = remapXY(w, h);
        if ( isBirth && pixel < NUM_LEDS ) {
          // A cell reborn while its previous death's ember is still glowing
          // (ember decay pass below) would otherwise have this bloom snap
          // straight over it, flashing black for a frame first. Taking the
          // per-channel max against what's already lit lets the bloom
          // simply overtake the ember instead of replacing it outright.
          uint32_t existing = leds.getPixel(pixel);
          if ( red(existing)   > rTemp ) rTemp = red(existing);
          if ( green(existing) > gTemp ) gTemp = green(existing);
          if ( blue(existing)  > bTemp ) bTemp = blue(existing);
        }

        setPixelSafe( pixel, makeColor( rTemp, gTemp, bTemp ) );
      }
    }
  }

  // Ember decay: on its own clock, independent of lifeSpeed and of
  // lifeFadeInterval, so an ember lingers the same real length of time no
  // matter how fast Life itself is running. Only cells the fade loop above
  // is no longer touching (currentColor == nextColor == 0 -- fully dead and
  // not mid-transition) are eligible, so this never fights the fade loop
  // over the same pixel. Reads the framebuffer rather than any per-cell
  // state -- there is nowhere to put per-cell state; see cell.h -- so this
  // also mops up any pixel a truncated fade left stranded before the
  // finalize step above existed, not just the embers it seeds on purpose.
  // fadeTailColor() is the same hue-preserving decay Matrix rain uses for
  // its tail (see fadeTailColor() above); baseColor is unused here since an
  // ember always decays all the way to true black (canGoBlack = true).
  if ( currentTime - lifeEmberLastTime >= lifeEmberInterval ) {
    lifeEmberLastTime = currentTime;
    for ( byte w = 0; w < maxWidth; w++) {
      for ( byte h = 0; h < maxHeight; h++) {
        cell &thisCell = allCells[w][h];
        if ( thisCell.currentColor != 0 || thisCell.nextColor != 0 ) {
          continue;
        }
        uint16_t pixel = remapXY(w, h);
        if ( pixel >= NUM_LEDS ) continue;
        uint32_t existing = leds.getPixel(pixel);
        if ( existing != 0 ) {
          leds.setPixel( pixel, fadeTailColor( existing, 0, lifeEmberDim, true ) );
        }
      }
    }
  }

  leds.show();
}

// Neighbor lookups treat the full 38x41 array as one continuous logical grid.
// Gap cells (the wood struts between panes) are ordinary Life cells that compute
// and hold state -- they are simply not displayed (see remapXY). This lets a
// pattern traverse "behind" a strut, taking a few generations to cross the hidden
// band before re-emerging on the next pane, while computation continues off-camera.
uint8_t above( uint8_t y ) { return (y + maxHeight - 1) % maxHeight; }

uint8_t below( uint8_t y ) { return (y + 1) % maxHeight; }

uint8_t left( uint8_t x ) { return (x + maxWidth - 1) % maxWidth; }

uint8_t right( uint8_t x ) { return (x + 1) % maxWidth; }

uint8_t getNeighborCount( uint8_t x, uint8_t y ) {
  uint8_t count = 0;
  // Packed contiguously as live neighbors are found -- indexing by direction
  // (0-7) left slots for absent neighbors uninitialized, and a later read at
  // random(0, count) could land on one of those instead of a real parent.
  uint16_t parents[8];

  // Check cell above.
  if ( allCells[ x ][ above(y) ].currentColor ) {
    parents[count++] = allCells[ x ][ above(y) ].hVal;
  }

  // Check cell upper right.
  if ( allCells[ right(x) ][ above(y) ].currentColor ) {
    parents[count++] = allCells[ right(x) ][ above(y) ].hVal;
  }

  // Check cell on right.
  if ( allCells[ right(x) ][ y ].currentColor ) {
    parents[count++] = allCells[ right(x) ][ y ].hVal;
  }

  // Check cell lower right.
  if ( allCells[ right(x) ][ below(y) ].currentColor ) {
    parents[count++] = allCells[ right(x) ][ below(y) ].hVal;
  }

  // Check cell below.
  if ( allCells[ x ][ below(y) ].currentColor ) {
    parents[count++] = allCells[ x ][ below(y) ].hVal;
  }

  // Check cell lower left.
  if ( allCells[ left(x) ][ below(y) ].currentColor ) {
    parents[count++] = allCells[ left(x) ][ below(y) ].hVal;
  }

  // Check cell on left.
  if ( allCells[ left(x) ][ y ].currentColor ) {
    parents[count++] = allCells[ left(x) ][ y ].hVal;
  }

  // Check cell upper left.
  if ( allCells[ left(x) ][ above(y) ].currentColor ) {
    parents[count++] = allCells[ left(x) ][ above(y) ].hVal;
  }

  // If this cell has chance to be born, calculate it's color based on blend of two "parents".
  if ( count > 0 ) {
    // random(min, max) excludes max, so the upper bound must be count, not
    // count - 1 -- the old bound could never select the last parent.
    byte parentIndex = random(0, count);
    uint8_t geneIndex = random(0, 2);
    byte geneDirection = random(1, 100);
    uint16_t tempHVal = 0;

    if ( geneDirection % 2) { // Even or odd
      tempHVal = fmod(parents[parentIndex] + geneIndex, 360);
    } else {
      tempHVal = fmod(parents[parentIndex] - geneIndex, 360);
    }

    allCells[x][y].hVal = tempHVal;
  }

  return count;
}

void fireStarter() {
  if ( firePaused ) {
    oneColor(0);
    return;
  }

  if ( (currentTime - globalLastTime) < fireSpeed ) {
    return;
  }

  if ( ! fireInitialized ) {
    // Set buffers to 0.
    for (uint8_t y = 0; y < maxHeight; y++) {
      for (uint8_t x = 0; x < maxWidth; x++) {
        fireBuffer[x][y] = 0;
      }
    }
    fireInitialized = true;
  }

  if (1 == specialFire) {
    globalLastTime = currentTime;
    if ( (currentTime - hslLastTime ) > hslInterval ) {
      hslLastTime = currentTime;
      fireHueShift++;
      if (fireHueShift == 360) {
        fireHueShift = 0;
      }
    }
  }

  // Generate palette.
  for (uint16_t x = 0; x < 256; x++) {
    firePalette[x] = hsl2rgb((x / 3.4) + fireHueShift, 100, min(50, x / 4));
  }


  // Fill bottom row with random palette values.
  for (uint8_t x = 0; x < maxWidth; x++) {
    if (! random(0, 5)) {
      fireBuffer[x][maxHeight - 1] = random(0, 255);
    }
  }

  // Fill the buffer with a palette color (0-255).
  for (uint8_t y = 0; y < maxHeight - 1; y++) {
    for (uint8_t x = 0; x < maxWidth; x++) {
      fireBuffer[x][y] = min( 255, round( (
                                            fireBuffer[(x - 1 + maxWidth) % maxWidth][(y + 1) % maxHeight]
                                            + fireBuffer[(x) % maxWidth][(y + 1) % maxHeight]
                                            + fireBuffer[(x + 1) % maxWidth][(y + 1) % maxHeight]
                                            + fireBuffer[(x) % maxWidth][(y + 2) % maxHeight]
                                            * 16 ) / 22
                                        ) );
    }
  }

  // Set the LEDs based on the buffer and palette.
  for (uint8_t y = 0; y < maxHeight; y++) {
    for (uint8_t x = 0; x < maxWidth; x++) {
      setPixelSafe(remapXY(x, y), dimColor( firePalette[fireBuffer[x][y]], random(0, 8), 1));
    }
  }
  leds.show();

  globalLastTime = currentTime;
}

/**
   Re-latch the frame already in the draw buffer, for modes that only redraw when
   their data changes.

   Nothing is recalculated here -- leds.show() just retransmits what is already
   there. The point is that the LEDs are on a separate supply, so cutting their
   power loses the frame while the sketch carries on none the wiser. Calling this
   from the idle path of a static mode means the wall recovers on its own, and
   keeps doing so when no updates are arriving at all: overnight, at weekends, or
   any time after the market closes and prices stop changing.
*/
void refreshStaticFrame() {
  if ( (currentTime - staticRefreshTime) >= staticRefreshInterval ) {
    staticRefreshTime = currentTime;
    leds.show();
  }
}

// Draw one pixel in chart space, letting the lookup tables handle the struts.
inline void setChartPixel(uint8_t cx, uint8_t cy, uint32_t color) {
  if (cx >= chartWidth || cy >= chartHeight) return;
  setPixelSafe(remapXY(chartCol[cx], chartRow[cy]), color);
}

// Scale a color for the fade-in after a new frame, and for the stale dimming.
inline uint32_t stockScale(uint32_t color, uint8_t brightness) {
  return makeColor(red(color), green(color), blue(color), white(color), brightness);
}

/**
   Blit a 4-character band, one glyph per panel.

   Each glyph is drawn twice: first a 1px dark halo around every lit pixel, then
   the pixel itself. Without the halo the text would sit directly on the chart
   fill and turn to mush wherever the series is dense -- the knockout guarantees
   legibility no matter what the price did.
*/
void drawStockText(const char * text, uint8_t topRow, uint8_t brightness) {
  uint32_t ink = stockScale(makeColor(0, 0, 0, stockTextWhite), brightness);

  for (uint8_t pass = 0; pass < 2; pass++) {
    for (uint8_t slot = 0; slot < 4; slot++) {
      if (text[slot] == 0) break;
      uint8_t glyph = glyphIndex(text[slot]);
      uint8_t left = slot * 8 + 1; // Centered in the panel, 1px margin.

      for (uint8_t row = 0; row < GLYPH_HEIGHT; row++) {
        uint8_t bits = font5x7[glyph][row];
        for (uint8_t column = 0; column < GLYPH_WIDTH; column++) {
          if (! (bits >> (GLYPH_WIDTH - 1 - column) & 1)) continue;

          if (pass == 0) {
            // Knockout: clear the 3x3 neighborhood.
            for (int8_t dy = -1; dy <= 1; dy++) {
              for (int8_t dx = -1; dx <= 1; dx++) {
                setChartPixel(left + column + dx, topRow + row + dy, 0);
              }
            }
          } else {
            setChartPixel(left + column, topRow + row, ink);
          }
        }
      }
    }
  }
}

/**
   Stock position: a 32 day sparkline with the ticker and current price.

   The server hands us finished chart rows, so there is no price arithmetic
   here -- we only draw. Layout is the chart across all 32 rows, with the ticker
   overlaid on the top panel row and the price on the bottom.

   Reading the chart: the baseline is where the price sat 32 trading days ago,
   and the filled area between it and the line shows the move since, green above
   and red below.

   This is a still image. It repaints only when the data actually changes, so an
   update is silent -- no fade, no pulse, nothing that draws the eye. An earlier
   version eased each new frame in from black, which on a one minute refresh read
   as the whole wall flashing.
*/
void stockChart() {
  // Nothing to draw until the first frame arrives, so idle like mode 0.
  if ( ! stockReceived ) {
    oneColor(0x00000010);
    return;
  }

  // Already on screen and unchanged: leave the pixels alone, but keep pushing
  // the existing frame out so the wall survives the LEDs losing power.
  if ( ! stockDirty ) {
    refreshStaticFrame();
    return;
  }
  stockDirty = 0;

  uint8_t brightness = (stockFlags & 1)
                       ? stockBrightness / stockStaleDivisor
                       : stockBrightness;

  uint32_t gainFill = stockScale(
                        hsl2rgb(stockGainHue, stockFillSat, stockFillLight), brightness);
  uint32_t lossFill = stockScale(
                        hsl2rgb(stockLossHue, stockFillSat, stockFillLight), brightness);
  uint32_t gainLine = stockScale(
                        hsl2rgb(stockGainHue, stockGainLineSat, stockGainLineLight), brightness);
  uint32_t lossLine = stockScale(
                        hsl2rgb(stockLossHue, stockLossLineSat, stockLossLineLight), brightness);
  uint32_t baseInk  = stockScale(makeColor(0, 0, 0, stockBaselineWhite), brightness);

  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    leds.setPixel(i, 0);
  }

  for (uint8_t x = 0; x < chartWidth; x++) {
    uint8_t y = stockSeries[x];
    boolean up = (y <= stockBaseline); // Lower row number is higher price.

    // Area between the line and the baseline.
    uint8_t from = min(y, stockBaseline);
    uint8_t to = max(y, stockBaseline);
    for (uint8_t fill = from; fill <= to; fill++) {
      setChartPixel(x, fill, up ? gainFill : lossFill);
    }

    // The baseline itself, drawn over the fill so it stays readable.
    setChartPixel(x, stockBaseline, baseInk);

    // Line, interpolated from the previous day so steep moves stay connected
    // instead of breaking into disconnected dots.
    uint8_t previous = (x == 0) ? y : stockSeries[x - 1];
    uint32_t line = up ? gainLine : lossLine;
    for (uint8_t step = min(y, previous); step <= max(y, previous); step++) {
      setChartPixel(x, step, line);
    }
  }

  drawStockText(stockTicker, stockTickerRow, brightness);
  drawStockText(stockPrice, stockPriceRow, brightness);

  leds.show();
}

/**
   Sprite mode: one 8x8 sprite per panel, 16 in all.

   This is the reason the panel geometry is worth exploiting rather than working
   around. Every other mode treats the struts as damage to be routed around; here
   each panel is exactly one sprite and the strut becomes a frame around it.

   Chart space maps straight onto it -- panel column times 8 plus the sprite's own
   x -- so the existing lookup tables do all the strut arithmetic.
*/
void spriteShow() {
  // Nothing to draw until the first frame arrives, so idle like mode 0.
  if ( ! spritesReceived ) {
    oneColor(0x00000010);
    return;
  }

  // Still image: leave the pixels alone unless something changed, but keep
  // pushing the existing frame out in case the LEDs lost power.
  if ( ! spritesDirty ) {
    refreshStaticFrame();
    return;
  }
  spritesDirty = 0;

  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    leds.setPixel(i, 0);
  }

  for (uint8_t panel = 0; panel < 16; panel++) {
    const uint8_t * bitmap = spriteData[spriteChoice[panel]];
    uint8_t originX = (panel % 4) * SPRITE_SIZE;
    uint8_t originY = (panel / 4) * SPRITE_SIZE;

    for (uint8_t y = 0; y < SPRITE_SIZE; y++) {
      for (uint8_t x = 0; x < SPRITE_SIZE; x++) {
        uint8_t index = bitmap[y * SPRITE_SIZE + x];
        if (index == 0) continue; // Unlit.

        const uint8_t * rgb = spritePalette[index - 1];
        setChartPixel(originX + x, originY + y,
                      makeColor(rgb[0], rgb[1], rgb[2], 0, spriteBrightness));
      }
    }
  }

  leds.show();
}

void displayUserSelectedMode() {
  switch (userMode) {
    case 0: // None, dim white.
      oneColor(0x00000010);
      break;

    case 1: // RGBW.
      doRGBW();
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

    case 6: // Pause fire.
      fireStarter();
      break;

    case 7: // HSL.
      if (currentTime - globalLastTime >= fadeInterval) {
        globalLastTime = currentTime;
        oneColor( makeColor( rVal, gVal, bVal, wVal ), makeColor( rVal2, gVal2, bVal2, wVal2 ));
      }
      break;

    case 8: // Special HSL.
      doSpecialHSL();
      break;

    case 9: // Special Fire.
      fireStarter();
      break;

    case 10: // Conway's Game of Life.
      lifeStart();
      break;

    case 11: // Pause life.
      lifeStart();
      break;

    case 12: // Stock chart.
      stockChart();
      break;

    case 14: // Sprites, one per panel.
      spriteShow();
      break;

    default:
      oneColor(0x00000010);
      break;
  }
}
