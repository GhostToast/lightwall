// Rain Column Struct.
struct rainColumn {
  byte column;                 // which column this represents on the X axis.
  byte head;                   // which pixel is being processed as the head.
  byte headBrightness;         // how bright to make the head (white amount).
  byte height;                 // how many pixels tall this streamer should be.
  byte isRunning;              // whether this animation is currently playing.
  byte canGoBlack;             // whether this column will fade to black or stay faintly the dominant color.
  byte dimAmount;              // amount to diminish tail each tick.
  char dominantColor;          // the dominant color.
  uint32_t color;              // the color to initialize at.
  uint16_t interval;           // how long to wait between animation frames.
  uint16_t sleepTime;          // how long to wait before re-animating.
  unsigned long lastUpdated;   // how long ago this column animated a frame.
  unsigned long lastCompleted; // how long ago this column completed full animation.
};

