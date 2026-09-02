#include <OctoSK6812.h>
#include <Entropy.h>
#include "rainColumn.h"
#include "cell.h"
#include "firefly.h"
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
byte lifeColorMutation = 0;           // 0-100: widens how far a new cell's hue can drift from its "parent"'s -- see getNeighborCount().
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

// A minority of deaths leave a faint ember behind instead of going straight
// to black -- a cell that stays barely lit while the colony moves on around
// it. The ember persists indefinitely (no decay) until a birth lands on that
// same cell and overtakes it -- see the isBirth blend in lifeStart()'s fade
// loop. Deliberately stores no per-cell state (see cell.h's zero-padding-
// slack comment: one more byte per cell would cost 6.2KB across 1558 cells)
// -- see lifeCellEmbers() below for how membership is decided without a
// stored flag; the glow itself is carried entirely in the LED framebuffer,
// which nothing touches again for that pixel until that cell is reborn.
byte lifeEmberChance            = 25;  // 0-100: percent of deaths that leave a persistent ember behind; UI-adjustable, see processLifeTiming().
const uint8_t lifeEmberLevel    = 15;  // 0-255 brightness an ember holds at when its death fade lands -- kept low for contrast against live cells, especially at low L.
uint16_t lifeGeneration = 0;            // Bumped once per commit; mixed into lifeCellEmbers() so membership varies generation to generation.
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
byte specialMatrix = 0;
uint16_t matrixHueShift = 0;
const uint8_t matrixRainbowChannelSpread = 32;

// --- Fireflies ------------------------------------------------------------
// A blank field where individual points ignite, hold briefly, and fade out,
// then relight somewhere adjacent. Every number below is a starting value to
// be tuned against the real wall; the derivations are spelled out so tuning
// one does not silently invalidate another.

// 48 fireflies over 1024 visible pixels. Density is set by duty cycle, not by
// this count: expected simultaneous lit points is
//   FIREFLY_COUNT * fireflyOnMs / (fireflyOnMs + sleepAvg)
// which at the defaults below is about 5 -- sparse and occasional. The count
// only caps how busy the top of the Frequency slider can get (~25 lit at 100).
#define FIREFLY_COUNT 48
firefly allFireflies[FIREFLY_COUNT];

// Phases. Strictly sequential: DARK -> FADE_IN -> HOLD -> FADE_OUT -> DARK.
#define FIREFLY_DARK     0
#define FIREFLY_FADE_IN  1
#define FIREFLY_HOLD     2
#define FIREFLY_FADE_OUT 3

byte firefliesInitialized = 0;
byte firefliesPaused = 0;

uint16_t fireflyHue = 60;             // 0-359, the color selector. Hue only, like
                                       // Fire -- the ask was about the color wheel.
const uint8_t fireflySat = 100;       // Fixed, as Fire fixes s=100.
// Peak lightness, 0-100 into hsl2rgb, which scales all three channels
// proportionally below 50 -- so this is a clean brightness knob for the hue.
// 30, not 50: the field is otherwise pure black, the panels are punishing at
// close range, and this codebase's tuning has consistently gone dimmer for
// contrast (lifeEmberLevel is 15/255). Tune in the 15-50 band; promote to a
// slider later if it wants one.
const uint8_t fireflyPeakLight = 30;
// Per-blink ceiling jitter, 0-255 applied on top of fireflyPeakLight, so some
// fireflies read nearer and some further. The single cheapest realism win here.
const uint8_t fireflyPeakMin = 160;   // ~63% of peak.

uint16_t fireflyFadeMs = 700;         // Each of fade-in and fade-out. The eases
                                       // are already asymmetric (quadratic bloom
                                       // in, cubic crawl out), so one slider
                                       // yields a natural rise-and-linger.
const uint16_t fireflyMinFade = 100;
const uint16_t fireflyMaxFade = 2500;
uint16_t fireflyHoldMs = 300;         // Full-brightness dwell; 0 is legal and
                                       // gives a pure pulse.
const uint16_t fireflyMinHold = 0;
const uint16_t fireflyMaxHold = 3000;
byte fireflyFrequency = 40;           // 0-100, how often a firefly reignites.
byte fireflyHueVariation = 40;        // 0-100, mapped to +/- degrees below.

// Per-blink HOLD jitter, +/- this percent of fireflyHoldMs, rerolled at every
// ignition (see rollFireflyHold()). Without it every firefly glows for
// exactly the same length at a fixed slider setting -- only peak brightness
// and hue varied blink to blink. Clamped to [fireflyMinHold, fireflyMaxHold]
// same as the slider itself, so the result always stays within the range the
// slider promises, just spread out across it instead of pinned to one value.
const uint8_t fireflyHoldJitterPercent = 50;

