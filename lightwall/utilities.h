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

unsigned int h2rgb(unsigned int v1, unsigned int v2, unsigned int hue)
{
  if (hue < 60) return v1 * 60 + (v2 - v1) * hue;
  if (hue < 180) return v2 * 60;
  if (hue < 240) return v1 * 60 + (v2 - v1) * (240 - hue);
  return v1 * 60;
}

// Convert HSL (Hue, Saturation, Lightness) to RGB (Red, Green, Blue)
//
//   hue:        0 to 359 - position on the color wheel, 0=red, 60=orange,
//                            120=yellow, 180=green, 240=blue, 300=violet
//
//   saturation: 0 to 100 - how bright or dull the color, 100=full, 0=gray
//
//   lightness:  0 to 100 - how light the color is, 100=white, 50=color, 0=black
//
uint32_t hsl2rgb(unsigned int hue, unsigned int saturation, unsigned int lightness) {
  unsigned int red, green, blue;
  unsigned int var1, var2;

  if (hue > 359) hue = hue % 360;
  if (saturation > 100) saturation = 100;
  if (lightness > 100) lightness = 100;

  // algorithm from: http://www.easyrgb.com/index.php?X=MATH&H=19#text19
  if (saturation == 0) {
    red = green = blue = lightness * 255 / 100;
  } else {
    if (lightness < 50) {
      var2 = lightness * (100 + saturation);
    } else {
      var2 = ((lightness + saturation) * 100) - (saturation * lightness);
    }
    var1 = lightness * 200 - var2;
    red = h2rgb(var1, var2, (hue < 240) ? hue + 120 : hue - 240) * 255 / 600000;
    green = h2rgb(var1, var2, hue) * 255 / 600000;
    blue = h2rgb(var1, var2, (hue >= 120) ? hue - 120 : hue + 240) * 255 / 600000;
  }
  
  return ((uint32_t)red << 24) | ((uint32_t)green << 16) | ((uint32_t)blue <<  8) | 0;
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
