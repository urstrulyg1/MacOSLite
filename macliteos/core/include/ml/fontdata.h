#ifndef ML_FONTDATA_H
#define ML_FONTDATA_H
#include "ml/common.h"
typedef struct { uint32_t cp; uint16_t adv; const char *d; } ml_glyph_def;
extern const ml_glyph_def ml_glyph_table[];
extern const size_t ml_glyph_table_n;
#endif
