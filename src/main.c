// Necromancer PS2 - marco 2: carrega o atlas (8 bits + CLUT) e desenha as pecas do heroi.
// D-pad move as pecas. X alterna escala 1x/2x.
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <string.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>
#include "sprites.h"

extern const unsigned char atlas_pixels[];
extern const unsigned char atlas_clut[];

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

static const SpriteRect *find_sprite(const char *name) {
    for (int i = 0; i < g_sprite_count; i++)
        if (strcmp(g_sprites[i].name, name) == 0) return &g_sprites[i];
    return NULL;
}

static void draw_sprite(GSGLOBAL *gs, GSTEXTURE *t, const SpriteRect *s, float x, float y, float sc) {
    u64 col = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0);
    gsKit_prim_sprite_texture(gs, t,
        x, y, (float)s->x, (float)s->y,
        x + s->w * sc, y + s->h * sc, (float)(s->x + s->w), (float)(s->y + s->h),
        1, col);
}

int main(void) {
    GSGLOBAL *gs = gsKit_init_global();
    gs->ZBuffering = GS_SETTING_OFF;   // economiza VRAM p/ caber o atlas de 1 MB
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    gsKit_set_test(gs, GS_ZTEST_OFF);
    gs->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

    GSTEXTURE tex;
    memset(&tex, 0, sizeof(tex));
    tex.Width = 1024;
    tex.Height = 1024;
    tex.PSM = GS_PSM_T8;
    tex.Mem = (u32 *)atlas_pixels;
    tex.Clut = (u32 *)atlas_clut;
    tex.ClutPSM = GS_PSM_CT32;
    tex.Filter = GS_FILTER_NEAREST;
    tex.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(1024, 1024, GS_PSM_T8), GSKIT_ALLOC_USERBUFFER);
    tex.VramClut = gsKit_vram_alloc(gs, gsKit_texture_size(16, 16, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
    gsKit_texture_upload(gs, &tex);

    init_pad();

    static const char *parts[] = {
        "character/hero/head.png", "character/hero/trunk.png", "character/hero/arm.png",
        "character/hero/hand.png", "character/hero/hand2.png", "character/hero/leg.png",
        "character/hero/foot.png"
    };
    const SpriteRect *sp[7];
    for (int i = 0; i < 7; i++) sp[i] = find_sprite(parts[i]);

    float ox = 30, oy = 110, sc = 2.0f;
    int cross_prev = 0;
    struct padButtonStatus b;

    while (1) {
        if (padRead(0, 0, &b) != 0) {
            u32 p = 0xffff ^ b.btns;
            if (p & PAD_LEFT)  ox -= 3;
            if (p & PAD_RIGHT) ox += 3;
            if (p & PAD_UP)    oy -= 3;
            if (p & PAD_DOWN)  oy += 3;
            int cross = (p & PAD_CROSS) ? 1 : 0;
            if (cross && !cross_prev) sc = (sc > 1.5f) ? 1.0f : 2.0f;
            cross_prev = cross;
        }
        gsKit_clear(gs, GS_SETREG_RGBAQ(0x18, 0x18, 0x24, 0x80, 0));
        float x = ox;
        for (int i = 0; i < 7; i++) {
            if (!sp[i]) continue;
            draw_sprite(gs, &tex, sp[i], x, oy, sc);
            x += sp[i]->w * sc + 6;
        }
        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
