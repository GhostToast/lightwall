// Ornament Struct.
struct ornament {
  byte panel;                  // which grid panel does this ornament belong to.
  uint32_t color;              // the color to initialize at.
  uint16_t interval;           // how long to wait between animation frames.
  unsigned long lastUpdated;   // how long ago this column animated a frame.
};

