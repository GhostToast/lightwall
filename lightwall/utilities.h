#include "math.h"

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

uint32_t hsi2rgbw(float H, float S, float I) {
  int r, g, b, w;
  float cos_h, cos_1047_h;
  H = fmod(H,360); // cycle H around to 0-360 degrees
  H = 3.14159*H/(float)180; // Convert to radians.
  S = S>0?(S<1?S:1):0; // clamp S and I to interval [0,1]
  I = I>0?(I<1?I:1):0;
  
  if(H < 2.09439) {
    cos_h = cos(H);
    cos_1047_h = cos(1.047196667-H);
    r = S*255*I/3*(1+cos_h/cos_1047_h);
    g = S*255*I/3*(1+(1-cos_h/cos_1047_h));
    b = 0;
    w = 255*(1-S)*I;
  } else if(H < 4.188787) {
    H = H - 2.09439;
    cos_h = cos(H);
    cos_1047_h = cos(1.047196667-H);
    g = S*255*I/3*(1+cos_h/cos_1047_h);
    b = S*255*I/3*(1+(1-cos_h/cos_1047_h));
    r = 0;
    w = 255*(1-S)*I;
  } else {
    H = H - 4.188787;
    cos_h = cos(H);
    cos_1047_h = cos(1.047196667-H);
    b = S*255*I/3*(1+cos_h/cos_1047_h);
    r = S*255*I/3*(1+(1-cos_h/cos_1047_h));
    g = 0;
    w = 255*(1-S)*I;
  }

  return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b <<  8) | w;
}
