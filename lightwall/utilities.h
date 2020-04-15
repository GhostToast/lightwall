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

uint32_t hsvRgb(byte h, byte s, byte v) {
  //h = (h*192)/256; //0...191
  unsigned int i = h/32; // Want value of 0 through 5.
  unsigned int f = (h%32)*8; // Fractional part of i 0...248 in jumps

  unsigned int sInv = 255 - s; // 0 -> 0xff, 0xff -> 0
  unsigned int fInv = 255 - f;
  byte pv = v*sInv/256; // pv will be in range 0-255.
  byte qv = v*(256 - s * f / 256) / 256;
  byte tv = v*(256 - s * fInv / 256) / 256;

  switch (i) {
    case 0:
    return ((uint32_t)v << 24) | ((uint32_t)tv << 16) | ((uint32_t)pv <<  8) | 0;

    case 1:
    return ((uint32_t)qv << 24) | ((uint32_t)v << 16) | ((uint32_t)pv <<  8) | 0;

    case 2:
    return ((uint32_t)pv << 24) | ((uint32_t)v << 16) | ((uint32_t)tv <<  8) | 0;

    case 3:
    return ((uint32_t)pv << 24) | ((uint32_t)qv << 16) | ((uint32_t)v <<  8) | 0;

    case 4:
    return ((uint32_t)tv << 24) | ((uint32_t)pv << 16) | ((uint32_t)v <<  8) | 0;

    case 5:
    return ((uint32_t)v << 24) | ((uint32_t)pv << 16) | ((uint32_t)qv <<  8) | 0;

    default:
    return ((uint32_t)v << 24) | ((uint32_t)tv << 16) | ((uint32_t)pv <<  8) | 0;
  }
}
