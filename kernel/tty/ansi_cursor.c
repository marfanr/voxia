#include "ansi.h"
#include <graphic.h>
#include "libk/serial.h"
#include "tty.h"

void ansi_handle_cursor(struct tty_internal* priv, uint8_t c, int p0, int p1) {
	if (c == 'H' || c == 'f') {
		priv->cursory = p0 > 0 ? (uint32_t)(p0 - 1) : 0;
		priv->cursorx = p1 > 0 ? (uint32_t)(p1 - 1) : 0;
		if (priv->cursory >= priv->rows)
			priv->cursory = priv->rows - 1;
		if (priv->cursorx >= priv->cols)
			priv->cursorx = priv->cols - 1;
	} else if (c == 'A') {
		p0 = p0 == 0 ? 1 : p0;
		priv->cursory = (priv->cursory >= (uint32_t)p0)
		                    ? priv->cursory - (uint32_t)p0
		                    : 0;
	} else if (c == 'B') {
		p0 = p0 == 0 ? 1 : p0;
		priv->cursory = (priv->cursory + (uint32_t)p0 < priv->rows)
		                    ? priv->cursory + (uint32_t)p0
		                    : priv->rows - 1;
	} else if (c == 'C') {
		p0 = p0 == 0 ? 1 : p0;
		priv->cursorx = (priv->cursorx + (uint32_t)p0 < priv->cols)
		                    ? priv->cursorx + (uint32_t)p0
		                    : priv->cols - 1;
	} else if (c == 'D') {
		p0 = p0 == 0 ? 1 : p0;
		priv->cursorx = (priv->cursorx >= (uint32_t)p0)
		                    ? priv->cursorx - (uint32_t)p0
		                    : 0;
	} else if (c == 'E') { // Next line
		p0 = p0 == 0 ? 1 : p0;
		priv->cursory = (priv->cursory + (uint32_t)p0 < priv->rows)
		                    ? priv->cursory + (uint32_t)p0
		                    : priv->rows - 1;
		priv->cursorx = 0;
	} else if (c == 'F') { // Previous line
		p0 = p0 == 0 ? 1 : p0;
		priv->cursory = (priv->cursory >= (uint32_t)p0)
		                    ? priv->cursory - (uint32_t)p0
		                    : 0;
		priv->cursorx = 0;
	} else if (c == 'G' || c == '`') { // Absolute column
		p0 = p0 == 0 ? 1 : p0;
		priv->cursorx = (uint32_t)(p0 > 0 ? p0 - 1 : 0);
		if (priv->cursorx >= priv->cols)
			priv->cursorx = priv->cols - 1;
	} else if (c == 'd') { // Absolute row
		p0 = p0 == 0 ? 1 : p0;
		priv->cursory = (uint32_t)(p0 > 0 ? p0 - 1 : 0);
		if (priv->cursory >= priv->rows)
			priv->cursory = priv->rows - 1;
	} else if (c == 's') { // Save cursor
		priv->_pad[0] = priv->cursorx & 0xFF;
		priv->_pad[1] = (priv->cursorx >> 8) & 0xFF;
		priv->_pad[2] = priv->cursory & 0xFF;
		priv->_pad[3] = (priv->cursory >> 8) & 0xFF;
	} else if (c == 'u') { // Restore cursor
		priv->cursorx =
		    (uint32_t)priv->_pad[0] | ((uint32_t)priv->_pad[1] << 8);
		priv->cursory =
		    (uint32_t)priv->_pad[2] | ((uint32_t)priv->_pad[3] << 8);
		if (priv->cursorx >= priv->cols)
			priv->cursorx = priv->cols - 1;
		if (priv->cursory >= priv->rows)
			priv->cursory = priv->rows - 1;
	}
}