// Slider-to-internal mapping ceilings.
// Unlike Life's drift, this one is measured from the base hue every blink and
// never compounds, so it can afford to range wider than Life's effective
// ceiling (maxMutationDegrees x mutationSliderCeilingPercent = ~+/-18.75)
// without the runaway-color risk that caps Life. 45 degrees at 100 is chosen
// so the top of the Variation slider reads as clearly varied hue-to-hue
// rather than a near-miss of the base color.
const uint8_t fireflyMaxHueDrift = 45;
// Sleep window endpoints, in ms, at Frequency 0 and 100. fireflySleepMax is
// capped so that sleepAvg * 3/2 (the jitter ceiling in recomputeFireflyTiming)
// stays inside uint16_t.
const uint16_t fireflySleepMax = 40000;
const uint16_t fireflySleepMin = 1500;
const uint16_t fireflySleepFloor = 250;  // Always leave some darkness between
                                          // blinks, or it stops reading as a blink.

// The "gentle hover/drift" itself: at most ONE nudge per HOLD cycle, and only
// fireflyHoverChancePercent of cycles get even that -- most blinks are simply
// still. A per-tick chance or a fixed short interval both multiply into a
// blur once fireflyHoldMs is long or many fireflies are lit at once (high
// Frequency); capping it at one occasional, rare step keeps the movement
// pleasant and peaceful rather than a constant tremor across the field.
const uint8_t fireflyHoverChancePercent = 35;

// The brightness "waver": a brief dim-and-recover dip partway through HOLD,
// independent of and unrelated to the hover step above (one dims, one moves)
// and also unrelated to the pre-existing zero-hold "pure pulse" mentioned at
// fireflyHoldMs -- that is a whole blink with no dwell at all, this is a
// momentary sag inside a blink that is otherwise dwelling normally. Same
// at-most-once-per-cycle, chance-gated shape as the hover, for the same
// reason: a per-tick chance multiplies into a flicker rather than a read as
// one deliberate dip.
//
// Gated by fireflyWaverMinHoldMs because the dip needs room to be smooth --
// scheduled in a window and given a duration both scaled off f.holdMs (see
// fireflyStart()), so a short blink physically cannot fit one; below this
// floor it is not offered at all rather than being crushed into an
// indistinguishable flicker.
const uint8_t fireflyWaverChancePercent = 30;
const uint16_t fireflyWaverMinHoldMs = 700;
// Dip depth, 0-255 subtracted from the 255 HOLD ceiling at the deepest point
// of the dip (before f.peak scales it down further like any other level).
// 90 reads as a clear, deliberate sag without ever dropping low enough to
// look like the blink is ending early.
const uint8_t fireflyWaverDepth = 90;

// Derived by recomputeFireflyTiming(), same contract as recomputeLifeTiming().
uint16_t fireflyOnMs = 1700;          // 2 * fade + hold.
uint16_t fireflySleepLow = 6573;      // random(low, high) bounds for one cycle.
uint16_t fireflySleepHigh = 19719;
uint8_t  fireflyHueDrift = 9;         // Degrees, +/-.

const uint8_t fireflyFrameInterval = 16;  // ~60fps, matching lifeFadeInterval.
// Its own clock, for the reason spelled out above lifeFadeLastTime: sharing
// globalLastTime across modes left stale timestamps on mode switch.
unsigned long fireflyLastTime = 0;

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
// The GitHub contribution grid is the longest command -- 224 grid characters
// plus "<github," and ",F,NNN>" overhead is 239 bytes worst case -- so this
// has to clear it with room to spare. Every other command, stock included, is
// well under 60.
const uint16_t buffSize = 260;
char inputBuffer[buffSize];
const char startMarker = '<';
const char endMarker = '>';
uint16_t bytesRecvd = 0;
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

