#include "ansi.h"
#include "tty.h"
#include <graphic.h>
#include "str.h"

// implementation refer https://vt100.net/docs/vt100-ug/chapter3.html

#define ANSI_STATE_NORMAL 0
#define ANSI_STATE_ESC 1
#define ANSI_STATE_CSI 2
#define ANSI_STATE_CHARSET 3

/* Flag stored in _pad[6] to mark private CSI sequence (had '?') */
#define ANSI_PRIVATE_FLAG_IDX 6

int ansi_process_char(struct tty_internal* priv, uint8_t c) {
    if (priv->ansi_state == ANSI_STATE_NORMAL) {
        if (c == '\033') {
            priv->ansi_state = ANSI_STATE_ESC;
            return 1;
        }
        return 0;
    }
    
    if (priv->ansi_state == ANSI_STATE_ESC) {
        if (c == '[') {
            priv->ansi_state = ANSI_STATE_CSI;
            priv->ansi_param_count = 0;
            priv->ansi_params[0] = 0;
        } else if (c == '(' || c == ')') {
            priv->ansi_state = ANSI_STATE_CHARSET;
        } else if (c == '7') {
            priv->_pad[0] = priv->cursorx & 0xFF;
            priv->_pad[1] = (priv->cursorx >> 8) & 0xFF;
            priv->_pad[2] = priv->cursory & 0xFF;
            priv->_pad[3] = (priv->cursory >> 8) & 0xFF;
            priv->ansi_state = ANSI_STATE_NORMAL;
        } else if (c == '8') {
            priv->cursorx = (uint32_t)priv->_pad[0] | ((uint32_t)priv->_pad[1] << 8);
            priv->cursory = (uint32_t)priv->_pad[2] | ((uint32_t)priv->_pad[3] << 8);
            if (priv->cursorx >= priv->cols) priv->cursorx = priv->cols - 1;
            if (priv->cursory >= priv->rows) priv->cursory = priv->rows - 1;
            priv->ansi_state = ANSI_STATE_NORMAL;
        } else if (c == 'M') { // Reverse Index (scroll down if at top of scrolling region)
            if (priv->cursory == priv->scroll_top) {
                do_scroll_down_nolock(priv);
            } else if (priv->cursory > 0) {
                priv->cursory--;
            }
            priv->ansi_state = ANSI_STATE_NORMAL;
        } else if (c == 'D') { // Index (scroll up if at bottom of scrolling region)
            if (priv->cursory == priv->scroll_bottom) {
                do_scroll_nolock(priv);
            } else if (priv->cursory + 1 < priv->rows) {
                priv->cursory++;
            }
            priv->ansi_state = ANSI_STATE_NORMAL;
        } else {
            priv->ansi_state = ANSI_STATE_NORMAL;
        }
        return 1;
    }
    
    if (priv->ansi_state == ANSI_STATE_CHARSET) {
        if (c == '0') {
            priv->_pad[4] = 1; // Enable ACS
        } else if (c == 'B') {
            priv->_pad[4] = 0; // Disable ACS (US-ASCII)
        }
        priv->ansi_state = ANSI_STATE_NORMAL;
        return 1;
    }
    
    if (priv->ansi_state == ANSI_STATE_CSI) {
        if (c >= '0' && c <= '9') {
            priv->ansi_params[priv->ansi_param_count] = priv->ansi_params[priv->ansi_param_count] * 10 + (c - '0');
        } else if (c == '?') {
            /* Mark this as a private DEC sequence */
            priv->_pad[ANSI_PRIVATE_FLAG_IDX] = 1;
        } else if (c == ';') {
            if (priv->ansi_param_count < 15) {
                priv->ansi_param_count++;
                priv->ansi_params[priv->ansi_param_count] = 0;
            }
        } else {
            priv->ansi_state = ANSI_STATE_NORMAL;
            int p0 = priv->ansi_params[0];
            int p1 = priv->ansi_param_count >= 1 ? priv->ansi_params[1] : 0;
            int is_private = priv->_pad[ANSI_PRIVATE_FLAG_IDX];
            priv->_pad[ANSI_PRIVATE_FLAG_IDX] = 0; /* reset private flag */
            
            /* Handle DEC private sequences: ?1049h/l (alternate screen) */
            if (is_private && c == 'h' && p0 == 1049) {
                if (!priv->alt_screen) {
                    /* Enter alternate screen: clear screen, reset cursor */
                    priv->alt_screen = true;
                    if (priv->cells && priv->alt_cells) {
                        memcopy(priv->alt_cells, priv->cells, priv->cols * priv->rows * sizeof(struct tty_cell));
                        priv->alt_cursorx = priv->cursorx;
                        priv->alt_cursory = priv->cursory;
                    }
                    priv->scroll_top = 0;
                    priv->scroll_bottom = priv->rows - 1;
                    tty_clear_area_nolock(priv, 0, 0, (int)priv->cols, (int)priv->rows);
                    priv->cursorx = 0;
                    priv->cursory = 0;
                }
            } else if (is_private && c == 'l' && p0 == 1049) {
                if (priv->alt_screen) {
                    /* Exit alternate screen: restore screen and return to normal */
                    priv->alt_screen = false;
                    priv->scroll_top = 0;
                    priv->scroll_bottom = priv->rows - 1;
                    priv->bg_color = 0x000000;
                    priv->fg_color = 0xFFFFFF;
                    if (priv->cells && priv->alt_cells) {
                        memcopy(priv->cells, priv->alt_cells, priv->cols * priv->rows * sizeof(struct tty_cell));
                        priv->cursorx = priv->alt_cursorx;
                        priv->cursory = priv->alt_cursory;
                        tty_redraw_screen_nolock(priv);
                    } else {
                        tty_clear_area_nolock(priv, 0, 0, (int)priv->cols, (int)priv->rows);
                        priv->cursorx = 0;
                        priv->cursory = 0;
                    }
                    priv->_pad[0] = 0;
                    priv->_pad[1] = 0;
                    priv->_pad[2] = 0;
                    priv->_pad[3] = 0;
                    priv->_pad[TTY_INVERSE_FLAG_IDX] = 0;
                }
            } else if (c == 'r') {
                int top = p0 == 0 ? 1 : p0;
                int bottom = p1 == 0 ? (int)priv->rows : p1;
                if (top > 0 && top <= (int)priv->rows && bottom > 0 && bottom <= (int)priv->rows && top <= bottom) {
                    priv->scroll_top = (uint32_t)(top - 1);
                    priv->scroll_bottom = (uint32_t)(bottom - 1);
                }
                priv->cursorx = 0;
                priv->cursory = 0;
            } else if (c == 'S') {
                int count = p0 == 0 ? 1 : p0;
                for (int i = 0; i < count; i++) {
                    do_scroll_nolock(priv);
                }
            } else if (c == 'T') {
                int count = p0 == 0 ? 1 : p0;
                for (int i = 0; i < count; i++) {
                    do_scroll_down_nolock(priv);
                }
            } else if (c == 'm') {
                ansi_handle_color(priv);
            } else if (c == 'J' || c == 'K') {
                ansi_handle_erase(priv, c, p0);
            } else if (c == 'b') {
                /* CSI Pn b -> Repeat last printed character Pn times */
                int count = p0 == 0 ? 1 : p0;
                int len = (int)strlen(priv->last_char);
                if (len > 0) {
                    for (int i = 0; i < count; i++) {
                        tty_putchar_raw_nolock(priv, priv->last_char, len);
                    }
                }
            } else {
                ansi_handle_cursor(priv, c, p0, p1);
            }
        }
        return 1;
    }
    
    return 0;
}
