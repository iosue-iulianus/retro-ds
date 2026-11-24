/* The UI fonts, embedded (tools/embed_fonts.py). */
#ifndef RETRODS_FONTS_DATA_H
#define RETRODS_FONTS_DATA_H

#include <stddef.h>

enum {
	EMBEDDED_BARLOW_BOLD, EMBEDDED_BARLOW_MEDIUM,
	EMBEDDED_NOTO_SC_BOLD, EMBEDDED_NOTO_SC_REGULAR, EMBEDDED_NOTO_TC_BOLD, EMBEDDED_NOTO_TC_REGULAR,
	EMBEDDED_NOTO_JP_BOLD, EMBEDDED_NOTO_JP_REGULAR,
	EMBEDDED_NOTO_KR_BOLD, EMBEDDED_NOTO_KR_REGULAR,
	EMBEDDED_FONT_COUNT
};

typedef struct {
	const unsigned char* ttf;
	size_t size;
} EmbeddedFont;

extern const EmbeddedFont embeddedFonts[EMBEDDED_FONT_COUNT];

#endif