// GitHub contribution calendar. One column per week, oldest left, this week
// right, exactly like github.com's own grid. Each day is a 1-wide x 4-tall
// block of chart rows rather than a single pixel, so the squares read as
// chunky and GitHub-esque instead of a thin sparkline.
const uint8_t githubWeeks = 32;
const uint8_t githubDays = 7;
const uint8_t githubDayBlock = 4;                 // Rows per day-square.
const uint8_t githubTopMargin = 2;                // (32 - 7*4) / 2.
const uint16_t githubCells = (uint16_t)githubWeeks * githubDays; // 224
uint8_t githubGrid[githubCells];                  // Flat, index = week*7+day.
byte githubReceived = 0;
byte githubDirty = 0;
uint8_t githubFlags = 0;
uint8_t githubBrightness = 155;                   // Same starting value as stockBrightness.
uint32_t githubPalette[5];                         // hsl2rgb(120,100,L), computed once in setup().
// Lightness per contribution level 0 (none) .. 4 (max) -- dim like every
// other close-range mode; starting values, easy to retune here.
const uint8_t githubLevelLight[5] = {2, 8, 16, 26, 40};

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
  for (uint8_t i = 0; i < 5; i++) {
    githubPalette[i] = hsl2rgb(120, 100, githubLevelLight[i]);
  }
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
  } else if (strcmp(strtokIndex, "fireflies") == 0) {
    userMode = 13;
    processFireflies(strtokIndex);
  } else if (strcmp(strtokIndex, "firefliespause") == 0) {
    userMode = 15;
    processFirefliesPause(strtokIndex);
  } else if (strcmp(strtokIndex, "firefliestiming") == 0) {
    // Deliberately does not touch userMode -- see processFirefliesTiming().
    processFirefliesTiming(strtokIndex);
  } else if (strcmp(strtokIndex, "specialmatrix") == 0) {
    userMode = 16;
    processSpecialMatrix(strtokIndex);
  } else if (strcmp(strtokIndex, "github") == 0) {
    userMode = 17;
    processGithub(strtokIndex);
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
    Serial.print(",");
    Serial.print(lifeColorMutation);
    Serial.print(",");
    Serial.print(lifeEmberChance);
    Serial.println(">");
  } else if (11 == userMode) {
    Serial.print("<lifepause,");
    Serial.print(lifePaused);
    Serial.print(",");
    Serial.print(lifeSpeed);
    Serial.print(",");
    Serial.print(lifeOrganic);
    Serial.print(",");
    Serial.print(lifeColorMutation);
    Serial.print(",");
    Serial.print(lifeEmberChance);
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
  } else if (13 == userMode) {
    Serial.print("<fireflies,");
    Serial.print(fireflyHue);
    Serial.print(",");
    Serial.print(fireflyFadeMs);
    Serial.print(",");
    Serial.print(fireflyHoldMs);
    Serial.print(",");
    Serial.print(fireflyFrequency);
    Serial.print(",");
    Serial.print(fireflyHueVariation);
    Serial.println(">");
  } else if (15 == userMode) {
    // No color here, exactly as <lifepause,...> omits h/s/l -- the server
    // backfills the hue from its own defaults. The four timing fields still
    // have to come back so the sliders restore while paused.
    Serial.print("<firefliespause,");
    Serial.print(firefliesPaused);
    Serial.print(",");
    Serial.print(fireflyFadeMs);
    Serial.print(",");
    Serial.print(fireflyHoldMs);
    Serial.print(",");
    Serial.print(fireflyFrequency);
    Serial.print(",");
    Serial.print(fireflyHueVariation);
    Serial.println(">");
  } else if (16 == userMode) {
    Serial.print("<specialmatrix,");
    Serial.print(specialMatrix);
    Serial.println(">");
  } else if (17 == userMode) {
    Serial.println("<github>");
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
  specialMatrix = 0;
}

void processMatrix(char * strtokIndex) {
  matrixPaused = 0;
  specialMatrix = 0;
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

  strtokIndex = strtok(NULL, ",");
  int requestedColorMutation = atoi(strtokIndex);
  if (requestedColorMutation < 0) requestedColorMutation = 0;
  if (requestedColorMutation > 100) requestedColorMutation = 100;
  lifeColorMutation = requestedColorMutation;

  strtokIndex = strtok(NULL, ",");
  int requestedEmberChance = atoi(strtokIndex);
  if (requestedEmberChance < 0) requestedEmberChance = 0;
  if (requestedEmberChance > 100) requestedEmberChance = 100;
  lifeEmberChance = requestedEmberChance;

  recomputeLifeTiming();
}

/**
   <fireflies,H> -- the color selector, hue only (mirrors <fire,H>).

   Deliberately does not clear firefliesInitialized: this arrives on every
   release of the hue slider, and reinitializing would blank the field and
   restart all FIREFLY_COUNT clocks on each drag. Fireflies already lit keep
   the hue they ignited with and finish their blink in it; new ignitions use
   the new hue, so the wall migrates to the new color over a second or two
   instead of snapping. Unlike Life, there is no lifeNewColor equivalent, and
   none is wanted -- the migration is the pleasant behavior here.
*/
void processFireflies(char * strtokIndex) {
  strtokIndex = strtok(NULL, ",");
  uint16_t requested = atoi(strtokIndex);
  fireflyHue = ( requested > 359 ) ? ( requested % 360 ) : requested;
  firefliesPaused = 0;
}

void processFirefliesPause(char * strtokIndex) {
  strtokIndex = strtok(NULL, ",");
  firefliesPaused = atoi(strtokIndex);
  if ( firefliesPaused ) {
    firefliesInitialized = 0;
  }
}

