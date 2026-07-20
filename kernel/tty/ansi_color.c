#include "ansi.h"
#include "tty.h"
#include <graphic.h>

static uint32_t ansi_colors[8] = {
    0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
    0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA
};

static uint32_t xterm_256_to_rgb(uint8_t color) {
    if (color < 8) return ansi_colors[color];
    if (color < 16) return ansi_colors[color - 8] | 0x555555;
    
    if (color >= 16 && color <= 231) {
        color -= 16;
        uint32_t b = (color % 6);
        color /= 6;
        uint32_t g = (color % 6);
        uint32_t r = (color / 6);
        
        r = r ? (r * 40 + 55) : 0;
        g = g ? (g * 40 + 55) : 0;
        b = b ? (b * 40 + 55) : 0;
        
        return (r << 16) | (g << 8) | b;
    }
    
    if (color >= 232) {
        uint32_t gray = (color - 232) * 10 + 8;
        return (gray << 16) | (gray << 8) | gray;
    }
    
    return 0xFFFFFF;
}

void ansi_handle_color(struct tty_internal* priv) {
    for (int i = 0; i <= priv->ansi_param_count; i++) {
        int p = priv->ansi_params[i];
        
        if (p == 38 && i + 2 <= priv->ansi_param_count && priv->ansi_params[i+1] == 5) {
            priv->fg_color = xterm_256_to_rgb((uint8_t)priv->ansi_params[i+2]);
            i += 2;
            continue;
        }
        if (p == 48 && i + 2 <= priv->ansi_param_count && priv->ansi_params[i+1] == 5) {
            priv->bg_color = xterm_256_to_rgb((uint8_t)priv->ansi_params[i+2]);
            i += 2;
            continue;
        }
        if (p == 38 && i + 4 <= priv->ansi_param_count && priv->ansi_params[i+1] == 2) {
            priv->fg_color = ((uint32_t)priv->ansi_params[i+2] << 16) | ((uint32_t)priv->ansi_params[i+3] << 8) | (uint32_t)priv->ansi_params[i+4];
            i += 4;
            continue;
        }
        if (p == 48 && i + 4 <= priv->ansi_param_count && priv->ansi_params[i+1] == 2) {
            priv->bg_color = ((uint32_t)priv->ansi_params[i+2] << 16) | ((uint32_t)priv->ansi_params[i+3] << 8) | (uint32_t)priv->ansi_params[i+4];
            i += 4;
            continue;
        }
        
        if (p == 0) {
            priv->fg_color = 0xFFFFFF;
            priv->bg_color = 0x000000;
            priv->_pad[TTY_INVERSE_FLAG_IDX] = 0;
        } else if (p >= 30 && p <= 37) {
            priv->fg_color = ansi_colors[p - 30];
        } else if (p >= 40 && p <= 47) {
            priv->bg_color = ansi_colors[p - 40];
        } else if (p == 39) {
            priv->fg_color = 0xFFFFFF;
        } else if (p == 49) {
            priv->bg_color = 0x000000;
        } else if (p >= 90 && p <= 97) {
            priv->fg_color = ansi_colors[p - 90] | 0x555555;
        } else if (p >= 100 && p <= 107) {
            priv->bg_color = ansi_colors[p - 100] | 0x555555;
        } else if (p == 7) { // Inverse
            priv->_pad[TTY_INVERSE_FLAG_IDX] = 1;
        } else if (p == 27) { // Inverse off
            priv->_pad[TTY_INVERSE_FLAG_IDX] = 0;
        }
    }
}
