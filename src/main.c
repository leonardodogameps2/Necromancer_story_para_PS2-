// Necromancer PS2 - marco 4: cenario + heroi montado por pecas, andando (idle/walk), virando de lado.
// Controle: D-pad e os dois analogicos movem. X=atacar, Quadrado=chutar, O=defender (segurar).
// Tela logica do jogo = 853x480 ("design"), mapeada p/ 640x360 centralizado.
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>
#include "sprites.h"

extern const unsigned char atlas_pixels[];
extern const unsigned char atlas_clut[];

static GSGLOBAL *gs;
static GSTEXTURE tex;
static float K = 0.75f, YOFF = 44.0f;   // design -> tela
#define DEG 0.01745329252f

static char padBuf[256] __attribute__((aligned(64)));

static void init_pad(void) {
    SifInitRpc(0);
    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);
    padInit(0);
    padPortOpen(0, 0, padBuf);
    int st;
    do { st = padGetState(0, 0); } while (st != PAD_STATE_STABLE && st != PAD_STATE_FINDCTP1);
    padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
}

static const SpriteRect *find_sprite(const char *name) {
    for (int i = 0; i < g_sprite_count; i++)
        if (strcmp(g_sprites[i].name, name) == 0) return &g_sprites[i];
    return NULL;
}

/* ---------- esqueleto do heroi (angulos em graus; positivo = sentido horario) ---------- */
typedef struct { float t, h, af1, af2, ab1, ab2, lf1, lf2, lb1, lb2; } Pose;
//                 tronco cabeca bracoF antebF bracoT antebT coxaF canelaF coxaT canelaT
#define POSE_IDLE     { 9, 9.5, 24, -61.2, 3.6, -23.9, -65.5, 66, 0, 9.5 }
#define POSE_WA       { 8, 14.5, 5, -145, 19.8, -50.8, -59, 87, 27, 12 }
#define POSE_WB { 13.2, 7.5, 19.8, -138.2, 10, -125, 25, 7, -63, 85 }
#define POSE_ATK_W    { -9, 3, -154, -30, -150, -35, -30, 46, 6, 26 }
#define POSE_ATK_H    { 39.5, -27, -65.5, 0, -62.5, -37.5, -66, 44, 22, 10 }
#define POSE_KICK_UP  { -14, -2, -17.5, -27.5, 12.5, 2.5, -80, 78, -4, 8 }
#define POSE_KICK_HIT { -20, -1, -10, -45, 25, -7.5, -95, 8, -2, 6 }
#define POSE_GUARD    { 6, -2, -70, -135, -30, -80, -42, 56, -14, 42 }
static const Pose P_IDLE  = POSE_IDLE;
static const Pose P_WA    = POSE_WA;
static const Pose P_WB    = POSE_WB;
static const Pose P_GUARD = POSE_GUARD;
static const Pose K_ATK[4]  = { POSE_IDLE, POSE_ATK_W, POSE_ATK_H, POSE_IDLE };
static const Pose K_KICK[4] = { POSE_IDLE, POSE_KICK_UP, POSE_KICK_HIT, POSE_IDLE };

static Pose lerp_pose(const Pose *a, const Pose *b, float u) {
    Pose r;
    const float *pa = (const float *)a, *pb = (const float *)b;
    float *pr = (float *)&r;
    for (int i = 0; i < 10; i++) pr[i] = pa[i] + (pb[i] - pa[i]) * u;
    return r;
}

static Pose pose_seq(const Pose *keys, int n, float u) {
    if (u >= 1.0f) return keys[n - 1];
    float f = u * (n - 1);
    int i = (int)f;
    return lerp_pose(&keys[i], &keys[i + 1], f - (float)i);
}

enum { S_TRUNK, S_HEAD, S_ARM, S_HAND, S_HAND2, S_LEG, S_FOOT, S_COUNT };
static const char *S_NAME[S_COUNT] = {
    "character/hero/trunk.png", "character/hero/head.png", "character/hero/arm.png",
    "character/hero/hand.png", "character/hero/hand2.png", "character/hero/leg.png",
    "character/hero/foot.png" };
static const float PVX[S_COUNT] = { 28, 28, 14, 13, 14, 12, 27 };   // pivo de cada peca (px do atlas)
static const float PVY[S_COUNT] = { 70, 56,  8, 10,  8,  6,  3 };
static const SpriteRect *spr[S_COUNT];

typedef struct { int s; float x, y, a; } Op;

static void rot(float a, float dx, float dy, float *ox, float *oy) {
    float r = a * DEG, c = cosf(r), s = sinf(r);
    *ox = dx * c - dy * s;
    *oy = dx * s + dy * c;
}

static void add_leg(Op *ops, int *n, float hx, float hy, float l1, float l2, float *low) {
    float kx, ky, fx, fy;
    ops[(*n)++] = (Op){ S_LEG, hx, hy, l1 };
    rot(l1, 0, 36, &kx, &ky); kx += hx; ky += hy;
    ops[(*n)++] = (Op){ S_FOOT, kx, ky, l1 + l2 };
    rot(l1 + l2, 19 - 27, 48 - 3, &fx, &fy);          // ponto da sola do pe
    if (ky + fy > *low) *low = ky + fy;
}

