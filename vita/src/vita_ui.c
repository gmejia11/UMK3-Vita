#include <stdlib.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include "ui_alert.h"

int ui_font_ok(void) {
    return 0;
}

int ui_font_height(int px, int bold) {
    (void)px;
    (void)bold;
    return 0;
}

int ui_font_width(const unsigned short *s, int n, int px, int bold) {
    (void)s;
    (void)n;
    (void)px;
    (void)bold;
    return 0;
}

unsigned char *ui_font_line(const unsigned short *s, int n, int px,
                            int bold, int *w, int *h) {
    (void)s;
    (void)n;
    (void)px;
    (void)bold;
    if (w) *w = 0;
    if (h) *h = 0;
    return NULL;
}

int ui_key(int k) {
    SceCtrlData pad = {0};
    sceCtrlPeekBufferPositive(0, &pad, 1);

    switch (k) {
        case UI_K_OK:
            return (pad.buttons & (SCE_CTRL_CROSS | SCE_CTRL_START)) != 0;
        case UI_K_CANCEL:
            return (pad.buttons & SCE_CTRL_CIRCLE) != 0;
        case UI_K_LEFT:
            return (pad.buttons & SCE_CTRL_LEFT) != 0;
        case UI_K_RIGHT:
            return (pad.buttons & SCE_CTRL_RIGHT) != 0;
        default:
            return 0;
    }
}

int ui_focused(void) {
    return 1;
}

void ui_sleep_ms(int ms) {
    if (ms > 0)
        sceKernelDelayThread((unsigned int)ms * 1000u);
}

int ui_ask_native(const unsigned short *msg,
                  const unsigned short *ok,
                  const unsigned short *cancel) {
    (void)msg;
    (void)ok;
    (void)cancel;
    return 0;
}
