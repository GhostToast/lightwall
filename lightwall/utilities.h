// Make a 32-bit RGBW color.
uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w=0, uint8_t brightness = 0) {
  if (brightness > 0) {
    r = (r * brightness ) >> 8;
    g = (g * brightness ) >> 8;
    b = (b * brightness ) >> 8;
    w = (w * brightness ) >> 8;
  }
  
  return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b <<  8) | w;
}

// Returns red component of 32-bit RGBW color.
uint8_t red(uint32_t c) {
  return (c >> 24);
}

// Returns green component of 32-bit RGBW color.
uint8_t green(uint32_t c) {
  return (c >> 16);
}

// Returns blue component of 32-bit RGBW color.
uint8_t blue(uint32_t c) {
  return (c >> 8 );
}

// Returns white component of 32-bit RGBW color.
uint8_t white(uint32_t c) {
  return (c);
}