static void add_arm(Op *ops, int *n, float sx, float sy, float ang, float a2, int fore) {
    float ex, ey;
    ops[(*n)++] = (Op){ S_ARM, sx, sy, ang };
    rot(ang, 0, 42, &ex, &ey);
    ops[(*n)++] = (Op){ fore, sx + ex, sy + ey, ang + a2 };
}

// monta as 10 pecas na ordem de desenho; devolve y da sola (p/ apoiar no chao)
static float build_ops(const Pose *p, Op *ops) {
    float nx, ny, sx, sy, hx, hy, low = -1e9f;
    int n = 0;
    rot(p->t, 38.5f - 28, 16.5f - 70, &nx, &ny);     // pescoco
    rot(p->t, 10.2f - 28, 7.0f - 70, &sx, &sy);    // ombro (costas do tronco)
    rot(p->t, 23.4f - 28, 74.8f - 70, &hx, &hy);    // quadril
    add_leg(ops, &n, hx, hy, p->lb1, p->lb2, &low);
    add_arm(ops, &n, sx, sy, p->t + p->ab1, p->ab2, S_HAND2);
    ops[n++] = (Op){ S_TRUNK, 0, 0, p->t };
    ops[n++] = (Op){ S_HEAD, nx, ny, p->t + p->h };
    add_leg(ops, &n, hx, hy, p->lf1, p->lf2, &low);
    add_arm(ops, &n, sx, sy, p->t + p->af1, p->af2, S_HAND);
    return low;
}

static void draw_hero(float hx, float gy, const Pose *p, int flip) {
    Op ops[10];
    float low = build_ops(p, ops);
    u64 col = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0);
    for (int i = 0; i < 10; i++) {
        const SpriteRect *s = spr[ops[i].s];
        if (!s) continue;
        float px = PVX[ops[i].s], py = PVY[ops[i].s];
        float lx[4] = { -px, s->w - px, -px, s->w - px };
        float ly[4] = { -py, -py, s->h - py, s->h - py };
        float X[4], Y[4], rx, ry;
        for (int j = 0; j < 4; j++) {
            rot(ops[i].a, lx[j], ly[j], &rx, &ry);
            float wx = ops[i].x + rx;
            float wy = ops[i].y + ry - low;       // sola = y 0
            if (flip) wx = -wx;
            X[j] = (hx + wx) * K;
            Y[j] = (gy + wy) * K + YOFF;
        }
        float u0 = s->x + 0.5f, u1 = s->x + s->w - 0.5f, v0 = s->y + 0.5f, v1 = s->y + s->h - 0.5f;
        gsKit_prim_quad_texture(gs, &tex,
            X[0], Y[0], u0, v0,  X[1], Y[1], u1, v0,
            X[2], Y[2], u0, v1,  X[3], Y[3], u1, v1,
            1, col);
    }
}

/* ---------- cenario ---------- */
static void draw_rect_sprite(const SpriteRect *s, float x1, float y1, float x2, float y2, u64 col) {
    if (!s) return;
    gsKit_prim_sprite_texture(gs, &tex,
        x1 * K, y1 * K + YOFF, s->x + 0.5f, s->y + 0.5f,
        x2 * K, y2 * K + YOFF, s->x + s->w - 0.5f, s->y + s->h - 0.5f, 1, col);
}

