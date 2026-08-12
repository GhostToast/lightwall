// Cell Struct. Two uint32_t plus two uint16_t is exactly 12 bytes with no
// padding (4-byte aligned already lands on a multiple of 4), so fadeDelay --
// the per-cell fade start stagger, in milliseconds -- costs nothing across all
// 1558 cells. uint16_t rather than uint8_t because it holds a millisecond
// offset (up to roughly lifeSpeed's range) rather than a small step count.
struct cell {
  uint32_t currentColor = 0;
  uint32_t nextColor = 0;
  uint16_t hVal = 0;
  uint16_t fadeDelay = 0;
};
