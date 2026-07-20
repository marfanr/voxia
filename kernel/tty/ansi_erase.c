#include "ansi.h"
#include "tty.h"
#include <graphic.h>
#include "str.h"

void ansi_handle_erase(struct tty_internal* priv, uint8_t c, int p0) {
    if (c == 'J') {
        if (p0 == 2 || p0 == 3) {
            /*
             * Erase entire display. In alt-screen mode (smcup active) we
             * suppress the actual clear so ncurses' redundant stdscr
             * background redraw does NOT erase the window content it just
             * drew.  We still reset the cursor so ncurses cursor tracking
             * stays correct.
             */
            tty_clear_area_nolock(priv, 0, 0, (int)priv->cols, (int)priv->rows);
            priv->cursorx = 0;
            priv->cursory = 0;
            return;
        }
        if (p0 == 0) { // Clear to end of screen
            tty_clear_area_nolock(priv, (int)priv->cursorx, (int)priv->cursory, (int)priv->cols - (int)priv->cursorx, 1);
            int remaining_rows = (int)priv->rows - (int)priv->cursory - 1;
            if (remaining_rows > 0)
                tty_clear_area_nolock(priv, 0, (int)priv->cursory + 1, (int)priv->cols, remaining_rows);
        } else if (p0 == 1) { // Clear to beginning of screen
            tty_clear_area_nolock(priv, 0, (int)priv->cursory, (int)priv->cursorx + 1, 1);
            if (priv->cursory > 0)
                tty_clear_area_nolock(priv, 0, 0, (int)priv->cols, (int)priv->cursory);
        }
    } else if (c == 'K') {
        if (p0 == 0) {
            tty_clear_area_nolock(priv, (int)priv->cursorx, (int)priv->cursory, (int)priv->cols - (int)priv->cursorx, 1);
        } else if (p0 == 1) {
            tty_clear_area_nolock(priv, 0, (int)priv->cursory, (int)priv->cursorx + 1, 1);
        } else if (p0 == 2) {
            tty_clear_area_nolock(priv, 0, (int)priv->cursory, (int)priv->cols, 1);
        }
    }
}