int main(void) {
    gs = gsKit_init_global();
    gs->ZBuffering = GS_SETTING_OFF;
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);
    gsKit_init_screen(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
    gsKit_set_test(gs, GS_ZTEST_OFF);
    gs->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

    K = gs->Width / 853.0f;
    YOFF = (gs->Height - 480.0f * K) / 2.0f;

    memset(&tex, 0, sizeof(tex));
    tex.Width = 1024;
    tex.Height = 1024;
    tex.PSM = GS_PSM_T8;
    tex.Mem = (u32 *)atlas_pixels;
    tex.Clut = (u32 *)atlas_clut;
    tex.ClutPSM = GS_PSM_CT32;
    tex.Filter = GS_FILTER_LINEAR;
    tex.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(1024, 1024, GS_PSM_T8), GSKIT_ALLOC_USERBUFFER);
    tex.VramClut = gsKit_vram_alloc(gs, gsKit_texture_size(16, 16, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
    gsKit_texture_upload(gs, &tex);

    for (int i = 0; i < S_COUNT; i++) spr[i] = find_sprite(S_NAME[i]);
    const SpriteRect *sky = find_sprite("stage/sky.png"), *grass = find_sprite("stage/grass.png"),
                     *moon = find_sprite("stage/moon.png"), *shadow = find_sprite("stage/shadow.png"),
                     *ring = find_sprite("stage/chr_round.png");

    init_pad();

    float hx = 426, hy = 380;          // pes do heroi (coordenadas "design")
    int face_left = 1;
    float ph = 0, ph_idle = 0, blend = 0, guard = 0;
    int act = 0, act_t = 0;            // acao: 0 nada, 1 ataque (X), 2 chute (Quadrado)
    int prev_x = 0, prev_sq = 0;
    struct padButtonStatus b;
    u64 white = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0);
    u64 shadow_c = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x48, 0);
    u64 ring_c = GS_SETREG_RGBAQ(0x10, 0x38, 0x10, 0x80, 0);

    while (1) {
        float mx = 0, my = 0;
        int btn_x = 0, btn_o = 0, btn_sq = 0;
        if (padRead(0, 0, &b) != 0) {
            u32 p = 0xffff ^ b.btns;
            if (p & PAD_LEFT)  mx -= 1;
            if (p & PAD_RIGHT) mx += 1;
            if (p & PAD_UP)    my -= 1;
            if (p & PAD_DOWN)  my += 1;
            btn_x = (p & PAD_CROSS) != 0;
            btn_o = (p & PAD_CIRCLE) != 0;
            btn_sq = (p & PAD_SQUARE) != 0;
            if ((b.mode >> 4) == 0x07) {          // DualShock em modo analogico: os dois sticks movem
                float ax[2] = { (b.ljoy_h - 128) / 128.0f, (b.rjoy_h - 128) / 128.0f };
                float ay[2] = { (b.ljoy_v - 128) / 128.0f, (b.rjoy_v - 128) / 128.0f };
                for (int k = 0; k < 2; k++) {
                    if (fabsf(ax[k]) > 0.25f && fabsf(ax[k]) > fabsf(mx)) mx = ax[k];
                    if (fabsf(ay[k]) > 0.25f && fabsf(ay[k]) > fabsf(my)) my = ay[k];
                }
            }
        }
        // acoes: X = atacar (A), Quadrado = chutar (K), O = defender (D, segurar)
        if (act == 0 && !btn_o) {
            if (btn_x && !prev_x) { act = 1; act_t = 0; }
            else if (btn_sq && !prev_sq) { act = 2; act_t = 0; }
        }
        prev_x = btn_x; prev_sq = btn_sq;
        int defending = (act == 0 && btn_o);
        int moving = (act == 0 && !defending && (fabsf(mx) > 0.1f || fabsf(my) > 0.1f));
        if (moving) {
            hx += mx * 2.2f;
            hy += my * 1.5f;
            if (mx < -0.1f) face_left = 1;
            if (mx >  0.1f) face_left = 0;
            ph += 0.17f;
        }
        ph_idle += 0.06f;
        blend += ((moving ? 1.0f : 0.0f) - blend) * 0.2f;
        guard += ((defending ? 1.0f : 0.0f) - guard) * 0.25f;

        Pose cur;
        if (act) {
            int dur = (act == 1) ? 22 : 26;
            float u = (float)act_t / (float)dur;
            cur = pose_seq(act == 1 ? K_ATK : K_KICK, 4, u);
            if (act == 1 && u > 0.3f && u < 0.6f) hx += (face_left ? -2.2f : 2.2f);   // pequeno avanco no golpe
            if (++act_t >= dur) act = 0;
        } else {
            Pose idle = P_IDLE;
            float br = sinf(ph_idle);
            idle.t += 1.5f * br; idle.h -= 1.0f * br; idle.af2 += 3.0f * br; idle.ab2 += 3.0f * br;
            Pose walk = lerp_pose(&P_WA, &P_WB, (sinf(ph) + 1.0f) * 0.5f);
            cur = lerp_pose(&idle, &walk, blend);
            cur = lerp_pose(&cur, &P_GUARD, guard);
        }
        if (hx < 45) hx = 45;
        if (hx > 808) hx = 808;
        if (hy < 215) hy = 215;
        if (hy > 440) hy = 440;

        gsKit_clear(gs, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
        draw_rect_sprite(sky, 0, 0, 853, 190, white);
        draw_rect_sprite(moon, 693, 34, 793, 134, white);
        for (int i = 0; i < 3; i++)
            draw_rect_sprite(grass, i * 285.0f, 182, (i + 1) * 285.0f, 480, white);
        draw_rect_sprite(shadow, hx - 50, hy - 25, hx + 50, hy + 25, shadow_c);
        draw_rect_sprite(ring, hx - 50, hy - 20, hx + 50, hy + 20, ring_c);
        draw_hero(hx, hy, &cur, face_left);
        // barra de vida
        gsKit_prim_sprite(gs, (hx - 27) * K, (hy - 195) * K + YOFF, (hx + 27) * K, (hy - 190) * K + YOFF,
                          1, GS_SETREG_RGBAQ(0x00, 0xB0, 0x00, 0x80, 0));
        gsKit_queue_exec(gs);
        gsKit_sync_flip(gs);
    }
    return 0;
}
