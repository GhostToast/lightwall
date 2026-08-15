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
// Three timestamps/durations beyond phaseStart, not rainColumn's
// lastUpdated/lastCompleted pair: phaseStart is the strictly-sequential
// DARK->FADE_IN->HOLD->FADE_OUT clock (only ever one phase live), while
// nextHoverTime and nextWaverTime are independent sub-clocks that only
// matter during HOLD -- see fireflyStart()'s hover step, which moves the
// firefly a little *while lit* so the drift is actually visible rather than
// an invisible reposition between blinks, and its waver step, which dips
// brightness down and back for the same reason. The two are unrelated (one
// moves a pixel, one dims it) and may coincide in the same frame.
struct firefly {
  uint8_t  x;                // 0-31, column in chart space (see chartCol[]).
  uint8_t  y;                // 0-31, row in chart space (see chartRow[]).
  uint8_t  phase;            // FIREFLY_DARK / _FADE_IN / _HOLD / _FADE_OUT.
  uint8_t  peak;             // 0-255 ceiling for this blink; rerolled per ignite
                             // so not every firefly is equally bright.
  uint16_t hue;              // 0-359, this blink's hue: fireflyHue +/- drift.
  uint16_t sleepMs;          // dark dwell rolled for this cycle.
  uint16_t holdMs;           // this blink's HOLD duration: fireflyHoldMs jittered
                             // per rollFireflyHold(), so not every glow lasts the
                             // same length even at a fixed slider setting.
  unsigned long phaseStart;  // millis() when the current phase began.
  unsigned long nextHoverTime; // millis() deadline for the next in-place hover step, while HOLD.
  unsigned long nextWaverTime; // millis() deadline for the next dim-and-recover dip, while HOLD.
  uint16_t waverDurationMs;    // length of that dip; 0 if none was rolled this cycle.
};
