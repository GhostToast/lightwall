// Firefly Struct. One blinking point of light with its own dark dwell, its own
// hue, and its own brightness ceiling.
//
// Position is stored in the gap-free 32x32 chart space, not the 38x41 logical
// grid, and mapped through chartCol[]/chartRow[] at draw time -- a third of the
// logical grid is strut (see remapXY), and unlike Life, which deliberately
// simulates behind the struts, a firefly there is simply a blink that never
// happens. With only FIREFLY_COUNT objects on the wall that loss is visible as
// irregular density rather than as a texture.
//
// One timestamp, not rainColumn's lastUpdated/lastCompleted pair: a rain column
// runs a per-object frame timer and a completion timer simultaneously, whereas a
// firefly's four phases are strictly sequential (only ever one timer live) and
// the frame cadence is global (fireflyLastTime). phaseStart is therefore the
// whole clock.
struct firefly {
  uint8_t  x;                // 0-31, column in chart space (see chartCol[]).
  uint8_t  y;                // 0-31, row in chart space (see chartRow[]).
  uint8_t  phase;            // FIREFLY_DARK / _FADE_IN / _HOLD / _FADE_OUT.
  uint8_t  peak;             // 0-255 ceiling for this blink; rerolled per ignite
                             // so not every firefly is equally bright.
  uint16_t hue;              // 0-359, this blink's hue: fireflyHue +/- drift.
  uint16_t sleepMs;          // dark dwell rolled for this cycle.
  unsigned long phaseStart;  // millis() when the current phase began.
};
