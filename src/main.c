// Necromancer PS2 - marco 1: quadrado que anda com D-pad; X/O/Quadrado/Triangulo mudam a cor.
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <string.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>

static char padBuf[256] __attribute__((aligned(64)));

static void init_pad(void) {
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, padBuf);
    int st;
    do { st = padGetState(0, 0); } while (st != PAD_STATE_STABLE && st != PAD_STATE_FINDCTP1);
}

int main(void) {
    GSGLOBAL *gs = gsKit_init_global();
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);

    init_pad();

    float x = gs->Width / 2.0f, y = gs->Height / 2.0f;
    u64 cor = GS_SETREG_RGBAQ(0x20, 0xC0, 0x40, 0x80, 0x00);
    struct padButtonStatus b;

    while (1) {
        if (padRead(0, 0, &b) != 0) {
            u32 p = 0xffff ^ b.btns;
            if (p & PAD_LEFT)  x -= 3;
            if (p & PAD_RIGHT) x += 3;
            if (p & PAD_UP)    y -= 3;
            if (p & PAD_DOWN)  y += 3;
            if (p & PAD_CROSS)    cor = GS_SETREG_RGBAQ(0x20, 0x40, 0xE0, 0x80, 0);
            if (p & PAD_CIRCLE)   cor = GS_SETREG_RGBAQ(0xE0, 0x30, 0x30, 0x80, 0);
            if (p & PAD_SQUARE)   cor = GS_SETREG_RGBAQ(0xE0, 0x30, 0xE0, 0x80, 0);
            if (p & PAD_TRIANGLE) cor = GS_SETREG_RGBAQ(0x20, 0xC0, 0x40, 0x80, 0);
        }
        gsKit_clear(gs, GS_SETREG_RGBAQ(0x10, 0x10, 0x18, 0x80, 0));
        gsKit_prim_sprite(gs, x - 16, y - 16, x + 16, y + 16, 1, cor);
        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
