// Cell Struct. Sized to stay inside 12 bytes (10 packs, 4-byte aligned rounds
// up to 12 either way) so fadeDelay -- the per-cell fade start stagger -- costs
// nothing across all 1558 cells.
struct cell {
  uint32_t currentColor = 0;
  uint32_t nextColor = 0;
  uint16_t hVal = 0;
  uint8_t fadeDelay = 0;
};