/**
   <firefliestiming,FADE,HOLD,FREQ,VAR> -- glow shape and density only. Like
   <lifetiming,...>, this deliberately does not touch userMode, so dragging a
   slider is safe while paused or while the wall is in another mode, and does
   not touch firefliesInitialized, so it does not restart the field.
*/
void processFirefliesTiming(char * strtokIndex) {
  strtokIndex = strtok(NULL, ",");
  int requestedFade = atoi(strtokIndex);
  if ( requestedFade < fireflyMinFade ) requestedFade = fireflyMinFade;
  if ( requestedFade > fireflyMaxFade ) requestedFade = fireflyMaxFade;
  fireflyFadeMs = requestedFade;

  strtokIndex = strtok(NULL, ",");
  int requestedHold = atoi(strtokIndex);
  if ( requestedHold < fireflyMinHold ) requestedHold = fireflyMinHold;
  if ( requestedHold > fireflyMaxHold ) requestedHold = fireflyMaxHold;
  fireflyHoldMs = requestedHold;

  strtokIndex = strtok(NULL, ",");
  int requestedFrequency = atoi(strtokIndex);
  if ( requestedFrequency < 0 )   requestedFrequency = 0;
  if ( requestedFrequency > 100 ) requestedFrequency = 100;
  fireflyFrequency = requestedFrequency;

  strtokIndex = strtok(NULL, ",");
  int requestedVariation = atoi(strtokIndex);
  if ( requestedVariation < 0 )   requestedVariation = 0;
  if ( requestedVariation > 100 ) requestedVariation = 100;
  fireflyHueVariation = requestedVariation;

  uint16_t oldSleepLow = fireflySleepLow;
  uint16_t oldSleepHigh = fireflySleepHigh;
  recomputeFireflyTiming();

  // Without this, a firefly already sitting in the dark phase keeps waiting
  // out the sleepMs it rolled under the *old* frequency -- which at a low
  // frequency can be tens of seconds -- so dragging the slider would look
  // like it did nothing until each firefly happened to cycle through on its
  // own. But resetting every dark firefly's clock to the same instant (an
  // earlier version of this did exactly that) resynchronizes the whole dark
  // population onto one shared countdown -- it reads as the whole field
  // switching on and off together, "upon changing any timing slider," rather
  // than independently. Scaling each firefly's own remaining target by how
  // much the average sleep window just changed makes the new frequency felt
  // immediately while leaving each firefly's own elapsed progress (and so its
  // stagger relative to every other firefly) untouched. When only fade/hold/
  // variation changed, oldMid == newMid and this is a no-op.
  uint32_t oldMid = (uint32_t) oldSleepLow + oldSleepHigh;
  uint32_t newMid = (uint32_t) fireflySleepLow + fireflySleepHigh;
  if ( oldMid == 0 ) oldMid = 1;
  // uint64_t, not uint32_t: sleepMs and newMid can each be tens of thousands,
  // and their product can exceed 4.29 billion (uint32_t's ceiling) at the low
  // end of the Frequency range.
  for ( uint8_t i = 0; i < FIREFLY_COUNT; i++ ) {
    if ( FIREFLY_DARK == allFireflies[i].phase ) {
      allFireflies[i].sleepMs = (uint16_t)( ( (uint64_t) allFireflies[i].sleepMs * newMid ) / oldMid );
    }
  }
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

/**
   Decode a GitHub contribution calendar frame:

     <github,LLLLLLLL...L (224 chars),F,NNN>

   L  224 contribution levels, one character per day ('0'-'4', GitHub's own
      quartile bucketing), flat index = week*7+day, oldest week first
   F  flag bitfield; bit 0 means the data is stale
   N  overall brightness, 5-255 as decimal

   Single-digit-per-day encoding is what keeps 32 weeks of history inside one
   atomic frame instead of a chunked protocol with ordering state.
*/
void processGithub(char * strtokIndex) {
  strtokIndex = strtok(NULL, ",");
  if (strtokIndex == NULL || strlen(strtokIndex) < githubCells) {
    return; // Truncated frame; keep showing whatever we had.
  }
  for (uint16_t i = 0; i < githubCells; i++) {
    uint8_t level = strtokIndex[i] - '0';
    githubGrid[i] = (level <= 4) ? level : 0;
  }

  strtokIndex = strtok(NULL, ",");
  githubFlags = (strtokIndex == NULL) ? 0 : atoi(strtokIndex);

  strtokIndex = strtok(NULL, ",");
  if (strtokIndex != NULL) {
    int requested = atoi(strtokIndex);
    if (requested < 5) requested = 5;
    if (requested > 255) requested = 255;
    githubBrightness = requested;
  }

  githubReceived = 1;
  githubDirty = 1;
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

void processSpecialMatrix(char * strtokIndex) {
  strtokIndex = strtok(NULL, ",");
  specialMatrix = atoi(strtokIndex);
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

  if ( specialMatrix ) {
    if ( (currentTime - hslLastTime) > hslInterval ) {
      hslLastTime = currentTime;
      matrixHueShift++;
      if (matrixHueShift == 360) matrixHueShift = 0;
    }
    uint32_t rainbowColor = hsl2rgb(matrixHueShift, 100, 50);
    uint8_t rMax = red(rainbowColor), gMax = green(rainbowColor), bMax = blue(rainbowColor);
    matrixColors[0][0] = (rMax > matrixRainbowChannelSpread) ? rMax - matrixRainbowChannelSpread : 0;
    matrixColors[0][1] = rMax;
    matrixColors[1][0] = (gMax > matrixRainbowChannelSpread) ? gMax - matrixRainbowChannelSpread : 0;
    matrixColors[1][1] = gMax;
    matrixColors[2][0] = (bMax > matrixRainbowChannelSpread) ? bMax - matrixRainbowChannelSpread : 0;
    matrixColors[2][1] = bMax;
    matrixColors[3][0] = 0;
    matrixColors[3][1] = 0;
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

// Mirrors recomputeLifeTiming(): called from processFirefliesTiming() and from
// fireflyStart()'s init block, so nothing downstream has to recompute.
//
// Frequency maps to a *sleep window*, not to a target duty cycle: fade/hold
// decide how long a blink lasts, frequency decides how often one starts, and
// the two compose without either redefining the other.
//
// The taper is quadratic in (100 - frequency) rather than linear in
// milliseconds. Linear-in-ms puts almost all of the perceptible change in the
// top third of the slider (halving the sleep doubles the rate, and the last 10
// points of the slider would carry a 14x rate change); the quadratic tracks a
// geometric sweep closely enough to feel even under the hand, with one integer
// expression and no table. Worst-case intermediate is 38500 * 100 * 100, well
// inside uint32_t.
void recomputeFireflyTiming() {
  fireflyOnMs = (uint16_t)( 2 * (uint32_t) fireflyFadeMs + fireflyHoldMs );

  uint32_t inv = 100 - fireflyFrequency;
  uint16_t sleepAvg = fireflySleepMin +
    (uint16_t)( ( (uint32_t)( fireflySleepMax - fireflySleepMin ) * inv * inv ) / 10000 );

  // +/-50% jitter per cycle, so no two fireflies settle into lockstep and the
  // same slider position never produces a metronome.
  fireflySleepLow  = sleepAvg / 2;
  fireflySleepHigh = sleepAvg + sleepAvg / 2;
  if ( fireflySleepLow < fireflySleepFloor ) fireflySleepLow = fireflySleepFloor;
  // random(min, max) returns min when max <= min, which would freeze the jitter.
  if ( fireflySleepHigh <= fireflySleepLow ) fireflySleepHigh = fireflySleepLow + 1;

  fireflyHueDrift = (uint8_t)( ( (uint32_t) fireflyHueVariation * fireflyMaxHueDrift ) / 100 );
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
    // instead holds at lifeEmberLevel indefinitely, until a birth overtakes
    // this pixel (see the isBirth blend in the fade loop below).
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
          // Cell continues living if 2 or 3 neighbors. Keep same color --
          // mutation happens at birth instead, see getNeighborCount()'s
          // parent-hue blend below.
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
          // A cell reborn on a cell whose previous death left a persistent
          // ember still glowing would otherwise have this bloom snap
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
    uint16_t tempHVal = 0;

    // lifeColorMutation widens how far a new cell's hue can wander from its
    // chosen parent's -- 1 degree at 0 (the original subtle drift, kept
    // byte-identical) up to a capped ceiling at 100. Same gene-blend
    // mechanism throughout, just a wider dice roll, rather than swapping to
    // an unrelated "big leap" mode -- that read as pure random noise instead
    // of a heritable trait. The slider's full 0-100 range only spends
    // mutationSliderCeilingPercent of maxMutationDegrees's total range --
    // past that point in testing read as too wild, so 100 on the slider now
    // lands where ~75 used to.
    const uint8_t maxMutationDegrees = 25;
    const uint8_t mutationSliderCeilingPercent = 75;
    uint8_t maxGeneIndex = 1 + (uint8_t)( (uint32_t) lifeColorMutation * (maxMutationDegrees - 1) * mutationSliderCeilingPercent / 10000 );
    uint8_t geneIndex = random(0, maxGeneIndex + 1);
    byte geneDirection = random(1, 100);

    if ( geneDirection % 2) { // Even or odd
      tempHVal = (parents[parentIndex] + geneIndex) % 360;
    } else {
      // +360 before subtracting keeps this unsigned-safe when geneIndex
      // exceeds the parent's hue -- the old fmod() version could go negative
      // there and wrap to a huge uint16_t instead of a small hue.
      tempHVal = (parents[parentIndex] + 360 - geneIndex) % 360;
    }

    allCells[x][y].hVal = tempHVal;
  }

  return count;
}

// Chart-space neighbors. Not above()/below()/left()/right() -- those wrap mod
// maxHeight/maxWidth and would walk a firefly into a strut band, where it would
// be invisible for a whole blink. See firefly.h.
uint8_t fireflyStep( uint8_t v ) {
  return random(0, 2) ? ( ( v + 1 ) % 32 ) : ( ( v + 31 ) % 32 );
}

// This blink's hue: the selected hue plus a subtle, non-cumulative offset.
uint16_t rollFireflyHue() {
  if ( 0 == fireflyHueDrift ) return fireflyHue;
  int16_t h = (int16_t) fireflyHue + (int16_t) random( -(int16_t) fireflyHueDrift,
                                                       (int16_t) fireflyHueDrift + 1 );
  if ( h < 0 )    h += 360;
  if ( h > 359 )  h -= 360;
  return (uint16_t) h;
}

// This blink's HOLD duration: fireflyHoldMs +/- fireflyHoldJitterPercent,
// clamped back into the slider's own [min, max] so a jittered value can never
// read as "the slider promised something it didn't deliver." A hold of 0 is
// the legal "pure pulse" case (see fireflyHoldMs) and has no jitter to add.
uint16_t rollFireflyHold() {
  if ( 0 == fireflyHoldMs ) return 0;
  int32_t jitter = (int32_t)( (uint32_t) fireflyHoldMs * fireflyHoldJitterPercent / 100 );
  int32_t h = (int32_t) fireflyHoldMs + (int32_t) random( -jitter, jitter + 1 );
  if ( h < (int32_t) fireflyMinHold ) h = fireflyMinHold;
  if ( h > (int32_t) fireflyMaxHold ) h = fireflyMaxHold;
  return (uint16_t) h;
}

// A blank field where individual points ignite, hold briefly, and fade out,
// then relight somewhere adjacent. Position lives in the gap-free 32x32 chart
// space (chartCol[]/chartRow[]), not the 38x41 logical grid -- see firefly.h.
// Fireflies never read the framebuffer (no ember-style overtake logic here),
// so a firefly only ever repositions while fully dark: the pixel it last used
// was written black when its fade-out completed, so there is no trail to clean
// up and no previous-pixel bookkeeping needed.
void fireflyStart() {
  if ( firefliesPaused ) {
    oneColor(0);
    return;
  }

  if ( ! firefliesInitialized ) {
    oneColor(0);
    recomputeFireflyTiming();
    for ( uint8_t i = 0; i < FIREFLY_COUNT; i++ ) {
      firefly &f = allFireflies[i];
      f.x = random(0, 32);
      f.y = random(0, 32);
      f.phase = FIREFLY_DARK;
      f.phaseStart = currentTime;
      // First cycle only: draw the dwell from [0, high) rather than
      // [low, high), so the field wakes up gradually over one full sleep
      // window instead of a third of the wall igniting at once at t=low.
      f.sleepMs = random(0, fireflySleepHigh);
      f.hue = fireflyHue;
      f.peak = 255;
    }
    firefliesInitialized = 1;
  }

  if ( currentTime - fireflyLastTime < fireflyFrameInterval ) {
    return;
  }
  fireflyLastTime = currentTime;

  // Pass 1: advance phases. Any firefly whose fade-out just landed writes its
  // own pixel black here, before pass 2 draws anything -- two fireflies can
  // share a pixel, and a single advance-and-draw loop would let a finishing
  // firefly erase a still-lit one for a frame. Blacks first, draws second, so
  // that ordering cannot happen.
  for ( uint8_t i = 0; i < FIREFLY_COUNT; i++ ) {
    firefly &f = allFireflies[i];
    unsigned long elapsed = currentTime - f.phaseStart;

    switch ( f.phase ) {
      case FIREFLY_DARK:
        if ( elapsed >= f.sleepMs ) {
          // Ignition owns the position, hue, and brightness reroll. It is
          // safe to move here precisely because the pixel this firefly last
          // used was written black when its fade-out completed, and nothing
          // in this mode reads the framebuffer -- so there is no trail to
          // clean up and no previous-pixel bookkeeping at all.
          f.x = fireflyStep(f.x);
          f.y = fireflyStep(f.y);
          f.hue = rollFireflyHue();
          f.peak = random(fireflyPeakMin, 256);
          f.holdMs = rollFireflyHold();
          f.phase = FIREFLY_FADE_IN;
          f.phaseStart = currentTime;
        }
        break;

      case FIREFLY_FADE_IN:
        if ( elapsed >= fireflyFadeMs ) {
          f.phase = FIREFLY_HOLD;
          f.phaseStart = currentTime;
          // At most one hover this cycle, and only fireflyHoverChancePercent
          // of cycles get even that -- scheduled somewhere in the middle 40%
          // of the hold (not right at ignition, not right before fade-out).
          // Missing the roll (or holdMs being 0, a legal pure-pulse) sets
          // nextHoverTime past the end of the hold, which is the same as
          // never, since the elapsed >= f.holdMs check above always fires
          // first.
          if ( f.holdMs > 0 && random(1, 101) <= fireflyHoverChancePercent ) {
            uint16_t hoverWindowStart = (uint16_t)( (uint32_t) f.holdMs * 3 / 10 );
            uint16_t hoverWindowEnd   = (uint16_t)( (uint32_t) f.holdMs * 7 / 10 );
            if ( hoverWindowEnd <= hoverWindowStart ) hoverWindowEnd = hoverWindowStart + 1;
            f.nextHoverTime = currentTime + random(hoverWindowStart, hoverWindowEnd);
          } else {
            f.nextHoverTime = currentTime + f.holdMs + 1;
          }
          // The waver: same shape as the hover roll above, but gated by
          // fireflyWaverMinHoldMs first -- a hold too short to give the dip
          // room to read as deliberate doesn't get offered one at all.
          // Duration is rolled *before* the window on purpose: unlike the
          // hover, which is instantaneous, the waver takes time to play out,
          // so the window's late edge is holdMs - duration, not a fixed
          // fraction -- otherwise a dip starting near 9/10 of a long hold
          // could still be mid-recovery when elapsed >= f.holdMs above cuts
          // over to FADE_OUT, clipping the return-to-full half short.
          if ( f.holdMs >= fireflyWaverMinHoldMs && random(1, 101) <= fireflyWaverChancePercent ) {
            f.waverDurationMs = (uint16_t)( (uint32_t) f.holdMs * random(15, 31) / 100 );
            uint16_t waverWindowStart = (uint16_t)( (uint32_t) f.holdMs * 1 / 10 );
            uint16_t waverWindowEnd   = f.holdMs - f.waverDurationMs;
            if ( waverWindowEnd <= waverWindowStart ) waverWindowEnd = waverWindowStart + 1;
            f.nextWaverTime = currentTime + random(waverWindowStart, waverWindowEnd);
          } else {
            f.nextWaverTime = currentTime + f.holdMs + 1;
            f.waverDurationMs = 0;
          }
        }
        break;

      case FIREFLY_HOLD:
        if ( elapsed >= f.holdMs ) {
          f.phase = FIREFLY_FADE_OUT;
          f.phaseStart = currentTime;
        } else if ( currentTime >= f.nextHoverTime ) {
          // The gentle hover: nudge position by one step while still lit.
          // Blank the old spot explicitly first -- unlike the DARK->FADE_IN
          // reposition, this one moves a pixel that is genuinely lit right
          // now, so nothing else will ever clear it if we don't. Safe against
          // another firefly sharing this pixel: pass 2 below redraws every
          // still-lit firefly unconditionally from its current position, so
          // a blank written here can never outlive this frame.
          setPixelSafe( remapXY( chartCol[f.x], chartRow[f.y] ), 0 );
          f.x = fireflyStep(f.x);
          f.y = fireflyStep(f.y);
          // Consumed: push past the end of this hold so it cannot fire again
          // this cycle. The next FADE_IN->HOLD transition rolls a fresh one.
          f.nextHoverTime = currentTime + f.holdMs + 1;
        }
        break;

      case FIREFLY_FADE_OUT:
        if ( elapsed >= fireflyFadeMs ) {
          // Explicitly write the resting value rather than trusting the last
          // fade frame to have reached 0 -- the same lesson as
          // finalizeRainColumn() and Life's finalize block. Integer rounding
          // or a frame skipped by a stall would otherwise leave a dim ghost
          // lit forever, since nothing else ever touches this pixel.
          setPixelSafe( remapXY( chartCol[f.x], chartRow[f.y] ), 0 );
          f.sleepMs = random( fireflySleepLow, fireflySleepHigh );
          f.phase = FIREFLY_DARK;
          f.phaseStart = currentTime;
        }
        break;
    }
  }

  // Pass 2: draw everything currently lit.
  for ( uint8_t i = 0; i < FIREFLY_COUNT; i++ ) {
    firefly &f = allFireflies[i];
    if ( FIREFLY_DARK == f.phase ) continue;

    uint8_t level;
    if ( FIREFLY_HOLD == f.phase ) {
      level = 255;
      // The waver, if one was rolled for this hold (waverDurationMs > 0) and
      // its window has arrived: a triangular dip, zero at both edges and
      // deepest at the midpoint, so it reads as one smooth sag-and-recover
      // rather than a hard step down and back. Folded in before the f.peak
      // scale below, same as the fade-in/fade-out eases are.
      if ( f.waverDurationMs > 0 && currentTime >= f.nextWaverTime ) {
        unsigned long waverElapsed = currentTime - f.nextWaverTime;
        if ( waverElapsed < f.waverDurationMs ) {
          uint8_t wt = (uint8_t)( ( (uint32_t) waverElapsed * 255 ) / f.waverDurationMs );
          uint8_t depthNow = ( wt < 128 ) ? (uint8_t)( wt * 2 ) : (uint8_t)( ( 255 - wt ) * 2 );
          uint8_t dip = (uint8_t)( ( (uint16_t) depthNow * fireflyWaverDepth ) / 255 );
          level = (uint8_t)( 255 - dip );
        }
      }
    } else {
      // Same elapsed -> t -> ease idiom as Life's fade loop (see the
      // elapsedMs/cellSpanMs/t block there). Span floored at the frame
      // interval so a fade can never divide by zero.
      uint16_t span = ( fireflyFadeMs > fireflyFrameInterval ) ? fireflyFadeMs
                                                                : fireflyFrameInterval;
      uint16_t progress = (uint16_t)( currentTime - f.phaseStart );
      if ( progress > span ) progress = span;
      uint8_t t = (uint8_t)( ( (uint32_t) progress * 255 ) / span );
      level = ( FIREFLY_FADE_IN == f.phase ) ? easeLifeBirth(t) : easeLifeDeath(t);
    }

    // Fold this blink's own ceiling into the eased level, then scale the hue.
    level = (uint8_t)( ( (uint16_t) level * f.peak ) / 255 );

    uint32_t base = hsl2rgb( f.hue, fireflySat, fireflyPeakLight );
    uint8_t r = (uint8_t)( ( (uint16_t) red(base)   * level ) / 255 );
    uint8_t g = (uint8_t)( ( (uint16_t) green(base) * level ) / 255 );
    uint8_t b = (uint8_t)( ( (uint16_t) blue(base)  * level ) / 255 );

    setPixelSafe( remapXY( chartCol[f.x], chartRow[f.y] ), makeColor( r, g, b ) );
  }

  // Inside the frame gate, unlike lifeStart()/makeItRain(), which show() on
  // every loop() pass. A show() is a ~5ms blocking DMA transfer for 1024 RGBW
  // pixels; there is nothing to send when no firefly has moved.
  leds.show();
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

/**
   GitHub contribution calendar: a grid of squares, darker for quiet days and
   brighter green for busy ones, exactly like github.com's own profile page.

   The server already bucketed each day into one of GitHub's own 0-4 levels,
   so there is no arithmetic here -- we only draw. One column per week, oldest
   left, this week right; each day is a 1-wide x githubDayBlock-tall block
   rather than a single pixel, so the squares read as chunky and GitHub-esque
   instead of a thin sparkline.

   Like Stock and Sprites, this is a still image. It repaints only when the
   data actually changes.
*/
void githubShow() {
  if ( ! githubReceived ) {
    oneColor(0x00000010);
    return;
  }
  if ( ! githubDirty ) {
    refreshStaticFrame();
    return;
  }
  githubDirty = 0;

  uint8_t brightness = (githubFlags & 1)
                       ? githubBrightness / stockStaleDivisor
                       : githubBrightness;

  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    leds.setPixel(i, 0);
  }

  for (uint8_t week = 0; week < githubWeeks; week++) {
    for (uint8_t day = 0; day < githubDays; day++) {
      uint32_t color = stockScale(githubPalette[githubGrid[week * githubDays + day]], brightness);
      uint8_t top = githubTopMargin + day * githubDayBlock;
      for (uint8_t sub = 0; sub < githubDayBlock; sub++) {
        setChartPixel(week, top + sub, color);
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

    case 13: // Fireflies.
      fireflyStart();
      break;

    case 15: // Pause fireflies.
      fireflyStart();
      break;

    case 16: // Special matrix (rainbow).
      makeItRain();
      break;

    case 17: // GitHub contribution calendar.
      githubShow();
      break;

    default:
      oneColor(0x00000010);
      break;
  }
}
