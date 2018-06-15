// Rain Column Struct.
struct rainColumn {
  byte column;     // which column this represents on the X axis.
  byte height;     // how many pixels tall this streamer should be.
  uint32_t color1; 
  uint32_t color2;
  byte isRunning; // whether this animation is currently playing.
  byte interval; // how long to wait between animation frames.
  uint32_t sleepTime; // how long to wait before re-animating.
  unsigned long lastUpdated; // how long ago this column animated a frame.
  unsigned long lastCompleted; // how long ago this column completed full animation.
};

