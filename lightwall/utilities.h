// Returns red component or RGB color.
uint8_t red(uint32_t c) {
  return (c >> 16);
}

// Returns green component or RGB color.
uint8_t green(uint32_t c) {
  return (c >> 8);
}

// Returns blue component or RGB color.
uint8_t blue(uint32_t c) {
  return (c);
}

