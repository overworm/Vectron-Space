/**
 * @file main.c
 * @brief KRONOS STATION - Doom-style raycast sci-fi FPS prototype for the SNES
 *
 * Super FX (GSU) draws floor, ceiling and textured walls of the 3D view into a
 * 256x128 4bpp bitmap (gsu_ray.sfx).  Everything else is the 65816 + PPU:
 *   - enemies, projectiles, effects, weapon, HUD: OBJ sprites, depth-scaled by
 *     choosing between three pre-drawn sizes, hidden behind walls through the
 *     per-column wall distance (ZBUF) the GSU writes
 *   - boss supercomputer: BG2 (three sizes), scrolled by its screen position
 *   - lighting: distance shading + two light directions + normal-lit LUT in the
 *     GSU, whole-palette flashes (muzzle, damage, alarm) on the CPU
 *
 * Controls: D-pad move/turn, L/R strafe, Y run, B jump, A fire,
 *           X + Up/Down look up/down, SELECT centre view, START restart.
 */
#include <snes.h>
#include <snes/superfx.h>
#include <snes/snesmod.h>
#include <snes/hdma.h>
#include "soundbank.h"
#include "res/layout.h"
#include "res/palette.h"
#include "res/spr_defs.h"
#include "res/weapon_muz.h"
#include "res/hbar_pal.h"
#include "res/wire_pal.h"
#include "res/note.h"
#include "res/spr_pal.h"
#include "res/level.h"
#include "res/rowd.h"
#include "res/scr_pal.h"
#include "res/hud_tiles.h"

extern const u8 gsu_fc[], gsu_fc_end[], gsu_w[], gsu_w_end[];
extern void copyGpram(void);
extern const u8 spr_e0_n0[], spr_e0_n1[], spr_e0_n2[], spr_e0_nb[], spr_e0_s32[], spr_e0_s16[];
extern const u8 spr_e1_n0[], spr_e1_n1[], spr_e1_n2[], spr_e1_nb[], spr_e1_s32[], spr_e1_s16[];
extern const u8 spr_e2_n0[], spr_e2_n1[], spr_e2_n2[], spr_e2_nb[], spr_e2_s32[], spr_e2_s16[];
extern const u8 spr_e3_n0[], spr_e3_n1[], spr_e3_n2[], spr_e3_nb[], spr_e3_s32[], spr_e3_s16[];
extern const u8 spr_e4_n0[], spr_e4_n1[], spr_e4_n2[], spr_e4_nb[], spr_e4_s32[], spr_e4_s16[];
extern const u8 spr_e5_n0[], spr_e5_n1[], spr_e5_n2[], spr_e5_nb[], spr_e5_s32[], spr_e5_s16[];
extern const u8 spr_prop[];
#include "res/prop_pal.h"
extern const u8 spr_e6_n0[], spr_e6_n1[], spr_e6_n2[], spr_e6_nb[], spr_e6_s32[], spr_e6_s16[];
/* v13: helpers that only the author's patched OpenSNES had; defined here so a stock OpenSNES builds the game */
static void oam_hide_range(u8 first, u8 n) { u16 i; for (i = first; i < (u16)first + n && i < 128; i++) oamHide((u8)i); }
#define oamHideRange(a, n) oam_hide_range((u8)(a), (u8)(n))
#define nmiSetLag(f) ((void)0)       /* stock lib: nmiSet(pad_tick) is called on every NMI already */
extern void gsuLoadFc(void);
extern void gsuLoadW(void);
extern void gsuDmaFrame8(void);
extern void gsuLoadS(void), gsMove0(void), gsMove1(void);
#define GSPR_X(t) extern const u8 gspr_e##t##_a[], gspr_e##t##_b[];
GSPR_X(0) GSPR_X(1) GSPR_X(2) GSPR_X(3) GSPR_X(4) GSPR_X(5) GSPR_X(6)
#include "res/gspr.h"
extern const u8 spr_note[], spr_wire[], spr_hbar[], spr_weapon[], spr_weapon2[], spr_wicons[], spr_big[], spr_obj1[], card_tiles[];
#include "res/card.h"
#define CARD_T0 100
extern const u8 spr_obj1[];
extern const u8 spr_fx0[];
extern const u8 boss_tiles[];
extern const u8 scr_0a[], scr_0b[], scr_1a[], scr_1b[];

/* ---------------------------------------------------------------- Game Pak RAM */
#define GP_U8(a)   ((u8 FAR *)(0x700000L + (a)))
#define GP_U16(a)  ((u16 FAR *)(0x700000L + (a)))
#define V_POSX 0x00
#define V_POSY 0x02
#define V_ANG  0x04
#define V_HOR  0x06
#define V_ZS   0x08
#define V_TEXF 0x0E
#define V_TEXC 0x10

/* ---------------------------------------------------------------- VRAM layout */
#define VRAM_BG1_CHR  0x0000
#define VRAM_BG2_CHR  0x3000          /* mode 3: BG1 is 8 bpp (20 x 16 tiles = 0x2800 words), BG2 the 4 bpp HUD */
#define VRAM_BG1_MAP  0x5000
#define VRAM_BG2_MAP  0x5400
#define VRAM_OBJ0     0x6000
#define VRAM_OBJ1     0x7000
#define BLANK_TILE    511          /* BG2 char base: unused, zero */
#define BLANK8        364          /* BG1 (8 bpp) tile at word 0x2D80: zero (v13: 28 columns x 13 tiles = 364 tiles, no gaps) */
#define VIEW_XS       16           /* screen column of the 224 px wide view (v13) */
#define VIEW_W        224
#define VIEW_COLS     112
#define VIEW_TOP      (VIEW_Y + 12)  /* screen line of bitmap row 0: the 104 row view keeps the old horizon line (VIEW_Y + 64) */

#define TOP_BAND    40             /* letterbox: forced blank lines top/bottom */
#define VIEW_Y      40             /* screen line of bitmap row 0 */

/* ---------------------------------------------------------------- sound */
enum { S_FUSER, S_QUANTON, S_PLASMA, S_RAIL, S_ROCKET, S_BEAM, S_KOZE, S_ESHOT, S_EHIT, S_EDIE, S_HURT, S_PICK, S_WEAPON,
       S_DOOR, S_JUMP, S_MELEE, S_MEDKIT, S_VON, S_VOFF, S_SPAWN, S_WIN, S_DEATH, S_READY, S_BLIP, S_RICO, S_CHAIN, S_STEP, NSFX };
/* SNESMOD has 16 effect slots: the sound ids share exactly 16 samples (a 17th load returns $FF and plays as slot 15) */
static const u16 sfx_src[NSFX] = { SFX_FUSER, SFX_QUANTON, SFX_PLASMA, SFX_RAIL, SFX_ROCKET, SFX_BEAM, SFX_KOZE, SFX_PLASMA, SFX_MELEE,
    SFX_EDIE, SFX_HURT, SFX_PICK, SFX_PICK, SFX_DOOR, SFX_JUMP, SFX_MELEE, SFX_PICK, SFX_BLIP, SFX_BLIP, SFX_DOOR, SFX_PICK,
    SFX_EDIE, SFX_BLIP, SFX_BLIP, SFX_BLIP, SFX_CHAIN, SFX_STEP };
static u8 sfx_slot[NSFX];
static u8 sfx_cd[NSFX];
static void sfx_init(void) {
    u8 i, j;
    for (i = 0; i < NSFX; i++) {
        for (j = 0; j < i; j++) if (sfx_src[j] == sfx_src[i]) break;
        sfx_slot[i] = (j < i) ? sfx_slot[j] : snesmodLoadEffect(sfx_src[i]);
    }
}
static void sfx(u8 id) {
    if (sfx_cd[id]) return;                       /* the same sound at most every few frames */
    sfx_cd[id] = 4;
    snesmodPlayEffect(sfx_slot[id], 127, 128, SNESMOD_PITCH_NORMAL);
}
static void sfx_tick(void) {
    u8 i;
    for (i = 0; i < NSFX; i++) if (sfx_cd[i]) sfx_cd[i]--;
}

/* ---------------------------------------------------------------- helpers */
static s16 sin8w[512];                          /* the sine table in WRAM: a ROM table read costs a far-read call every time */
#define SINA(a) sin8w[(a) & 511]
#define COSA(a) sin8w[((a) + 128) & 511]

static s16 iabs(s16 v) { return v < 0 ? -v : v; }
static s16 clamp16(s16 v, s16 lo, s16 hi) { return v < lo ? lo : (v > hi ? hi : v); }

static u16 dist2d(s16 dx, s16 dy) {
    u16 ax = (u16)iabs(dx), ay = (u16)iabs(dy);
    return ax > ay ? ax + (ay >> 1) : ay + (ax >> 1);
}

/* the level tables read every frame are copied to WRAM once: a read of a ROM table is a far-read call (about 700 master cycles) every time */
static u8 door_xyk_w[NUM_DOORS][3], door_sec_w[NUM_DOORS][2], item_kind_w[NUM_ITEMS], room_rc_w[NUM_ROOMS][5];
/* item_pos / spawn_cls / spawn_room / spawn_pos stay in ROM (read a few times per update): ~300 B of WRAM saved for the 51-spawn level */
/* spawn_room / spawn_pos stay in ROM (read a few times per update): 255 B of WRAM saved for the 51-spawn level */
static void mirror_tables(void) {
    u8 i, j;
    for (i = 0; i < NUM_DOORS; i++) { for (j = 0; j < 3; j++) door_xyk_w[i][j] = door_xyk[i][j]; door_sec_w[i][0] = door_sec[i][0]; door_sec_w[i][1] = door_sec[i][1]; }
    for (i = 0; i < NUM_ITEMS; i++) { item_kind_w[i] = item_kind[i]; }
    for (i = 0; i < NUM_ROOMS; i++) for (j = 0; j < 5; j++) room_rc_w[i][j] = room_rc[i][j];
}
#define door_xyk door_xyk_w
#define door_sec door_sec_w
#define item_kind item_kind_w
#define room_rc room_rc_w

/* The GSU runs from its code cache while the 65816 goes on with the game logic, and Game Pak RAM belongs to the GSU meanwhile: the logic
   works on WRAM copies of the map and of the wall distances; changes to the map are queued and written when the GSU has stopped. */
static u8 map_w[2048];
static u16 zbuf_w[VIEW_COLS];
u16 dbg_it;                                          /* rendered frames (for the fps measurement) */
static u16 zb_ang, zb_ang_pend;                    /* camera angle the wall distances were rendered at */
static u16 gpq_a[24];
static u8 gpq_v[24], gpq_n, gpq_full;
static u8 cell(s16 x, s16 y) {
    u16 cx, cy;
    if (x < 0 || y < 0) return 1;
    cx = (u16)x >> 8;
    cy = (u16)y >> 8;
    if (cx > 31 || cy > 63) return 1;
    return map_w[(cy << 5) + cx];
}

static void crystal_pos(void);
static u8 blocked(s16 x, s16 y, s16 r) {
    return cell(x - r, y - r) || cell(x + r, y - r) || cell(x - r, y + r) || cell(x + r, y + r);
}

/* clear line of sight between two points (steps of a quarter tile) */
static u8 los(s16 x0, s16 y0, s16 x1, s16 y1) {
    s16 dx = x1 - x0, dy = y1 - y0;
    u16 d = dist2d(dx, dy);
    u8 n = (u8)(d >> 6), i;
    s16 sx, sy;
    if (n == 0) return 1;
    sx = dx / n;
    sy = dy / n;
    for (i = 1; i < n; i++) {
        x0 += sx;
        y0 += sy;
        if (cell(x0, y0)) return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- state */
#define MAXE   2
#define MAXB   8
#define MAXFX  4
#define PLAYER_HP 100

enum { E_NONE, E_TELE, E_IDLE, E_WALK, E_RUN, E_STRAFE, E_CROUCH, E_JUMP, E_SHOOT, E_HURT, E_DEAD, E_FLEE };
enum { F_IDLE = 0, F_WALK = 1, F_AIM = 7, F_FIRE = 8, F_HURT = 9, F_CROUCH = 10, F_JUMP = 11,
       F_DEATH = 12, F_BACK = 15, F_V20 = 17, F_V90 = 18, F_V135 = 19 };
#define NBACK 5
#define OAM_AIM 77        /* 12: brackets + life cells */
#define OAM_MAP 89        /* minimap arrow */
#define OAM_OBJA 90       /* optovisor objective arrow */
#define OAM_TRB 91        /* the TR button */
#define OAM_CHG 92        /* 8: weapon charge bar */
#define OAM_ITEM 100      /* 8 */
#define OAM_OVAL 108      /* 4 */
#define OAM_TXT 112       /* 16: wall text / gate lamps */

typedef struct {
    s16 x, y, z, vz;
    s16 hp;
    u8 st, shown_f, shown_v, dir, type, side, flee, up, sp;
    s16 lsx;
    u16 t, at, shotcd, pt;
    u8 fired, rush, ph, seen, fire_t, burst, bt, hid, gm, fk;
    s16 mark;
} Enemy;

typedef struct { s16 x, y, vx, vy; u8 on, kind, life, miss; } Bolt;      /* kind 0 enemy, 1 plasma */
typedef struct { s16 x, y; u8 on, kind, f, t; } Fx;                /* 0 spark 1 tele 2 puff */

static Enemy en[MAXE];
static void gs_invalidate(void);
#define BOSS_DOWN(i) (en[i].type == 0 && (bst == 7 || bst == 8))      /* the fallen boss: no auto-aim, shots pass through */
#define IS_BARREL(e) ((e)->type == 6 && (bst == 1 || ((bst == 3 || bst == 4) && (e)->hp == 99)))      /* the boss's rolling barrel (wave 2 and the glyph wall) */
static Bolt bolts[MAXB];
static Fx fxs[MAXFX];
/* level state */
#define K_BLUE 1
#define K_YELLOW 2
#define K_RED 4
static u8 sp_st[NUM_SPAWNS];                  /* 0 asleep 1 awake 2 killed */
static u8 item_got[NUM_ITEMS];
static u8 door_st[NUM_DOORS];                 /* 0 closed 1 open (auto) 2 open for good */
static u8 trd_open, crystals, keys, have_w, cur_w, level_no;
static u16 super_t, banner_t;                 /* super_t: the radiation is switched off (bottle) */
static u8 medkits;                              /* 0..5, used automatically at the last cell of the power bar */
static u16 rad_t;                               /* radiation: one cell every 15 s */
#define LOOK_ROOM (NUM_ROOMS - 1)
static u8 wv_sp[NUM_ROOMS], wv_dead[NUM_ROOMS], wv_n[NUM_ROOMS];     /* waves of 3: spawned / killed in the wave, waves done */
static u8 wv_t[NUM_ROOMS];
static const char *const wave_txt[6] = { "WAVE 1", "WAVE 2", "WAVE 3", "WAVE 4", "WAVE 5", "WAVE 6" };
static void mus_set(u8 mode);
static u16 mus_hold;
static const char *banner_txt;
static u16 obj_cur[128];
static u8 obj_dirty, wp_pal_dirty;
static u16 vis_t[3], koze_t, pl_shot_t, a_hold, r_hold, prev_held;
#define nv_t vis_t[0]
static u8 koze_rep, info_win, info_clr, info_row;
/* first-run HUD tour: grey picture, the game frozen, a white frame walks over the bar, the cells, the crystals, 3R and 5L */
static u8 tour, tour_done, tour_rep;
static u16 tour_t;
static const char *const tour_txt[8] = { "HEALTH CELLS", "FOUND CRYSTALS", "REPAIR REP 2", "MEDKIT MDK 5", "SELECTED WEAPON", "AMMO", "RELOAD BAR", "ENEMIES" };
/* The white frame is baked into copies of the HUD glyphs themselves: the top lines of the screen hold as many sprite tiles as the hardware can fetch, so the frame can not be extra sprites.
   Up to 4 variants (glyph + frame edge) go into spare marker cells (43, 44, 52, 53) for the length of the tour. */
static const u8 tv_cell[4] = { 43, 44, 52, 53 };
static u8 tv_ready, tv_n, tv_g[4], tv_e[4], tv_cnt, tv_id[10], tv_k[10];
static u8 tv_x[10];
/* the boss cut-scenes borrow the tour's spare bytes (the tour is over for good by then) */
#define CSP    (tour_done ? cs_ph : 0)   /* the cut-scene bytes share the tour's memory: they only count once the tour is over */
#define cs_ph  tv_x[0]   /* 0 none, 1 white flash, 2 black + text, 3 the black halves part */
#define cs_n   tv_x[1]   /* frames in the phase */
#define cs_w   tv_x[2]   /* 0 darkness, 1 all crystals found */
#define cs_win tv_x[3]   /* the colour window (the curtain) is on */
#define cs_dim tv_x[4]   /* the OBJ palettes are dimmed (dark boss stage) */
static u8 tv_buf[64];
/* the enemy attacks borrow tv_buf too (the HUD tour that fills it is over for good by the time they matter) */
#define info_pg tv_buf[63]              /* the briefing card: page 0 / 1 (the card is up before the tour starts, so the tour's memory is free then) */
#define BHN    tv_buf[26]                /* wave 1: boss helpers sent so far (0..4) */
#define cr_t   tv_buf[0]                 /* the crack on the camera: 140..49 visible, 48..1 washed away by the blue beam */
#define cr_x   tv_buf[1]
#define cr_y   tv_buf[2]
#define bm_t   tv_buf[3]                 /* an enemy beam / rail line on screen (frames) */
#define bm_x   tv_buf[4]
#define bm_y   tv_buf[5]
#define wh_t   tv_buf[6]                 /* kozematy: the screen is white (frames) */
#define wh_on  tv_buf[7]
#define wr_s   tv_buf[23]                /* the wires minigame: closed switches (bit per switch) */
#define wr_c   tv_buf[24]                /* the selected switch */
#define wr_t   tv_buf[25]                /* the current flows: frames before the next stage */
#define wr_pl  tv_buf[27]                /* the white current: position along the circuit (px), low / high byte */
#define wr_ph  tv_buf[28]
#define wr_d   tv_buf[29]                /* 0 forward, 1 coming back */
#define wr_e   (&tv_buf[30])             /* per wire: the slot where the current leaves it (6 = the right end) */
#define wr_n   tv_buf[34]                /* wires used by the circuit */
#define wr_w   tv_buf[35]                /* 1: the circuit reaches GROUND */
#define wr_lf  (*(u16 *)&tv_buf[36])     /* last drawn frame */
#define sv_dev tv_buf[38]
#define sp_t   tv_buf[39]                /* the boss spins (chase): ticks */
#define wr_t0  (*(u16 *)&tv_buf[42])
#define wr_pf  tv_buf[40]                /* the minigame palettes are loaded */
#define hb_s   tv_buf[22]                /* the handlebars on the bike: 0 left, 1 straight, 2 right */
#define lk_s   tv_buf[21]                /* the eyes' gaze: 0 straight, 1 the hero strafes left, 2 right */
#define rl_cd  (*(u16 *)&tv_buf[8])      /* railgun: one shot per 5 s for all enemies together */
#define kz_cd  (*(u16 *)&tv_buf[10])     /* kozematy: one per 40 s */
#define sk_n   tv_buf[12]                /* spawn points skipped because they were in view and close */
#define bm_tx  tv_buf[13]                /* where the beam ends on screen */
#define bm_ty  tv_buf[14]
#define ch_i   tv_buf[46]                /* the debug cheat: how much of the code is typed */
#define eye_f  tv_buf[47]                /* after the wires: 2 the eyes close, 1 the black card is due once they are open */
#define ar_t   tv_buf[15]                /* the arrival strip after the chase (frames / 2) */
#define xp_t   tv_buf[16]                /* an explosion on screen (ticks of 2 frames) */
#define xp_x   (*(s16 *)&tv_buf[17])
#define xp_y   (*(s16 *)&tv_buf[19])
#define ey_a  tv_buf[62]
#define ey_f  tv_buf[29]                    /* bit 0 the eye is on, bit 1 the medkit is on, bit 2 the redvisor was on last time */
#define ey_te tv_buf[24]                    /* when the eye moves on (frames / 8) */
#define ey_tm tv_buf[25]                    /* when the medkit is gone */
#define ATK_RESET() { tv_buf[61] = 0; eye_f = 0; ch_i = 0; tv_buf[50] = 0; tv_buf[54] = 0; cr_t = 0; bm_t = 0; wh_t = 0; wh_on = 0; rl_cd = 480; kz_cd = 1200; sk_n = 0; ar_t = 0; xp_t = 0; }

/* the wires minigame: 4 thick wires (red = VOLTAGE on top ... black = GROUND at the bottom), 2 round switches on each, 5 slots of 32 px across.
   The white current runs from the left end of the red wire; at a switch that is UP (1) it goes straight on, at one that is LEFT (0) it drops to the next wire
   and goes on to the right from there. Reaching the right end of the black wire = GROUND. A dead end sends the current back along the circuit. */
static const u8 wr_sl[4][2] = { { 1, 3 }, { 2, 4 }, { 1, 3 }, { 2, 4 } };
static void wr_calc(void) {
    u8 r = 0, s = 0, k, dr;
    wr_n = 0; wr_w = 0;
    while (r < 4) {
        dr = 0; wr_e[r] = 6;
        for (k = 0; k < 2; k++) {
            u8 sl = wr_sl[r][k];
            if (sl > s && !((wr_s >> (r * 2 + k)) & 1)) { wr_e[r] = sl; dr = 1; break; }
        }
        wr_n = (u8)(r + 1);
        if (!dr) { if (r == 3) wr_w = 1; break; }
        if (r == 3) break;
        s = wr_e[r]; r++;
    }
}
static u16 wr_len(void) {
    u8 r; u16 n = 0; s16 xs = 48;
    for (r = 0; r < wr_n; r++) {
        s16 xe = (s16)(wr_e[r] == 6 ? 208 : 64 + 32 * wr_e[r]);
        n = (u16)(n + (u16)(xe - xs));
        if (r + 1 < wr_n) n = (u16)(n + 32);
        xs = xe;
    }
    return n;
}
static void wr_xy(u16 p, s16 *ox, s16 *oy) {                 /* the point at distance p along the circuit */
    u8 r; s16 xs = 48, xe, y;
    for (r = 0; r < wr_n; r++) {
        xe = (s16)(wr_e[r] == 6 ? 208 : 64 + 32 * wr_e[r]);
        y = (s16)(VIEW_Y + 16 + 32 * r);
        if (p <= (u16)(xe - xs)) { *ox = (s16)(xs + p); *oy = y; return; }
        p = (u16)(p - (u16)(xe - xs));
        if (r + 1 >= wr_n) { *ox = xe; *oy = y; return; }
        if (p <= 32) { *ox = xe; *oy = (s16)(y + p); return; }
        p = (u16)(p - 32); xs = xe;
    }
    *ox = xs; *oy = VIEW_Y + 16;
}
static void wire_init(void) {                                /* random positions of the switches (never solved at the start) */
    u8 n = 0;
    wr_c = 0; wr_t = 0; wr_pl = 0; wr_ph = 0; wr_d = 0; wr_lf = 0; wr_pf = 0; tv_buf[62] = 0;
    do { wr_s = (u8)rngNext(); wr_calc(); } while (wr_w && ++n < 20);
    if (wr_w) { wr_s = 0; wr_calc(); }
}

static u16 info_t, info_buf[64];
static u8 info_wr, info_wn;
static u8 r_long, l_used, sp_rr, shot_w, koze_on, koze_shown, rail_on;
static u16 koze_row[32], koze_blank[32];
static u8 blink_tp, blink_was;
static const u16 mm_pal[4] = { 0, (u16)((28 << 10) | (22 << 5) | 6), (u16)((5 << 10) | (3 << 5) | 1), 0 };
static u16 x_hold, blink_t;                    /* X held 2 s = close the eyes, wake up at the start */
static u8 sp_left[NUM_SPAWNS];
static u8 gpause;                              /* START: the game stands still */
static u8 sp_cd[NUM_SPAWNS];
typedef struct { s16 x, y, vx, vy, z, vz; u8 on; } Drop;       /* on: 1 lying, 2 falling from the ceiling portal */
static Drop drops[4];
static u8 bcry_on;                                  /* the ninth crystal: dropped by the boss when it dies */
static s16 bcry_x, bcry_y;
/* boss stages: 0 rockets + charge, 1 energy waves (jump), 2 invisible (nightvisor) + teleport race, 3 searching (optovisor reads the key), 4 rockets again, 5 button (redvisor), 6 dead */
static u8 bst, tp_on, bw_on;
static u16 bst_t, bw_cd;
static s16 tp_x, tp_y, bw_x, bw_y;
#define BK_X 4736
#define BK_Y 290
/* life is counted in small squares: boss 20, classes E1..E5 = 2,4,6,8,10 (indexed by art type) */
static const u8 sq_n[7] = { 20, 2, 6, 4, 8, 10, 5 };
static const u8 cls_type[7] = { 1, 3, 2, 4, 5, 0, 6 };      /* E1..E5, boss -> sprite/art type */
#define NW 8
/* fuser, quanton, plasmagun, railgun, rocketgun, beamchain, kozematy: damage = squares taken, charge time in 1/60 s */
static const u8 w_dmg[NW] = { 1, 2, 3, 4, 5, 6, 7, 1 };
static const u16 w_cd[NW] = { 10, 18, 22, 28, 34, 44, 52, 6 };    /* short gap between shots of one magazine */
static const char *const w_name[NW] = { "FUSER", "QUANTON", "PLASMAGUN", "RAILGUN", "ROCKETGUN", "BEAMCHAIN", "KOZEMATY", "CHAINGUN" };
static const u8 w_ord[NW] = { 0, 1, 2, 7, 3, 4, 5, 6 };      /* switching order: the chaingun sits between plasmagun and rocket launcher */
static u8 chain_n;
static u16 w_left[NW];
/* ammunition: fuser unlimited (255), W2..W5 five rounds, W6/W7 two; an empty weapon reloads by itself, the player may switch meanwhile */
static const u8 w_amax[NW] = { 255, 5, 5, 5, 5, 2, 2, 100 };
static const u16 w_rel_t[NW] = { 0, 240, 240, 240, 240, 240, 240, 240 };   /* 2 s for every weapon */
static u8 w_ammo[NW];
static u16 w_rel[NW];
static const char *const d_name[3] = { "NIGHTVISOR", "OPTOVISOR", "REDVISOR" };
static u8 have_d, dev_on, cls_sel, cur_d, gate_pow, nv_gone, wv_k;       /* wv_k: helpers / fighters killed in the boss fight so far (the ENM counter) */
static u16 read_t;
static u8 stage_no;

/* player */
static s16 px, py;
static u16 pang;
static s16 ppitch;
static s16 pz, pvz;                   /* jump height (8.8 tiles) and speed */
static s16 php;
static u16 bobph, stp_acc;
static u8 moving, running;
static u16 fire_cd, flash_t, hurt_t, wp_t;
static s16 wp_lower;
static u8 wp_frame;
static u8 dead, won, lives;
static u8 en_act[MAXE];                 /* 1 = the enemy's sector is awake */
static u8 rst[NUM_ROOMS], room_left[NUM_ROOMS], door_lk[NUM_DOORS];   /* arena rooms: 0 idle, 1 pause, 2 fight, 3 cleared */
static u8 rt_[NUM_ROOMS];
static u8 ghost_act, ghost_n, ghost_kill;                              /* class 6: woken by the LOOK wall, 3 of them */
static u16 kills;
static u16 dead_t, win_t;
static s16 shake;

/* per frame projections kept for hit tests */
static s16 scr_x0[MAXE], scr_x1[MAXE], scr_y0[MAXE], scr_y1[MAXE], scr_f[MAXE];
static u8 scr_vis[MAXE];

static u16 last_fc;
static u8 pal_cur = 255;

/* ---------------------------------------------------------------- text */
static u8 glyph_of(char c) {
    if (c >= '0' && c <= '9') return (u8)(c - '0');
    if (c >= 'A' && c <= 'Z') return (u8)(10 + c - 'A');
    if (c == '/') return 36;
    if (c == ':') return 37;
    if (c == '-') return 38;
    if (c == '!') return 39;
    return 255;
}

/* OBJ table 1 holds the glyphs: tile = 256 + (g/8)*32 + (g%8)*2 */
static u16 glyph_tile(u8 g) {
    return (u16)(256 + (g >> 3) * 32 + (g & 7) * 2);
}

static u8 put_glyph(u8 id, s16 x, s16 y, u8 g) {
    oamSet(id, (u16)x, (u16)y, glyph_tile(g), 2, 3, 0);
    oamSetSize(id, 0);
    return (u8)(id + 1);
}

static u8 put_text(u8 id, s16 x, s16 y, const char *s) {
    while (*s) {
        u8 g = glyph_of(*s);
        if (g != 255) id = put_glyph(id, x, y, g);
        x += 12;
        s++;
    }
    return id;
}

__attribute__((unused)) static u8 put_num(u8 id, s16 x, s16 y, u16 v, u8 digits) {
    u8 i;
    u16 div = (digits == 3) ? 100 : 10;
    for (i = 0; i < digits; i++) {
        id = put_glyph(id, x, y, (u8)((v / div) % 10));
        x += 12;
        div = div / 10;
        if (div == 0) div = 1;
    }
    return id;
}

/* ---------------------------------------------------------------- VRAM streams */
/* Each enemy owns half of OBJ table 0 (8 x 8 tiles). Canvas class by distance level:
   levels 0-1 = 64x64 (four large sprites), 2-3 = 32x32 (one large), 4-6 = 16x16 (one small). */
#define PICK(t, nm) ((t) == 1 ? spr_e1_##nm : (t) == 2 ? spr_e2_##nm : (t) == 3 ? spr_e3_##nm : (t) == 4 ? spr_e4_##nm : (t) == 5 ? spr_e5_##nm : (t) == 6 ? spr_e6_##nm : spr_e0_##nm)
static const u8 *enemy_src(u8 type, u8 lvl, u8 frame) {
    if (lvl < 3) {
        if (frame >= F_BACK) return PICK(type, nb) + (u16)((lvl * NBACK + (frame - F_BACK)) * 2048);
        return (lvl == 0 ? PICK(type, n0) : lvl == 1 ? PICK(type, n1) : PICK(type, n2)) + (u16)(frame * 2048);
    }
    if (lvl < 6) return PICK(type, s32) + (u16)(frame * 1536 + (lvl - 3) * 512);
    return PICK(type, s16) + (u16)(frame * 512 + (lvl - 6) * 128);
}

static u16 slot_base(u8 slot) {
    return (u16)(VRAM_OBJ0 + (u16)slot * 128);
}

static void upload_enemy(u8 slot, u8 type, u8 lvl, u8 frame) {
    const u8 *src = enemy_src(type, lvl, frame);
    u16 base = slot_base(slot);
    u8 r;
    if (lvl < 3) {
        for (r = 0; r < 8; r++) dmaCopyVram(src + r * 256, base + r * 256, 256);
    } else if (lvl < 6) {
        for (r = 0; r < 4; r++) dmaCopyVram(src + r * 128, base + r * 256, 128);
    } else {
        for (r = 0; r < 2; r++) dmaCopyVram(src + r * 64, base + r * 256, 64);
    }
}

#define ARR_T0 120                                                /* the arrival at the glyph wall: the boss rides up on its bike (frames) ... */
#define ARR_T1 230                                                /* ... leaves it and runs to the wall; then 5 s at the terminal */
#define IS_PROP(e) ((e)->type == 0 && (e)->hid == 7)
static void upload_prop(u8 slot, u8 lvl) {                     /* the parked bike of the cut-scene: its own rider-less pictures */
    const u8 *src = spr_prop;
    u16 base = slot_base(slot);
    u8 r;
    if (lvl < 3) { src += (u16)lvl * 2048; for (r = 0; r < 8; r++) dmaCopyVram(src + r * 256, base + r * 256, 256); }
    else if (lvl < 6) { src += 6144 + (u16)(lvl - 3) * 512; for (r = 0; r < 4; r++) dmaCopyVram(src + r * 128, base + r * 256, 128); }
    else { src += 7680 + (u16)(lvl - 6) * 128; for (r = 0; r < 2; r++) dmaCopyVram(src + r * 64, base + r * 256, 64); }
}

/* which animation frame an enemy shows (shared by drawing and the VRAM streamer) */
static u8 enemy_frame(const Enemy *e) {
    if (IS_BARREL(e)) return (u8)(F_CROUCH + ((last_fc >> 3) & 1));                    /* the barrel rolls: two views */
    if (bst == 10 && e->st != E_NONE) {                                                              /* the chase: the boss on the bike seen from behind; it turns round while it burns; the rolling barrel (2 views) */
        if (e->type == 0) { static const u8 spb[6] = { F_JUMP, 3, 4, 5, 6, F_CROUCH }; return sp_t ? spb[(last_fc >> 2) % 6] : F_JUMP;     /* the boss on the bike turns through 6 views */ }
        return (u8)(F_CROUCH + ((last_fc >> 3) & 1));
    }     /* the chase: the boss on the bike, a barrel or a saw */
    if (bst == 9 && e->type == 0 && e->st != E_NONE) {                              /* the cut-scene: the prop bike, the boss running, on foot, on its bike */
        if (e->hid == 7) return F_CROUCH;
        if (bw_y == 1) return (u8)(F_BACK + ((last_fc >> 2) & 1));
        return (u8)(bw_y == 4 ? F_V90 : bw_y >= 5 ? F_JUMP : bw_y == 0 ? F_IDLE : F_BACK);
    }
    if (bst == 13 && e->type == 0 && e->st != E_NONE) return (u8)(e->hid == 7 ? F_CROUCH : bst_t < ARR_T0 ? F_JUMP : bst_t < ARR_T1 ? F_V90 : F_BACK);      /* the bike, the boss on it, on foot, at the wall */
    if (e->type == 0 && e->st != E_DEAD && e->st != E_HURT) {
        if (bst == 3 && bw_y == 3) return (u8)(F_BACK + ((last_fc >> 2) & 1));      /* it runs away to the left */
        if (bst == 7) return (u8)(bst_t < 30 ? F_DEATH : (bst_t < 170 ? F_DEATH + 2 : (bst_t < 205 ? F_DEATH + 1 : F_IDLE)));   /* falls, lies, gets up */
        if (bst == 8) return (u8)(F_DEATH + 2);                                      /* the last time it stays down while the fighters come */
        if (bst == 11 && bst_t >= 40) return (u8)(F_BACK + ((last_fc >> 2) & 1));                      /* it runs away to the teleport */
        if (bst == 5 || bst == 11) { static const u8 spin[4] = { F_IDLE, F_V90, F_BACK, F_V135 }; return spin[(last_fc >> 3) & 3]; }   /* two lives left: it spins on its axis */
        if (bst == 2 || bst == 13 || (bst == 3 && !e->pt)) return F_BACK;                        /* the boss stands at the wall, back to the hero */
        if (bst == 3) return e->fire_t ? F_FIRE : F_AIM;                            /* it turns round and shoots when the hero attacks */
    }
    if (e->fire_t && e->st != E_DEAD && e->st != E_HURT && e->st != E_TELE && e->st != E_FLEE) return F_FIRE;
    switch (e->st) {
    case E_WALK:
        if (dist2d(px - e->x, py - e->y) < 600) {                                  /* backing off: turns away */
            if (e->at < 6) return F_V135;
            return (u8)(F_BACK + ((e->at / 8) & 1));
        }
        return (u8)(F_WALK + (e->at / 5) % (e->type ? 6 : 2));
    case E_FLEE:
        if (e->at < 6) return F_V135;
        return (u8)(F_BACK + ((e->at / 8) & 1));
    case E_RUN:    return (u8)(F_WALK + (e->at / 4) % (e->type ? 6 : 2));
    case E_STRAFE: return (e->at < 8 || e->t < 8) ? F_V20 : F_V90;   /* circling: side view */
    case E_CROUCH: return e->type ? F_CROUCH : F_IDLE;      /* frames 3-6, 10, 11 of the boss are the bike */
    case E_JUMP:   return e->type ? F_JUMP : F_IDLE;
    case E_SHOOT:  return (e->at >= 12) ? F_FIRE : F_AIM;
    case E_HURT:   return F_HURT;
    case E_DEAD:   return (u8)(F_DEATH + (e->at / 12 > 2 ? 2 : (u8)(e->at / 12)));
    default:       return F_IDLE;
    }
}

/* size level from the distance: the apparent height follows 1/distance like the walls do */
static u8 level_of(u32 s) {
    if (s >= 227) return 0;
    if (s >= 180) return 1;
    if (s >= 143) return 2;
    if (s >= 114) return 3;
    if (s >= 91)  return 4;
    if (s >= 72)  return 5;
    if (s >= 57)  return 6;
    if (s >= 46)  return 7;
    if (s >= 36)  return 8;
    return 9;
}

/* size level from the distance (apparent height follows 1/distance); hysteresis stops flicker */
static u8 enemy_level(u8 type, s16 f, u8 cur) {
    u32 s = enemy_kc[type] / (u32)(f < 64 ? 64 : f);
    u8 l = level_of(s), l2;
    if (cur == 255 || l == cur) return l;
    if (l > cur) { l2 = level_of(s * 112 / 100); return l2 > cur ? l2 : cur; }
    l2 = level_of(s * 88 / 100);
    return l2 < cur ? l2 : cur;
}

static u8 wp_shown = 255, wp_want, wp_busy, wp_row, wp_cw, wp_tgt;
/* the 2 KB of weapon tiles go to VRAM two tile rows per blank (a long copy used to run past the end of the blank and left old pictures behind) */
static void upload_weapon_step(void) {
    const u8 *src;
    u8 r;
    if (!wp_busy || wp_cw != cur_w || wp_tgt != wp_want) { wp_busy = 1; wp_row = 0; wp_cw = cur_w; wp_tgt = wp_want; }
    src = (wp_tgt >= 8 ? spr_note + (u16)(wp_tgt - 8) * 2048 : bst == 12 ? spr_wire : bst == 10 ? spr_hbar + (u16)wp_tgt * 2048 : cur_w < 4 ? spr_weapon + ((u16)cur_w * 3 + wp_tgt) * 2048 : spr_weapon2 + ((u16)(cur_w - 4) * 3 + wp_tgt) * 2048);
    { u8 st = (u8)(bst == 10 || wp_tgt >= 8 ? 8 : 2);                        /* the handlebars change in ONE blank: no half-written picture, no flicker */
    for (r = wp_row; r < wp_row + st; r++) dmaCopyVram(src + r * 256, (u16)(VRAM_OBJ0 + 2048 + r * 256), 256);
    wp_row = (u8)(wp_row + st); }
    if (wp_row >= 8) { wp_shown = wp_tgt; wp_busy = 0; }
}

static void cg_view(const u16 *p) {                    /* the 112 view colours go to the CGRAM slots the HUD and the sprites leave free */
    u8 i;
    for (i = 0; i < VRUN_N; i++) dmaCopyCGram((const u8 *)(p + vruns[i][0]), vruns[i][1], (u16)(vruns[i][2] * 2));
}
static void set_bg_pal(u8 v) {
    if (v != pal_cur) { cg_view(bg_pal[v]); pal_cur = v; }
}

static void set_obj_pal(u8 n, const u16 *src) {
    u8 i;
    for (i = 0; i < 16; i++) obj_cur[n * 16 + i] = src[i];
    obj_dirty = 1;
}

/* the pad is looked at on EVERY frame by the NMI (the main loop needs several frames per round, and a button tap shorter than a round used to be lost) */
static volatile u16 pad_cur, pad_edge, pad_prev;
static void pad_tick(void) {
    u16 c;
    while (*(volatile u8 *)0x4212 & 1) {}                /* auto-read still running */
    c = (u16)(*(volatile u8 *)0x4218 | ((u16)*(volatile u8 *)0x4219 << 8));
    pad_edge |= (u16)(c & ~pad_prev);
    pad_prev = c;
    pad_cur = c;
}



/* ---------------------------------------------------------------- projections */
static s16 g_horizon, g_cam_h8;
#define BIG_F 640
#define BIG_OK(n) (big_shown == (n) && big_want == (n) && !big_rep)     /* the block's tiles are really in VRAM only after its repeated writes */
static u8 big_want = 255, big_shown = 255, big_cand = 255, look_vis, big_rep;      /* big frame 23 = the four alien glyphs of the LOOK wall */
static s16 big_cand_f;

/* world -> screen. returns 1 if in front of the camera and inside the view */
static u8 project(s16 wx, s16 wy, s16 *sx, s16 *sfeet, s16 *f) {
    s32 dx = wx - px, dy = wy - py;
    s32 fw = (dx * COSA(pang) + dy * SINA(pang)) >> 8;
    s32 rt = (dy * COSA(pang) - dx * SINA(pang)) >> 8;
    if (fw < 100 || fw > 7000) return 0;
    if (rt > fw * 2 || rt < -fw * 2) return 0;
    *f = (s16)fw;
    *sx = (s16)(128 + (rt * 128) / fw);
    *sfeet = (s16)(g_horizon + ((s32)g_cam_h8 * 128) / fw);
    return 1;
}

static u8 col_visible(s16 sx, s16 f) {
    s16 c, da = (s16)((pang - zb_ang) & 511);
    if (da > 256) da -= 512;
    c = (s16)((sx - VIEW_XS + (s16)((da * 25) >> 4)) >> 1);     /* the stored distances belong to an older camera angle */
    if (sx < VIEW_XS || sx >= VIEW_XS + VIEW_W) return 0;
    if (c < 0) c = 0;
    if (c > VIEW_COLS - 1) c = VIEW_COLS - 1;
    return (s16)zbuf_w[c] > f;
}

/* ---------------------------------------------------------------- effects */
static void add_fx(s16 x, s16 y, u8 kind) {
    u8 i;
    for (i = 0; i < MAXFX; i++) {
        if (!fxs[i].on) {
            fxs[i].on = 1; fxs[i].x = x; fxs[i].y = y; fxs[i].kind = kind;
            fxs[i].f = 0; fxs[i].t = 0;
            return;
        }
    }
}

static u8 g_miss;
static void add_bolt(s16 x, s16 y, s16 vx, s16 vy, u8 kind) {
    u8 i;
    for (i = 0; i < MAXB; i++) {
        if (!bolts[i].on) {
            bolts[i].on = 1; bolts[i].x = x; bolts[i].y = y;
            bolts[i].vx = vx; bolts[i].vy = vy; bolts[i].kind = kind;
            bolts[i].life = 120; bolts[i].miss = g_miss;
            return;
        }
    }
}

/* unit vector * speed from (x0,y0) towards (x1,y1), with an aiming error */
static void aim(s16 x0, s16 y0, s16 x1, s16 y1, s16 speed, s16 err, s16 *vx, s16 *vy) {
    s16 dx = x1 - x0, dy = y1 - y0;
    u16 d = dist2d(dx, dy);
    s16 ux, uy, e;
    if (d == 0) d = 1;
    ux = (s16)(((s32)dx * speed) / d);
    uy = (s16)(((s32)dy * speed) / d);
    e = (s16)((rngNext() % (2 * err + 1)) - err);
    *vx = ux - (s16)(((s32)uy * e) >> 8);
    *vy = uy + (s16)(((s32)ux * e) >> 8);
}

/* ---------------------------------------------------------------- player damage */
#define DEATH_END 130
static u8 dth_tab[24], dth_on;
static u8 cheat_i, cheat2_i, ud_t, lr_t, lr_done;
static u16 white_t;
#ifdef DBG_GOD
static u8 player_god = 1;
#else
static u8 player_god = 0;
#endif
//      /* 1 = the player cannot die */

static void set_banner(const char *t) { banner_txt = t; banner_t = 120; }
static void check_life(void) {                        /* a medkit refills the whole bar when one cell is left; none left: the player dies (respawn) */
    if (player_god || dead) return;
    if (php <= 10 && medkits) { medkits--; php = PLAYER_HP; set_banner("MEDKIT USED"); sfx(S_MEDKIT); }
    if (php <= 0) {
        php = 0;
        dead = 1;
        dead_t = 0;
        sfx(S_DEATH);
    }
}

static void hurt_player(s16 dmg) {
    if (dead) return;
    sfx(S_HURT);
    if (!player_god) php -= dmg;
    hurt_t = 14;
    shake = 4;
    check_life();
}

static s16 near_r(u8 type) { return (s16)(enemy_kc[type] / 340 + 8); }   /* closer than this the 64px sprite is at its biggest */

/* ---------------------------------------------------------------- enemies */
static void rush_sync(u8 r);
static u8 spawn_enemy(s16 x, s16 y, u8 type, u8 sp) {
    u8 i;
    for (i = 0; i < MAXE; i++) {
        if (en[i].st == E_NONE) {
            en[i].x = x; en[i].y = y; en[i].z = 0; en[i].vz = 0; en[i].sp = sp; en[i].flee = 0;
            en[i].type = type; en[i].hp = sq_n[type]; en[i].st = E_TELE; en[i].t = 44; en[i].at = 0;
            en[i].shotcd = 100; en[i].dir = 1; en[i].fired = 0;
            en[i].rush = 0; en[i].ph = 0; en[i].pt = (u16)(type == 0 ? 600 : 0); en[i].seen = 0; en[i].fire_t = 0; en[i].burst = 0; en[i].bt = 0; en[i].hid = 0; en[i].mark = (s16)sq_n[type];
            en[i].shown_f = 255; en[i].shown_v = 255; en[i].up = 0; en_act[i] = 1; en[i].gm = 0; en[i].fk = 255;
            set_obj_pal(i ? 4 : 0, enemy_pal_data[type]);
            if (type != 0 && type != 6) {
                if (spawn_room[sp] == 255) en[i].rush = (u8)((rngNext() % 3) == 0);
                else { en[i].rush = 0; rush_sync(spawn_room[sp]); }
            }
            return (u8)(i + 1);
        }
    }
    return 0;
}

static void enemy_move(Enemy *e, s16 vx, s16 vy) {
    s16 nx = e->x + vx, ny = e->y + vy;
    u8 in = blocked(e->x, e->y, 170);                /* already in the wall margin: only the old, narrow rule applies, so it can walk out */
    if (!blocked(nx, e->y, 170) || (in && !blocked(nx, e->y, 92))) e->x = nx;          /* keeps the body 120 units from walls and door textures (no half-drawn sprites) */
    if (!blocked(e->x, ny, 170) || (in && !blocked(e->x, ny, 92))) e->y = ny;
}

static void boom(s16 x, s16 y) { xp_t = 10; xp_x = x; xp_y = y; sfx(S_EDIE); }

/* an enemy beam (beamchain) or rail line: a white line on screen from the enemy toward the camera, a hitscan; the rail also cracks the camera */
static void atk_beam(Enemy *e, s16 dmg, u8 rail) {
    s16 sx, sfeet, f;
    sfx(rail ? S_RAIL : S_BEAM);
    bm_t = 10; bm_x = 8; bm_y = 56;
    if (project(e->x, e->y, &sx, &sfeet, &f) && f > 0) {
        s16 y = (s16)(g_horizon + (s16)(((s32)g_cam_h8 * 128 - 44 * 128) / f));
        if (sx < 8) sx = 8; if (sx > 247) sx = 247;
        if (y < 8) y = 8; if (y > 110) y = 110;
        bm_x = (u8)sx; bm_y = (u8)y;
    }
    if (g_miss) { u8 r = (u8)rngNext(); bm_tx = (u8)((r & 1) ? 190 + (r % 40) : 66 - (r % 40)); bm_ty = (u8)(50 + (r % 30)); return; }
    bm_tx = 128; bm_ty = 62;
    hurt_player(dmg);
    if (rail && !dead && !cr_t) { cr_t = 60; cr_x = (u8)(72 + (rngNext() & 111)); cr_y = (u8)(40 + (rngNext() & 31)); }
}

static void enemy_fire(Enemy *e) {
    s16 vx, vy;
    if (pl_shot_t) return;                               /* the hero fired first / at the same time: no damage */
    g_miss = (u8)((rngNext() % 10) < 3);                 /* about 7 of 10 shots are on target */
    sfx(S_ESHOT);
    if (e->type == 0 && (bst == 0 || bst == 4) && !e->ph && e->pt) {   /* shielded: three plasma balls at once, left of the hero, right of him and straight at the camera */
        s16 ox = (s16)((-SINA(pang) * 440) >> 8), oy = (s16)((COSA(pang) * 440) >> 8);
        sfx(S_PLASMA); g_miss = 0;
        aim(e->x, e->y, px, py, 20, 0, &vx, &vy); add_bolt(e->x, e->y, vx, vy, 1);
        aim(e->x, e->y, (s16)(px + ox), (s16)(py + oy), 20, 0, &vx, &vy); add_bolt(e->x, e->y, vx, vy, 1);
        aim(e->x, e->y, (s16)(px - ox), (s16)(py - oy), 20, 0, &vx, &vy); add_bolt(e->x, e->y, vx, vy, 1);
        return;
    }
    if (e->type == 0) {                                  /* the penguin boss: plasma, rockets, laser */
        u8 k = (u8)(rngNext() % 3), n;
        if (k == 0) { aim(e->x, e->y, px, py, 24, 8, &vx, &vy); add_bolt(e->x, e->y, vx, vy, 1); }
        else if (k == 1) { aim(e->x, e->y, px, py, 15, 4, &vx, &vy); add_bolt(e->x, e->y, vx, vy, 1);
                           aim(e->x, e->y, px, py, 15, 90, &vx, &vy); add_bolt(e->x, e->y, vx, vy, 1); }
        else { aim(e->x, e->y, px, py, 50, 2, &vx, &vy);
               for (n = 0; n < 2; n++) add_bolt((s16)(e->x - (vx * n) / 2), (s16)(e->y - (vy * n) / 2), vx, vy, 0); }
        return;
    }
    {
        u8 n = (u8)(e->type < 4 ? e->type : 4), w = (u8)(rngNext() % n), kd = 0;       /* class 1: quanton; 2: + plasmagun; 3: + rocket; 4+: + beamchain */
        s16 spd = 34;
        if (w == 3 && (rngNext() & 1)) { atk_beam(e, 8, 0); return; }
        if (w == 3) w = 0;
        if (w == 1) { kd = 1; spd = 22; sfx(S_PLASMA); }
        else if (w == 2) { kd = 2; spd = 46; sfx(S_ROCKET); }        /* the enemy rocket is fast: a miss flies past, a hit is a red flash */
        aim(e->x, e->y, px, py, spd, 0, &vx, &vy);
        if (g_miss) {                                        /* misses sail past the crosshair */
            s16 er = (s16)(70 + (rngNext() & 31)), ox;
            if (rngNext() & 1) er = (s16)-er;
            ox = vx;
            vx = (s16)(vx - (s16)(((s32)vy * er) >> 8));
            vy = (s16)(vy + (s16)(((s32)ox * er) >> 8));
        }
        add_bolt(e->x, e->y, vx, vy, kd);
    }
}

__attribute__((unused)) static u8 enemy_low(const Enemy *e) { return (u8)(e->hp * 5 <= (s16)sq_n[e->type] * 2); }

static u8 out_of_view(const Enemy *e) {
    s16 sx, sfeet, f;
    if (!project(e->x, e->y, &sx, &sfeet, &f)) return 1;
    return (u8)(sx < 6 || sx > 250);
}

#define RUN_SP 20                                   /* 4 x the walking speed (5) */

static void enemy_think(Enemy *e) {
    s16 dx = px - e->x, dy = py - e->y;
    u16 d = dist2d(dx, dy);
    u8 seen = los(e->x, e->y, px, py);
    u16 r = rngNext() & 127;
    e->at = 0;
    e->fired = 0;
    e->seen = seen;
    if (e->rush) { if (e->pt) { e->st = E_IDLE; e->t = 10; return; } e->st = E_RUN; e->t = 24; return; }       /* the rusher runs up for a melee blow */
    if (!seen) { e->st = d > 1280 ? E_RUN : E_WALK; e->t = 30; return; }
    if (d > 2048) { e->st = E_RUN; e->t = 28; return; }
    if (r < 56) { e->st = E_STRAFE; e->dir = (u8)(rngNext() & 1); e->t = 34; return; }
    if (r < 100) { e->st = E_SHOOT; e->t = 22; return; }
    e->st = d < 700 ? E_WALK : E_IDLE;
    e->t = 20;
}

/* the boss after losing 2 squares: freezes 2 s, chases the hero 4 s, returns to its spawn point, then shoots again */
static void boss_phase(Enemy *e, u16 dt) {
    s16 tx, ty, dx, dy, ux, uy;
    u16 d, i;
    if (e->ph == 1) {
        e->st = E_IDLE;
        if (e->pt > dt) e->pt -= dt; else { e->ph = 2; e->pt = 240; }
        return;
    }
    if (e->ph == 2) { tx = px; ty = py; } else { tx = spawn_pos[e->sp][0]; ty = spawn_pos[e->sp][1]; }
    dx = tx - e->x; dy = ty - e->y;
    d = dist2d(dx, dy);
    if (d == 0) d = 1;
    ux = (s16)(((s32)dx * 256) / d); uy = (s16)(((s32)dy * 256) / d);
    e->st = E_RUN;
    for (i = 0; i < dt; i++)
        if ((e->ph == 2 && d > 380) || (e->ph == 3 && d > 200)) enemy_move(e, (s16)((ux * 14) >> 8), (s16)((uy * 14) >> 8));
    if (e->ph == 2 && dist2d(px - e->x, py - e->y) < (u16)(near_r(e->type) + 70) && e->shotcd == 0) { hurt_player(20); e->shotcd = 40; e->fire_t = 14; }
    if (e->pt > dt) e->pt -= dt; else e->pt = 0;
    if (e->ph == 2 && e->pt == 0) { e->ph = 3; e->pt = 300; }
    else if (e->ph == 3 && (e->pt == 0 || d <= 200)) { e->ph = 0; e->mark = e->hp; e->st = E_IDLE; e->t = 10; e->shotcd = 90; }
}

#define FIELD_R 420                                          /* the electric field around the boss at the wall */
static const s16 bsp[6][2] = { { 2560, 512 }, { 3584, 1024 }, { 4608, 512 }, { 5632, 1024 }, { 6656, 512 }, { 5632, 448 } };   /* the boss room: teleports and fighters */

static void pick_tp(void) {                                  /* the next teleport: 0.8..1.7 tiles of running away, 3 s of life */
    u8 k, j = 0, n = (u8)bw_cd;
    for (k = 0; k < 6; k++) {
        u8 c = (u8)((n * 5 + k) % 6);
        u16 d = dist2d(px - bsp[c][0], py - bsp[c][1]);
        if (d > 800 && d < 1700) { j = c; break; }
    }
    tp_x = bsp[j][0]; tp_y = bsp[j][1]; tp_on = 1; bst_t = 180;
}

static void bw_clear(void) { u8 k; for (k = 0; k < MAXE; k++) if (en[k].type != 0 && en[k].st != E_NONE) en[k].st = E_NONE; }

static u8 boss_spawn(u8 type, s16 x, s16 y, s16 hp) {
    u8 r = spawn_enemy(x, y, type, 0);
    if (r) { en[r - 1].hp = hp; en[r - 1].mark = hp; }
    return r;
}

static void glyph_barrel(Enemy *e, u16 dt) {          /* the glyph wall: the turned boss rolls a barrel at the hero; it does not come back (a jump or a miss just ends it) */
    u8 i, found = 0;
    for (i = 0; i < MAXE; i++) {
        Enemy *o = &en[i];
        if (o->type == 6 && o->st != E_NONE && o->hp == 99) {
            s16 dx = (s16)(px - o->x), dy = (s16)(py - o->y);
            u16 d = dist2d(dx, dy), mv = (u16)(11 * dt);
            found = 1;
            if (d < 1) d = 1;
            if (mv > d) mv = d;
            o->x = (s16)(o->x + (s16)(((s32)dx * mv) / d)); o->y = (s16)(o->y + (s16)(((s32)dy * mv) / d));
            o->st = E_IDLE; o->z = 0; en_act[i] = 1;
            d = dist2d((s16)(px - o->x), (s16)(py - o->y));
            if (pz > 8 && d <= 200) { o->st = E_NONE; sfx(S_RICO); }
            else if (d <= 100) { hurt_player(20); o->st = E_NONE; }
            else if (blocked(o->x, o->y, 10)) o->st = E_NONE;
        }
    }
    if (!found && e->pt && !e->shotcd) {
        s16 bdx = (s16)(px - e->x), bdy = (s16)(py - e->y);
        u16 bd = dist2d(bdx, bdy);
        u8 r;
        if (bd < 1) bd = 1;
        r = spawn_enemy((s16)(e->x + (s16)(((s32)bdx * 170) / bd)), (s16)(e->y + (s16)(((s32)bdy * 170) / bd)), 6, 0);      /* it starts in front of the boss, not inside its picture */
        if (r) { Enemy *o = &en[r - 1]; o->st = E_IDLE; o->t = 0; o->hp = 99; o->bt = 0; o->fk = 255; o->hid = 0; en_act[r - 1] = 1; e->fire_t = 14; e->shotcd = (u16)(150 + (rngNext() & 63)); sfx(S_ESHOT); }
        else e->shotcd = 20;
    }
}

static void boss_next(Enemy *e) {                    /* the bar is empty: refill it and go on to the next stage */
    e->hp = (s16)sq_n[0]; e->mark = e->hp; e->ph = 0; e->st = E_IDLE; e->t = 10; e->shotcd = 90; e->fire_t = 0;
    bw_on = 0; tp_on = 0; bw_cd = 120; bst_t = 120;
    sfx(S_PICK);
    if (medkits < 5) medkits++;
    if (bst == 0) {                                                                /* 5 lives: it falls and gets up; the last time three fighters of class 6 come one after another */
        bw_y++; bw_clear(); sfx(S_EDIE); shake = 10;
        if (bw_y < 5) { bst = 7; bst_t = 0; }
        else { u8 q; for (q = 0; q < MAXE; q++) if (en[q].type != 0) en[q].st = E_NONE; bst = 8; bst_t = 0; bw_x = 0; bw_cd = 150; wv_k = 0; set_banner("3 FIGHTERS"); }
    }
    else if (bst == 1) {                                                           /* the barrels did their work: flash, "Run run run", the chase */
        u8 q; for (q = 0; q < MAXE; q++) if (en[q].type == 6) en[q].st = E_NONE;
        bw_y = 0; bw_x = 0; bst = 9; bst_t = 0; sfx(S_EDIE);                         /* the cut-scene: the boss opens a teleport and jumps in, the hero runs after it, a flash */
        dev_on &= (u8)~1; have_d &= (u8)~1; nv_gone = 1; cur_d = (u8)((have_d & 2) ? 1 : 2);        /* the nightvisor is switched off and taken away after the barrels */
    }
}

/* the cut-scene before the chase (bst 9): phases in bw_y: 0 the boss opens a teleport, 1 it runs in, 2 the hero runs after it, 3 flash,
   4 the road: the boss walks to its bike, 5 it rides off while the hero turns his head, 6 another bike appears on the right, 7 the black card */
static void intro_scene(Enemy *e, u16 dt) {
    Enemy *o = &en[(u8)((e - en) ^ 1)];
    if (e->hid == 7) { e->st = E_IDLE; return; }                     /* the parked bike is a prop */
    e->st = E_IDLE; bw_on = 0; e->fire_t = 0; e->bt = 0;
    bst_t = (u16)(bst_t + dt);
    if (bw_y == 0) {
        tp_x = (s16)(e->x < 4600 ? e->x + 500 : e->x - 500); tp_y = (s16)(e->y + 300); tp_on = 1; sfx(S_SPAWN); bw_y = 1; bst_t = 0;
    } else if (bw_y == 1) {                                          /* it runs into the teleport and is gone */
        s16 dx = (s16)(tp_x - e->x), dy = (s16)(tp_y - e->y);
        u16 dd = dist2d(dx, dy), mv = (u16)(9 * dt);
        if (dd > 60) { if (mv > dd) mv = dd; e->x = (s16)(e->x + (s16)(((s32)dx * mv) / dd)); e->y = (s16)(e->y + (s16)(((s32)dy * mv) / dd)); }
        else { add_fx(e->x, e->y, 2); add_fx(e->x, e->y, 0); sfx(S_EDIE); e->x = 128; e->y = 128; bw_y = 2; bst_t = 0; }
    } else if (bw_y == 2) {                                          /* the hero arrives (he runs by himself, see scene_player) */
        if (dist2d((s16)(px - tp_x), (s16)(py - tp_y)) < 140 || bst_t > 420) { bw_y = 3; cs_ph = 1; cs_n = 0; sfx(S_SPAWN); }
    } else if (bw_y == 4) {                                          /* it walks to the bike on its right */
        if (bst_t < 40) return;
        if (e->x < (s16)(o->x - 40)) e->x = (s16)(e->x + 4 * (s16)dt);
        else { o->st = E_NONE; add_fx(o->x, o->y, 0); sfx(S_PICK); bw_y = 5; bst_t = 0; }
    } else if (bw_y == 5 || bw_y == 6) {                             /* it sits down and rides off up the road */
        e->y = (s16)(e->y - 12 * (s16)dt);
        if (bw_y == 5 && bst_t >= 25) {                              /* the hero looks right: another bike stands there */
            u8 r = spawn_enemy(4400, (s16)(py - 150), 0, 0);
            if (r) { en[r - 1].hid = 7; en[r - 1].pt = 0; en[r - 1].shown_f = 255; en[r - 1].shown_v = 255; en[r - 1].fk = 255; }
            bw_y = 6;
        } else if (bw_y == 6 && bst_t >= 120) { u8 q; for (q = 0; q < MAXE; q++) if (en[q].type == 0 && en[q].hid == 7) en[q].st = E_NONE; bw_y = 7; cs_ph = 2; cs_n = 0; cs_w = 2; }
    }
}


/* ---- the last fight (bst 4): the shield, the barrels, the attack round, the crystal timer.
   tv_buf[58] = shield charges, [59] = step of the attack round, [60] = shots of that step, [61] = 0x5A once set up.
   The boss's e->pt is only the "shielded" flag here (the old 10 s shield is gone), e->shotcd is the pause between the shots. */
static void damage_enemy(u8 i, s16 dmg);
static void final_init(Enemy *e) {
    u8 q;
    for (q = 0; q < MAXE; q++) if (en[q].type == 6 && en[q].hp == 99) en[q].st = E_NONE;
    e->hp = (s16)sq_n[0]; e->mark = e->hp; e->ph = 0; e->pt = 600; e->shotcd = 90; e->st = E_IDLE;
    tv_buf[58] = 5; tv_buf[59] = 0; tv_buf[60] = 0; tv_buf[61] = 0x5A;
    bw_on = 0; bw_cd = 90;
    set_banner("SHOOT THE BARRELS");
}

static void enemy_shot(Enemy *e, u8 kd, s16 spd) {            /* one bolt at the hero, about 3 of 10 sail past */
    s16 vx, vy;
    g_miss = (u8)((rngNext() % 10) < 3);
    aim(e->x, e->y, px, py, spd, 0, &vx, &vy);
    if (g_miss) { s16 er = (s16)(70 + (rngNext() & 31)), ox = vx; if (rngNext() & 1) er = (s16)-er; vx = (s16)(vx - (s16)(((s32)vy * er) >> 8)); vy = (s16)(vy + (s16)(((s32)ox * er) >> 8)); }
    add_bolt(e->x, e->y, vx, vy, kd);
}

static void final_barrel(Enemy *e, u16 dt) {                   /* it rolls a barrel at the hero; a jump sends it back; a shot at it near the boss costs one charge of the shield */
    Enemy *ob = &en[(u8)((e - en) ^ 1)];
    if (!bw_on) {
        if (ob->type == 6 && ob->hp == 99) ob->st = E_NONE;                    /* the spent barrel goes away (it used to stay frozen and block the next one) */
        if (ob->st != E_NONE) return;      /* the other place is taken by a fighter: no barrels until it is dead */
        if (bw_cd > dt) bw_cd -= dt;
        else {
            u8 r = spawn_enemy(e->x, e->y, 6, 0);
            if (r) { bw_cd = 0; bw_on = 1; bw_x = e->x; bw_y = e->y; e->fire_t = 14; sfx(S_ESHOT); ob = &en[r - 1]; ob->st = E_IDLE; ob->t = 0; ob->hp = 99; ob->bt = 0; ob->fk = 255; ob->hid = 0; en_act[r - 1] = 1; }
            else bw_cd = 20;
        }
    } else {
        s16 tx = (bw_on == 1) ? px : e->x, ty = (bw_on == 1) ? py : e->y, dx = tx - bw_x, dy = ty - bw_y;
        u16 d = dist2d(dx, dy), mv = (u16)(11 * dt);
        if (d < 1) d = 1;
        if (mv > d) mv = d;
        bw_x += (s16)(((s32)dx * mv) / d); bw_y += (s16)(((s32)dy * mv) / d);
        d = dist2d(tx - bw_x, ty - bw_y);
        if (bw_on == 1) {
            if (pz > 8 && d <= 200) { bw_on = 2; sfx(S_RICO); }
            else if (d <= 100) { hurt_player(20); bw_on = 0; bw_cd = 120; }
        } else if (d <= 120) { sfx(S_RICO); bw_on = 0; bw_cd = 120; }       /* nobody shot it: the boss takes nothing */
        if (bw_on && blocked(bw_x, bw_y, 10)) { bw_on = 0; bw_cd = 60; }
        if (ob->type == 6 && ob->st != E_NONE) { ob->x = bw_x; ob->y = bw_y; ob->z = 0; ob->st = E_IDLE; en_act[(u8)(ob - en)] = 1; }
        else bw_on = 0;
    }
}

static void final_attack(Enemy *e) {                           /* the shield is down: 2 railgun shots, 4 rockets, plasma, then it runs up to hit the hero */
    u8 sub = tv_buf[59], cnt = tv_buf[60];
    if (e->ph || e->shotcd || dead) return;
    e->fire_t = 14;
    if (sub == 0) { g_miss = (u8)((rngNext() % 10) < 3); atk_beam(e, 15, 1); e->shotcd = 110; if (++cnt >= 2) { sub = 1; cnt = 0; e->shotcd = 100; } }
    else if (sub == 1) { sfx(S_ROCKET); enemy_shot(e, 2, 46); e->shotcd = 45; if (++cnt >= 4) { sub = 2; cnt = 0; e->shotcd = 100; } }
    else if (sub == 2) {
        s16 vx, vy, ox = (s16)((-SINA(pang) * 440) >> 8), oy = (s16)((COSA(pang) * 440) >> 8);
        sfx(S_PLASMA); g_miss = 0;
        aim(e->x, e->y, px, py, 20, 0, &vx, &vy); add_bolt(e->x, e->y, vx, vy, 1);
        aim(e->x, e->y, (s16)(px + ox), (s16)(py + oy), 20, 0, &vx, &vy); add_bolt(e->x, e->y, vx, vy, 1);
        aim(e->x, e->y, (s16)(px - ox), (s16)(py - oy), 20, 0, &vx, &vy); add_bolt(e->x, e->y, vx, vy, 1);
        sub = 3; cnt = 0; e->shotcd = 90;
    }
    else { e->ph = 2; e->pt = 240; sub = 0; cnt = 0; e->shotcd = 140; }
    tv_buf[59] = sub; tv_buf[60] = cnt;
}

static void final_fight(Enemy *e, u16 dt) {
    if (tv_buf[61] != 0x5A) final_init(e);
    if (CSP || dead) { e->st = E_IDLE; return; }
#ifdef DBG_F4
    { u8 q; for (q = 0; q < MAXE; q++) if (en[q].type == 6 && en[q].hp == 99 && en[q].st != E_NONE) {
        if (bw_on == 1 && dist2d((s16)(px - en[q].x), (s16)(py - en[q].y)) < 400) pz = 20;
        if (bw_on == 2 && dist2d((s16)(e->x - en[q].x), (s16)(e->y - en[q].y)) < 900) damage_enemy(q, 1);
    } }
    if (!tv_buf[58] && last_fc > 2600 && (last_fc % 90) == 0) damage_enemy((u8)(e - en), 4);
#endif
    if (e->ph) { boss_phase(e, dt); return; }
    if (e->st == E_HURT && e->t) e->t = (u16)(e->t > dt ? e->t - dt : 0); else e->st = E_IDLE;
    if (tv_buf[58]) { e->pt = 600; final_barrel(e, dt); }
    else { e->pt = 0; final_attack(e); }
}

static void final_retry(Enemy *e) {                                   /* the 20 s are over: life and shield are back and a class 5 fighter joins; everything starts again */
    u8 j = (u8)(rngNext() % 6);
    bst = 4; bcry_on = 0; have_d &= (u8)~4; dev_on &= (u8)~4; vis_t[2] = 0; cls_sel = 0; cur_d = (u8)((have_d & 1) ? 0 : 2);
    final_init(e);
    boss_spawn(5, bsp[j][0], bsp[j][1], (s16)sq_n[5]);
    set_banner("SHIELD IS BACK");
}

static void glyph_done(Enemy *e) {                   /* KEY DECRYPT is full: the wires minigame */
    bw_clear(); bst = 12; wire_init(); wp_shown = 255; wp_busy = 0; wp_pal_dirty = 0; e->pt = 0; e->st = E_IDLE;
    bw_x = 0; bw_y = 0; bst_t = 0; sfx(S_PICK); set_banner("CONNECT THE WIRES"); cs_ph = 2; cs_n = 0; cs_w = 6;
}

static void arrival_boss(Enemy *e, u16 dt) {
    u8 q, have = 0;
    if (e->hid == 7) { e->st = E_IDLE; return; }                  /* the bike it left standing */
    bw_on = 0; e->st = E_IDLE; e->hid = 0;
    bst_t = (u16)(bst_t + dt);
    if (bst_t < ARR_T0) { e->x = (s16)(BK_X - 480 + (s16)((80L * bst_t) / ARR_T0)); e->y = 440; }          /* 120 px to the left of the wall, it comes 20 closer */
    else if (bst_t < ARR_T1) {
        u16 t = (u16)(bst_t - ARR_T0);
        for (q = 0; q < MAXE; q++) if (en[q].type == 0 && en[q].hid == 7 && en[q].st != E_NONE) have = 1;
        if (!have) {                                              /* it puts the bike down in front of itself and runs on foot */
            u8 r = spawn_enemy((s16)(BK_X - 400), 640, 0, 0);
            if (r) { en[r - 1].hid = 7; en[r - 1].pt = 0; en[r - 1].shown_f = 255; en[r - 1].shown_v = 255; en[r - 1].fk = 255; add_fx((s16)(BK_X - 400), 640, 0); sfx(S_PICK); }
            e->shown_f = 255; gs_invalidate(); obj_dirty = 1;
        }
        e->x = (s16)(BK_X - 400 + (s16)((400L * t) / (ARR_T1 - ARR_T0))); e->y = (s16)(440 - (s16)((40L * t) / (ARR_T1 - ARR_T0)));
    } else { e->x = BK_X; e->y = 400; }
}
static void boss_stage(Enemy *e, u16 dt) {
#ifdef DBG_FIX
    if ((bst == 2 || bst == 3) && e->x != BK_X) { e->x = BK_X; e->y = 400; }
#endif
    if (bst == 12) { e->st = E_IDLE; bw_on = 0; return; }
    if (bst == 4) { final_fight(e, dt); return; }
    if (bst == 5) { if (!CSP) { bst_t = (u16)(bst_t + dt); if (bst_t >= 1200) { final_retry(e); return; } } e->st = E_IDLE; e->hp = 2; e->mark = 2; bw_on = 0; if (!(last_fc & 3)) add_fx((s16)(e->x + (s16)((rngNext() & 255) - 128)), (s16)(e->y + (s16)((rngNext() & 255) - 128)), 0); return; }   /* the blue aura */
    if (bst == 11) {                                         /* the last scene: it spins, opens a teleport, steps into it and is gone; then the exit teleport opens */
        if (CSP) return;
        e->st = E_IDLE; bw_on = 0; e->hp = 2;
        if (bst_t >= 40) {                                   /* it has opened the exit teleport and runs into it */
            s16 ex = (s16)((EXIT_X0 + EXIT_X1) / 2), ey = (s16)((EXIT_Y0 + EXIT_Y1) / 2), dx = (s16)(ex - e->x), dy = (s16)(ey - e->y);
            u16 dd = dist2d(dx, dy), mv = (u16)(14 * dt);
            if (dd > 120) { if (mv > dd) mv = dd; e->x = (s16)(e->x + (s16)(((s32)dx * mv) / dd)); e->y = (s16)(e->y + (s16)(((s32)dy * mv) / dd)); }
            else { add_fx(e->x, e->y, 2); add_fx(e->x, e->y, 0); e->st = E_NONE; sfx(S_EDIE); }
        }
        return;
    }
    if (bst == 7) {                                          /* gets up after the fall */
        e->st = E_IDLE; bw_on = 0;
        bst_t = (u16)(bst_t + dt);
#ifdef DBG_BT
        bst_t = DBG_BT;
#endif
        if (bst_t >= 230) { bst = (u8)(bw_y >= 5 ? 1 : 0); e->hp = (s16)sq_n[0]; e->mark = e->hp; e->ph = 0; e->shotcd = 60; e->t = 10; e->pt = (u16)(bst ? 300 : 600); if (bst) { u8 q; for (q = 0; q < MAXE; q++) if (en[q].type != 0) en[q].st = E_NONE; } if (bst) { bw_on = 0; bw_cd = 90; cs_ph = 2; cs_n = 0; cs_w = 4; } }
        return;
    }
    if (bst == 8) { e->st = E_IDLE; bw_on = 0; return; }
    if (bst == 9) { intro_scene(e, dt); return; }
    if (bst == 13) { arrival_boss(e, dt); return; }       /* the arrival at the glyph wall */
    if (bst == 1 && e->st == E_HURT && e->t) e->t = (u16)(e->t > dt ? e->t - dt : 0);      /* the screaming sprite for a moment */
    else e->st = E_IDLE;
    if (bst == 2) {                                          /* the teleport race: 5 teleports, 3 s each */
        e->x = BK_X; e->y = 400; bw_on = 0;
        if (!tp_on || dead) return;
        if (dist2d(px - tp_x, py - tp_y) < 220) {
            sfx(S_PICK); flash_t = 3; bw_cd++;
            if (bw_cd >= 5) { tp_on = 0; bst = 3; bw_x = 0; bw_y = 0; wv_k = 0; bst_t = 1800; bw_cd = 0; e->pt = 0; set_banner("WEAR OPTOVISOR"); }
            else pick_tp();
        } else if (bst_t > dt) bst_t -= dt;
        else { hurt_player(20); pick_tp(); }
        return;
    }
    if (bst == 3) {                                          /* the boss at the glyph wall, an electric field round it; the optovisor starts the 30 s decryption */
        u16 d = dist2d(px - e->x, py - e->y);
        if (bw_y != 3) { e->x = BK_X; e->y = 400; }
        bw_on = 0;
        if (dead) return;
        if (bw_y == 3) {                                     /* the key is decrypted: the boss runs off to the left, the monitor shows 0 and 1 */
            if (e->x > BK_X - 700) e->x = (s16)(e->x - 7 * (s16)dt);
            else { add_fx(e->x, e->y, 2); add_fx(e->x, e->y, 0); sfx(S_EDIE); e->x = 128; e->y = 128; glyph_done(e); }
            return;
        }
        if (e->bt > dt) e->bt = (u8)(e->bt - dt); else e->bt = 0;
        if (!(last_fc & 3)) { s16 a = (s16)(rngNext() & 511); add_fx((s16)(e->x + (s16)(((s32)COSA(a) * FIELD_R) >> 8)), (s16)(e->y + (s16)(((s32)SINA(a) * FIELD_R) >> 8)), 2); }
        if (d < FIELD_R && !e->bt) {                         /* stepping onto the field takes half of the life bar */
            s16 ux = (s16)(((s32)(px - e->x) * 256) / (d ? d : 1)), uy = (s16)(((s32)(py - e->y) * 256) / (d ? d : 1));
            s16 nx = (s16)(e->x + (s16)(((s32)ux * (FIELD_R + 40)) >> 8)), ny = (s16)(e->y + (s16)(((s32)uy * (FIELD_R + 40)) >> 8));
            e->bt = 60; hurt_player(PLAYER_HP / 2); add_fx(px, py, 0);
            if (!blocked(nx, ny, 40)) { px = nx; py = ny; }
        }
        glyph_barrel(e, dt);                                 /* the boss rolls barrels whenever it is turned round */
        if (bw_y == 2) e->pt = 90;                           /* a fighter is out: the boss faces the hero and keeps rolling barrels */
        else if (pl_shot_t) e->pt = 90;                      /* shots at it do nothing: it turns, rolls a barrel and stands with its back again */
        if (e->pt && bw_y != 2) e->pt = (u16)(e->pt > dt ? e->pt - dt : 0);
        if (bw_y == 0) {                                     /* waiting for the optovisor */
            if (dev_on & 2) { bw_y = 1; bst_t = 1800; e->pt = 0; set_banner("KEY DECRYPT"); dev_on &= (u8)~2; have_d &= (u8)~2; cur_d = (u8)((have_d & 1) ? 0 : 2); }      /* the optovisor did its job: it is switched off and taken away */
            return;
        }
        if (bw_y == 2) {                                     /* the fighter and the boss share one life: it ends when the fighter is dead */
            u8 q, fl = 0;
            for (q = 0; q < MAXE; q++) if (en[q].type == 6 && en[q].hp != 99 && en[q].st != E_NONE && en[q].st != E_DEAD) fl = 1;
            if (!fl) {
                e->pt = 0; wv_k++; dev_on &= (u8)~2;
                if (wv_k >= 3) { bw_y = 3; set_banner("KEY DECRYPTED"); sfx(S_PICK); }
                else { bw_y = 0; have_d |= 2; cur_d = (u8)((have_d & 1) ? 0 : 2); set_banner("WEAR OPTOVISOR"); }
            }
            return;
        }
        if (d > FIELD_R + 700) return;                       /* too far from the boss: the bar stops and hides */
        bw_x = (s16)(bw_x + (s16)dt);                        /* decrypting: 1 cell per second */
        if (bw_x >= (s16)(240 * (wv_k + 1))) {               /* every 4 cells the boss switches the optovisor off and sends a class 6 fighter; the visors are blocked until it is dead */
            if (boss_spawn(6, bsp[(last_fc >> 4) % 3 * 2][0], bsp[(last_fc >> 4) % 3 * 2][1], 20)) { bw_x = (s16)(240 * (wv_k + 1)); bw_y = 2; sfx(S_SPAWN); set_banner("KILL THE ENEMY"); dev_on = 0; }
        }
        return;
    }
    {                                                        /* the barrel wave: the boss rolls a barrel at the hero (it follows his strafing); a jump sends it back at the boss */
        Enemy *ob = &en[(u8)((e - en) ^ 1)];
        if (!(dev_on & 1)) {                                 /* no nightvisor: the boss is invisible, no barrels, but it shoots the hero */
            if (ob->type == 6) ob->st = E_NONE;
            bw_on = 0; bw_cd = 60;
            if (e->pt > dt) e->pt = (u16)(e->pt - dt); else { set_banner("WEAR NIGHTVISOR"); e->pt = 420; }
            if (e->shotcd > dt) e->shotcd = (u16)(e->shotcd - dt);
            else { enemy_fire(e); e->fire_t = 14; e->shotcd = (u16)(70 + (rngNext() & 31)); }
            return;
        }
        if (!bw_on) {
            if (ob->type == 6) ob->st = E_NONE;
            if (bw_cd > dt) bw_cd -= dt;
            else {
                u8 r = spawn_enemy(e->x, e->y, 6, 0);
                if (r) {
                    bw_cd = 0; bw_on = 1; bw_x = e->x; bw_y = e->y; e->fire_t = 14; sfx(S_ESHOT);
                    ob = &en[r - 1]; ob->st = E_IDLE; ob->t = 0; ob->hp = 99; ob->bt = 0; ob->fk = 255; en_act[r - 1] = 1;
                } else bw_cd = 20;
            }
        } else {
            s16 tx = (bw_on == 1) ? px : e->x, ty = (bw_on == 1) ? py : e->y, dx = tx - bw_x, dy = ty - bw_y;
            u16 d = dist2d(dx, dy), mv = (u16)(11 * dt);
            if (d < 1) d = 1;
            if (mv > d) mv = d;
            bw_x += (s16)(((s32)dx * mv) / d); bw_y += (s16)(((s32)dy * mv) / d);
            d = dist2d(tx - bw_x, ty - bw_y);
            if (bw_on == 1) {
                if (pz > 8 && d <= 200) { bw_on = 2; sfx(S_RICO); }                       /* jumped over it: it rolls back at the boss */
                else if (d <= 100) { hurt_player(20); bw_on = 0; bw_cd = 120; }
            } else if (d <= 120) {
                e->hp -= 4; boom(e->x, e->y); sfx(S_EHIT); bw_on = 0; bw_cd = 120;
                if (e->hp <= 0) { boss_next(e); return; }
                e->st = E_HURT; e->t = 14;
            }
            if (bw_on && blocked(bw_x, bw_y, 10)) { bw_on = 0; bw_cd = 60; }
            if (ob->type == 6 && ob->st != E_NONE) { ob->x = bw_x; ob->y = bw_y; ob->z = 0; ob->st = E_IDLE; en_act[(u8)(ob - en)] = 1; }
        }
    }
}

/* class 6: wherever the hero goes, they blink in next to him */
static void ghost_teleport(Enemy *e) {
    u8 t;
    for (t = 0; t < 10; t++) {
        u16 a = (u16)(rngNext() & 511), rr = (u16)(700 + rngNext() % 600);
        s16 x = (s16)(px + (s16)(((s32)COSA(a) * rr) >> 8)), y = (s16)(py + (s16)(((s32)SINA(a) * rr) >> 8));
        if (!blocked(x, y, 70) && los(x, y, px, py)) { e->x = x; e->y = y; e->st = E_TELE; e->t = 44; e->at = 0; e->fire_t = 0; sfx(S_SPAWN); break; }
    }
    e->pt = (u16)(300 + (rngNext() & 255));
}

static void enemy_update(Enemy *e, u16 dt) {
    if (bst == 10 || IS_BARREL(e)) return;      /* the chase is driven by bike_update; the barrel by the boss */
    s16 dx, dy, ux, uy;
    u16 d, i;
    if (e->st == E_NONE) return;
    e->at += dt;
    if (e->shotcd > dt) e->shotcd -= dt; else e->shotcd = 0;
    if (e->fire_t > dt) e->fire_t -= (u8)dt; else e->fire_t = 0;
    if (e->st == E_DEAD) {
        if (e->t > dt) e->t -= dt; else { e->st = E_NONE; add_fx(e->x, e->y, 2); }
        return;
    }
    if (e->st == E_TELE) {
        if (e->t > dt) e->t -= dt;
        else { e->st = E_IDLE; e->t = 6; add_fx(e->x, e->y, 0); }
        return;
    }
    if (e->type == 0 && (bst == 0 || bst == 4) && !e->ph && e->pt) {          /* the shield: 10 s, flickers cyan */
        u8 si = (u8)(e - en);
        if (bst == 0) e->pt = (u16)(e->pt > dt ? e->pt - dt : 0);
        if (!(last_fc & 7) || !e->pt) {
            if (e->pt && ((last_fc >> 3) & 1)) { u16 sp[16]; u8 q; for (q = 0; q < 16; q++) { u16 c = enemy_pal_data[0][q]; sp[q] = (u16)((((c & 31) >> 1)) | ((((c >> 5) & 31) + (31 - ((c >> 5) & 31)) / 2) << 5) | ((((c >> 10) & 31) + (31 - ((c >> 10) & 31)) / 2) << 10)); } set_obj_pal(si ? 4 : 0, sp); }
            else set_obj_pal(si ? 4 : 0, enemy_pal_data[0]);
        }
    }
    if (e->type == 0 && ((bst >= 1 && bst <= 5) || bst >= 7)) { boss_stage(e, dt); return; }
    if (e->type == 6) {                                   /* ghost: hops next to the hero every few seconds */
        if (!e->ph) { e->ph = 1; e->pt = 240; e->rush = 0; }
        else if (e->pt > dt) e->pt -= dt;
        else { ghost_teleport(e); return; }
    }
    if (e->type != 0 && e->type != 6) {                    /* never let the hero get right up to it: backs away at twice the hero's speed (the sprite art stops growing at 64 px) */
        s16 kx = px - e->x, ky = py - e->y;
        u16 kd = dist2d(kx, ky);
        if (kd < (u16)(near_r(e->type) + 10)) {
            s16 bx, by;
            if (kd == 0) kd = 1;
            bx = (s16)(-(((s32)kx * 28) / (s32)kd)); by = (s16)(-(((s32)ky * 28) / (s32)kd));
            for (i = 0; i < dt; i++) enemy_move(e, bx, by);
            kd = dist2d(px - e->x, py - e->y);
            if (kd < (u16)(near_r(e->type) + 10) && kd > 0) {            /* cornered: then the hero is pushed back instead */
                s16 nx = (s16)(px + (s16)(((s32)(px - e->x) * 30 * dt) / (s32)kd)), ny = (s16)(py + (s16)(((s32)(py - e->y) * 30 * dt) / (s32)kd));
                if (!blocked(nx, py, 80)) px = nx;
                if (!blocked(px, ny, 80)) py = ny;
            }
        }
    }
    if (e->st == E_HURT) {
        if (e->t > dt) e->t -= dt; else e->t = 0;
        if (e->t == 0) {
            if (e->type == 0 && e->ph) e->st = E_IDLE;
            else if (e->flee) { e->flee = 0; e->st = E_FLEE; e->t = 150; e->at = 0; e->fired = 0; e->dir = (u8)(rngNext() & 1); e->hid = 0; }   /* dash out of the camera's view */
            else enemy_think(e);
        }
        return;
    }
    if (e->type == 0 && e->ph) { boss_phase(e, dt); return; }
    if (e->rush && e->pt && e->type != 0 && e->type != 6) {                          /* charging */
        e->st = E_IDLE;
        if (e->pt > dt) { e->pt -= dt; return; }
        e->pt = 0; e->t = 0;
    }
    dx = px - e->x; dy = py - e->y;
    d = dist2d(dx, dy);
    if (d == 0) d = 1;
    ux = (s16)(((s32)dx * 256) / d);
    uy = (s16)(((s32)dy * 256) / d);
    if ((e->at & 3) < dt) e->seen = los(e->x, e->y, px, py);
    for (i = 0; i < dt; i++) {
        s16 sp = 0;
        s16 mvx = 0, mvy = 0;
        if (e->st == E_WALK) sp = 5;
        else if (e->st == E_RUN || e->st == E_FLEE) sp = RUN_SP;
        if (e->st == E_FLEE) {
            s16 sg = e->dir ? 1 : -1;                                   /* sideways relative to the camera, a little away from the hero */
            mvx = (s16)(((-SINA(pang) * sg * sp) >> 8) - ((ux * sp) >> 9));
            mvy = (s16)((( COSA(pang) * sg * sp) >> 8) - ((uy * sp) >> 9));
        } else if (sp) {
            s16 dir = (d < 600) ? -1 : 1;
            if (e->st == E_RUN && e->rush) dir = 1;
            if (d < 420 || d > 420) { mvx = (s16)((ux * sp * dir) >> 8); mvy = (s16)((uy * sp * dir) >> 8); }
            if (e->st == E_RUN && ((e->rush && d < (u16)(near_r(e->type) + 20)) || (!e->rush && d < 560))) { mvx = 0; mvy = 0; }
        } else if (e->st == E_STRAFE) {
            s16 sg = e->dir ? 5 : -5;
            mvx = (s16)((-uy * sg) >> 8);
            mvy = (s16)((ux * sg) >> 8);
        }
        if (mvx || mvy) enemy_move(e, mvx, mvy);
    }
    if (e->st == E_FLEE && ((e->at & 3) < dt) && e->at > 12 && out_of_view(e)) {      /* out of sight: stand there and shoot */
        e->st = E_SHOOT; e->t = 150; e->at = 0; e->hid = 1; e->fired = 0;
    }
    if (e->rush) {                                                     /* melee blow */
        if (!dead && d < (u16)(near_r(e->type) + 70) && e->shotcd == 0 && (e->st == E_RUN || e->st == E_IDLE)) {
            hurt_player(16); e->shotcd = 24; e->fire_t = 14; e->pt = 180; e->st = E_IDLE; e->t = 10;   /* after the touch it stands still for 3 s to "charge", then comes again */
        }
    } else if (e->shotcd == 0 && e->seen && e->at >= 6 && e->type != 5 && e->type != 6 &&
               (e->st == E_IDLE || e->st == E_WALK || e->st == E_STRAFE || e->st == E_SHOOT)) {     /* shoots all the time */
        if (e->type == 5) { e->burst = 3; e->bt = 0; e->shotcd = 100; }
        else { enemy_fire(e); e->fire_t = 14; e->shotcd = (u16)(e->type == 0 ? (e->pt && (bst == 0 || bst == 4) ? 120 : 120 + (rngNext() & 63)) : 36 + (rngNext() & 31)); }
    }
    if (e->type >= 5 && e->seen && !e->rush && !dead && !e->hid && (e->st == E_IDLE || e->st == E_WALK || e->st == E_STRAFE || e->st == E_SHOOT)) {   /* class 5 and 6 answer a wound (see damage_enemy), they do not shoot on their own */
        if (0 && rl_cd == 0) { g_miss = (u8)((rngNext() % 10) < 3); atk_beam(e, 15, 1); rl_cd = 600; e->fire_t = 14; }
        else if (0 && e->type == 6 && kz_cd == 0) { sfx(S_KOZE); wh_t = 150; kz_cd = 2400; e->fire_t = 14; }
    }
    if (e->burst) {
        if (e->bt > dt) e->bt -= (u8)dt; else e->bt = 0;
        if (e->bt == 0) { enemy_fire(e); e->fire_t = 10; e->burst--; e->bt = 9; }
    }
    if (e->t > dt) e->t -= dt; else e->t = 0;
    if (e->t == 0) enemy_think(e);
}

/* waves: an arena room sends enemies in waves of 3 (2 are on the field at the most); one of them is the melee fighter, the last one of a wave shoots */
static u8 en_room(const Enemy *e) { return (u8)(e->type == 6 ? LOOK_ROOM : (e->sp < NUM_SPAWNS ? spawn_room[e->sp] : 255)); }
static u8 wv_sz(u8 r) { u8 n = (u8)(room_left[r] + wv_dead[r]); return (u8)(n < 3 ? n : 3); }      /* the last wave may be smaller (8 = 3 + 3 + 2) */
static void rush_sync(u8 r) {                          /* exactly one melee fighter while at least 2 enemies of the wave are left */
    u8 i, have = 0, left = (u8)(wv_sz(r) - wv_dead[r]);
    for (i = 0; i < MAXE; i++) {
        Enemy *o = &en[i];
        if (o->st == E_NONE || o->st == E_DEAD || o->type == 0 || o->type == 6 || en_room(o) != r) continue;
        if (left < 2) o->rush = 0;
        else if (o->rush) { if (have) o->rush = 0; else have = 1; }
    }
    if (left < 2 || have) return;
    for (i = 0; i < MAXE; i++) {
        Enemy *o = &en[i];
        if (o->st == E_NONE || o->st == E_DEAD || o->type == 0 || o->type == 6 || en_room(o) != r) continue;
        o->rush = 1; o->pt = 0; break;
    }
}
static void drop_spawn(u8 r) {                       /* once a wave: a medkit falls from a white ceiling portal somewhere in the room */
    u8 k, tr;
    if (medkits >= 5 || (r >= NUM_ROOMS && r != 255)) return;           /* every wave, as long as the counter is not full */
    k = 4;
    for (tr = 0; tr < 4; tr++) if (!drops[tr].on) { k = tr; break; }
    if (k >= 4) for (tr = 0; tr < 4; tr++) if (drops[tr].on == 1) { k = tr; break; }     /* all busy: a lying one is replaced */
    if (k >= 4) return;
    for (tr = 0; tr < 16; tr++) {
        s16 x, y;
        if (r == 255) { x = (s16)(px + (s16)(rngNext() % 1200) - 600); y = (s16)(py + (s16)(rngNext() % 1200) - 600); }
        else {
            x = (s16)(((room_rc[r][0] + 1) << 8) + (s16)(rngNext() % ((u16)(room_rc[r][2] - room_rc[r][0] - 1) << 8)));
            y = (s16)(((room_rc[r][1] + 1) << 8) + (s16)(rngNext() % ((u16)(room_rc[r][3] - room_rc[r][1] - 1) << 8)));
        }
        if (tr >= 12) { x = (s16)(px + (tr - 13) * 150); y = (s16)(py + 200); }          /* last tries: next to the hero */
        if (blocked(x, y, 90)) continue;
        drops[k].x = x; drops[k].y = y; drops[k].z = 496; drops[k].vz = 0; drops[k].on = 2;
        {                                                  /* a random way, left or right of the line to the hero, and some speed */
            s16 dx = (s16)(px - x), dy = (s16)(py - y);
            u16 d = dist2d(dx, dy);
            s16 sx, sy;
            if (d == 0) d = 1;
            sx = (s16)(-(((s32)dy * 56) / (s32)d)); sy = (s16)(((s32)dx * 56) / (s32)d);
            if (rngNext() & 1) { sx = (s16)-sx; sy = (s16)-sy; }
            drops[k].vx = sx; drops[k].vy = sy;
        }
        return;
    }
}

static void drops_update(void) {
    u8 i, e;
    for (i = 0; i < 4; i++) {
        Drop *d = &drops[i];
        s16 nx, ny;
        if (!d->on) continue;
        if (d->on == 2) {
            d->vz = (s16)(d->vz - 7); d->z = (s16)(d->z + d->vz);
            if (d->z <= 0) { d->z = 0; if (d->vz < -30) d->vz = (s16)(-d->vz / 3); else { d->vz = 0; d->on = 1; } }
        } else {
            d->vx = (s16)((d->vx * 7) / 8); d->vy = (s16)((d->vy * 7) / 8);        /* sliding on the floor: slows down and stops */
            if (iabs(d->vx) < 4 && iabs(d->vy) < 4) { d->vx = 0; d->vy = 0; continue; }
        }
        nx = (s16)(d->x + d->vx); ny = (s16)(d->y + d->vy);
        if (blocked(nx, d->y, 60)) d->vx = (s16)(-(d->vx * 3) / 4); else d->x = nx;     /* bounce off the wall: reversed, slower */
        if (blocked(d->x, ny, 60)) d->vy = (s16)(-(d->vy * 3) / 4); else d->y = ny;
        for (e = 0; e < MAXE; e++)
            if (en[e].st != E_NONE && en[e].st != E_TELE && dist2d(d->x - en[e].x, d->y - en[e].y) < 130) {
                d->vx = (s16)(-(d->vx * 3) / 4); d->vy = (s16)(-(d->vy * 3) / 4);     /* off an enemy: the opposite way */
                if (d->vx == 0 && d->vy == 0) d->vx = (s16)(d->x > en[e].x ? 20 : -20);
                d->x = (s16)(d->x + d->vx); d->y = (s16)(d->y + d->vy);
            }
    }
}

static void wave_kill(u8 r) {
    if (r == 255) return;
    if (room_left[r]) room_left[r]--;
    wv_dead[r]++;
    if (wv_dead[r] == 1 && room_left[r]) drop_spawn(r);
    if (wv_dead[r] >= 3 || !room_left[r]) {
        wv_dead[r] = 0; wv_sp[r] = 0; wv_n[r]++;
        if (room_left[r]) { wv_t[r] = 120; set_banner(wave_txt[wv_n[r] > 5 ? 5 : wv_n[r]]); }
    }
    rush_sync(r);
}

static void damage_enemy(u8 i, s16 dmg) {
    Enemy *e = &en[i];
    if (IS_BARREL(e)) {                                       /* the barrel can not be shot - except in the last fight, when it comes back to the boss */
        if (bst == 4 && bw_on == 2 && tv_buf[58] && dist2d((s16)(e->x - en[i ^ 1].x), (s16)(e->y - en[i ^ 1].y)) < 1000) {
            tv_buf[58]--; boom(e->x, e->y); sfx(S_EHIT); e->st = E_NONE; bw_on = 0; bw_cd = 100;
            en[i ^ 1].fire_t = 0; en[i ^ 1].st = E_HURT; en[i ^ 1].t = 14;
            if (!tv_buf[58]) { set_banner("SHIELD DOWN"); tv_buf[59] = 0; tv_buf[60] = 0; en[i ^ 1].shotcd = 120; }
        } else sfx(S_RICO);
        return;
    }
    if (e->type == 0 && (bst == 0 || bst == 4) && !e->ph && e->pt) { sfx(S_RICO); add_fx(e->x, e->y, 0); if (bst != 4) e->pt = (u16)(e->pt > 60 ? e->pt - 60 : 0); return; }     /* the 10 s shield of every life in wave 1 */
    if (e->type == 0 && bst != 0 && bst != 4 && bst != 6) {                /* shielded in the other stages; at the glyph wall the shots through the optovisor pull the progress back */
        if (bst == 3) {                                      /* the glyph wall: nothing hurts the boss; while a fighter is out the two share one life */
            if (bw_y == 2) { u8 q; for (q = 0; q < MAXE; q++) if (en[q].type == 6 && en[q].hp != 99 && en[q].st >= E_IDLE && en[q].st != E_DEAD) { damage_enemy(q, dmg); break; } }
            else sfx(S_RICO);
            return;
        }
        else sfx(S_RICO);
        return;
    }
    e->hp -= dmg;
    add_fx(e->x, e->y, 2);
    if (e->type == 0 && bst == 4 && e->hp <= 2) {                         /* 2 lives left: it spins with a blue aura, the eye marks the crystal for the redvisor */
        e->hp = 2; e->mark = 2; e->ph = 0; e->st = E_IDLE; bst = 5; bw_clear();
        bcry_on = 1; crystal_pos(); have_d = 4; dev_on = 0; cur_d = 2; vis_t[2] = 10800; bst_t = 0; set_banner("FIND CRYSTAL HERE");      /* the hero puts the redvisor on himself */
        return;
    }
    if (e->hp <= 0 && e->type == 0 && bst < 5) { boss_next(e); return; }
    if (e->hp <= 0) {
        sfx(S_EDIE);
        e->st = E_DEAD; e->at = 0; e->t = 16; e->z = 0;
        if (e->type == 0) { crystals++; set_banner("EXIT OPEN"); cs_ph = 1; cs_n = 0; cs_w = 1; }     /* the last crystal is taken, the boss blows up: flash, "all crystals found" */
        if (e->type != 0 && (bst == 8 || (BHN && (bst == 0 || bst == 4 || bst == 7))) && wv_k < 99) wv_k++;        /* only the boss's own helpers / the three fighters: room and ghost kills do not count */
        if (e->type != 0 && (bst >= 2 || BHN)) { }                                       /* the boss-room fighters leave no traces */
        else if (e->type == 6) { ghost_kill++; wave_kill(LOOK_ROOM); }
        else if (e->type != 0) {
            sp_st[e->sp] = sp_left[e->sp] ? 0 : 2;
            sp_cd[e->sp] = 40;
            wave_kill(spawn_room[e->sp]);
            if (spawn_room[e->sp] == 255 && kills % 3 == 0) drop_spawn(255);       /* the corridor fights: a medkit for every 3 kills */
        }
        e->rush = 0;
        kills++;
    } else {
        sfx(S_EHIT);
        e->st = E_HURT; e->t = 12; e->at = 0;
        e->flee = (u8)(e->type != 0 && e->type != 6 && !e->rush);
        if ((e->type == 5 || e->type == 6) && e->mark - e->hp >= 2) {            /* once for every 2 cells of its own life that are lost */
            e->mark = e->hp;
            if (e->type == 5) { e->burst = 3; e->bt = 14; }
            else if (e->seen && !dead) { g_miss = (u8)((rngNext() % 10) < 3); atk_beam(e, 15, 1); e->fire_t = 14; }
        }
        if ((e->type == 5 || e->type == 6) && bst != 1 && bst != 10 && (rngNext() & 1)) {      /* hit: it may blink to any respawn point */
            s16 tx, ty;
            if (BHN || bst >= 2) { u8 j = (u8)(rngNext() % 6); tx = bsp[j][0]; ty = bsp[j][1]; }
            else { u8 j = (u8)(rngNext() % NUM_SPAWNS); tx = spawn_pos[j][0]; ty = spawn_pos[j][1]; }
            if (!blocked(tx, ty, 70)) { e->x = tx; e->y = ty; e->st = E_TELE; e->t = 44; e->at = 0; e->fire_t = 0; e->flee = 0; sfx(S_SPAWN); }
        }
        if (e->type == 0 && e->ph == 0 && e->mark - e->hp >= 2) { e->ph = 1; e->pt = 120; }
    }
}

/* ---------------------------------------------------------------- bolts */
static void bolts_update(u16 dt) {
    u8 i, k;
    for (i = 0; i < MAXB; i++) {
        Bolt *b = &bolts[i];
        if (!b->on) continue;
        for (k = 0; k < dt; k++) {
            s16 hx, hy;
            b->x += b->vx;
            b->y += b->vy;
            if (b->life) b->life--;
            if (cell(b->x, b->y) || b->life == 0) {
                if (b->kind == 2) { add_fx(b->x - b->vx, b->y - b->vy, 2); sfx(S_EHIT); }      /* a rocket explodes on the wall */
                else add_fx(b->x - b->vx, b->y - b->vy, 0);
                b->on = 0;
                break;
            }
            hx = b->x - px; hy = b->y - py;
            if (b->miss) {
                if (b->miss == 1 && iabs(hx) < 420 && iabs(hy) < 420) { b->miss = 2; sfx(S_RICO); }
                continue;
            }
            if (!dead && iabs(hx) < 96 && iabs(hy) < 96) {
                hurt_player(b->kind == 2 ? 22 : b->kind ? 18 : 8);
                b->on = 0;
                break;
            }
        }
    }
}

static void fx_update(u16 dt) {
    u8 i;
    for (i = 0; i < MAXFX; i++) {
        Fx *f = &fxs[i];
        if (!f->on) continue;
        f->t += (u8)dt;
        if (f->kind == 1) {
            f->f = (u8)((f->t >> 2) % 3);
            if (f->t > 44) f->on = 0;
        } else {
            f->f = (u8)(f->t >> 3);
            if (f->f > 2) f->on = 0;
        }
    }
}

/* ---------------------------------------------------------------- player */
static void try_move_player(s16 vx, s16 vy) {
    s16 nx = px + vx, ny = py + vy;
    if (!blocked(nx, py, 80)) px = nx;
    else if (vx) { s16 bx = (s16)(px - (vx > 0 ? 12 : -12)); if (!blocked(bx, py, 80)) px = bx; }      /* a wall: the hero is thrown back a few pixels */
    if (!blocked(px, ny, 80)) py = ny;
    else if (vy) { s16 by = (s16)(py - (vy > 0 ? 12 : -12)); if (!blocked(px, by, 80)) py = by; }
}

static void player_fire(void) {
    u8 i, best = 255, w = cur_w;
    s16 bf = 32000, wallz = (s16)zbuf_w[VIEW_COLS / 2];
    if (bst == 3 && bw_on == 1) { s16 wsx, wsf, wf; if (project(bw_x, bw_y, &wsx, &wsf, &wf) && wsx > 96 && wsx < 160) { bw_on = 2; sfx(S_RICO); add_fx(bw_x, bw_y, 0); } }     /* the shot hits the energy line: it bounces back */
    s16 dmg = (s16)w_dmg[w];
    u8 pierce = (u8)(w == 3 || w == 5);
    if (w == 7) { chain_n++; if (chain_n < 5) dmg = 0; else chain_n = 0; }       /* 10 bullets = 2 squares */
    for (i = 0; i < MAXE; i++) {
        if (!scr_vis[i] || en[i].st < E_IDLE || en[i].st == E_DEAD || BOSS_DOWN(i)) continue;
        if (128 >= scr_x0[i] - 14 && 128 <= scr_x1[i] + 14 && 64 >= scr_y0[i] - 10 && 64 <= scr_y1[i] + 10 && scr_f[i] < wallz) {
            if (pierce) damage_enemy(i, dmg);                        /* railgun and beamchain pierce */
            else if (scr_f[i] < bf) { bf = scr_f[i]; best = i; }
        }
    }
    sfx(w == 7 ? S_CHAIN : (u8)(S_FUSER + w));
    if (pierce) {
        s16 wz = wallz > 7000 ? 7000 : wallz;
        add_fx(px + (s16)(((s32)COSA(pang) * wz) >> 8), py + (s16)(((s32)SINA(pang) * wz) >> 8), 0);
        return;
    }
    if (best != 255 && dmg == 0) add_fx(en[best].x, en[best].y, 0);
    else if (best != 255) {
        damage_enemy(best, dmg);
        if (w == 2 || w == 4) {                                      /* plasma and rocket splash */
            for (i = 0; i < MAXE; i++)
                if (i != best && en[i].st >= E_IDLE && en[i].st != E_DEAD && dist2d(en[i].x - en[best].x, en[i].y - en[best].y) < (w == 4 ? 600 : 420))
                    damage_enemy(i, dmg);
        }
    } else {
        s16 wz = wallz > 7000 ? 7000 : wallz;
        add_fx(px + (s16)(((s32)COSA(pang) * wz) >> 8), py + (s16)(((s32)SINA(pang) * wz) >> 8), 0);
    }
}

static void melee(void) {
    u8 i;
    for (i = 0; i < MAXE; i++) {
        if (!scr_vis[i] || en[i].st < E_IDLE || en[i].st == E_DEAD) continue;
        if (dist2d(en[i].x - px, en[i].y - py) < 520 && 128 >= scr_x0[i] - 20 && 128 <= scr_x1[i] + 20) {
            sfx(S_MELEE);
            damage_enemy(i, 99);
            shake = 4;
            break;
        }
    }
}

/* ---- sectors (Doom / Portal style): the rooms are cut at the doors. Only the sector the hero stands in and the
   sectors seen through open doors are simulated and drawn; the rest of the level sleeps. */
static u8 sec_act[NUM_SECS], psec = 255, item_sec[NUM_ITEMS], sp_sec[NUM_SPAWNS];
static u8 sec_at(s16 x, s16 y) { return sec_map[((u16)((u8)(y >> 8) & 63) << 5) + ((u8)(x >> 8) & 31)]; }
static u8 sec_on(u8 s) { return (u8)(s == 255 || sec_act[s]); }
static s16 stk_x, stk_y; static u16 stuck_t;

static void bike_advance(u16 dt);
static u8 combat_now(void);
static u8 vis_block(void);
static u8 ch3_i;
static void cheat_feed(u16 pressed);
static void scene_player(u16 dt);
/* ---------------------------------------------------------------- special floors (map editor): checkpoints, teleports, lava, acid, electro water, dark hole, radiation zone, hidden wall */
static u8 cp_cur, fk_inst, fk_t, dh_d, tp_lock;
static u8 fk_om[3];                                   /* the zones switched off by their triggers: bit n = zone n; [0] lava, [1] electro water, [2] hidden walls */
#define FKK(v) ((v) & 15)
#define FKZ(v) ((v) >> 4)
static void fk_hide(void) {                           /* a hidden wall is solid for the hero and the enemies (map_w) but not drawn: the GSU map stays empty there */
#if FK_ANY
    u16 i;
    for (i = 0; i < 2048; i++) if (FKK(fk_map[i]) == 8) map_w[i] = (u8)((fk_om[2] >> FKZ(fk_map[i])) & 1 ? 0 : 15);
#endif
}
static void cp_goto(void) { px = (s16)(cp_xy[cp_cur][0] * 256 + 128); py = (s16)(cp_xy[cp_cur][1] * 256 + 128); pang = 384; }
static void trg_apply(u8 k, u8 z) {                    /* the three triggers: waterbox (hydrant) / electrobox / hiddenbox; z = the zone it switches off (255: every zone of the kind) */
    fk_om[k - 18] |= (u8)(z == 255 ? 255 : 1 << z);
    if (k == 18) set_banner("LAVA TURNED TO WATER");
    else if (k == 19) set_banner("ELECTRICITY OFF");
    else { fk_hide(); set_banner("HIDDEN WALLS OFF"); }
}
#if FK_ANY
static void fk_die(const char *msg) { fk_inst = 1; php = 0; dead = 1; dead_t = 0; flash_t = 8; shake = 8; sfx(S_DEATH); set_banner(msg); }
#endif
#if FK_ANY
static u8 fk_live(u8 v) { u8 k = FKK(v); if (k == 2 || k == 4) return (u8)((fk_om[k == 2 ? 0 : 1] >> FKZ(v)) & 1 ? 0 : k); return k; }     /* the kind, or 0 when its zone has been switched off */
static u8 fk_at(s16 x, s16 y) { if (x < 0 || y < 0 || (x >> 8) > 31 || (y >> 8) > 63) return 0; return fk_live(fk_map[(((u16)y >> 8) << 5) + ((u16)x >> 8)]); }
#endif
static void floor_update(u16 dt) {
#if FK_ANY
    u8 k, i, tx, ty, air;
    if (dead || won || CSP || cls_sel || !tour_done) return;
    tx = (u8)((u16)px >> 8); ty = (u8)((u16)py >> 8);
    k = fk_live(fk_map[((u16)ty << 5) + tx]);
    air = (u8)(pz > 12 || pvz > 0);
    if (k == 1) {
        for (i = 0; i < NUM_CP; i++) if (cp_xy[i][0] == tx && cp_xy[i][1] == ty && i != cp_cur) { cp_cur = i; sfx(S_PICK); set_banner("CHECKPOINT"); }
    }
    if (k != 3 && k != 7) fk_t = 0;
    if (k == 2 && !air) fk_die("LAVA");
    else if (k == 4 && !air) fk_die("ELECTRO WATER");
    else if (k == 3 && !air) {
        if (fk_t == 0 || fk_t >= 60) { fk_t = 1; php -= 30; hurt_t = 14; shake = 4; sfx(S_HURT); set_banner("ACID"); check_life(); }
        else fk_t = (u8)(fk_t + dt);
    }
    else if (k == 7 && !super_t) {
        fk_t = (u8)(fk_t + dt);
        if (fk_t >= 60) { fk_t = 0; php -= 10; hurt_t = 6; sfx(S_HURT); set_banner("RADIATION ZONE"); check_life(); }
    }
    if (k == 6 && !air) {                                     /* dark hole: without a jump the hero sinks (1.5 s standing, ~0.7 s running) and dies at the bottom */
        dh_d = (u8)(dh_d + dt * 3 > 90 ? 90 : dh_d + dt * 3);
        if (dh_d >= 90) { dh_d = 0; fk_die("DARK HOLE"); }
    } else if (dh_d) dh_d = (u8)(dh_d > dt * 4 ? dh_d - dt * 4 : 0);
#if NUM_TP
    {
        u8 on = 0;
        for (i = 0; i < NUM_TP; i++) {
            if (tp_xy[i][0] == tx && tp_xy[i][1] == ty) { on = 1; if (!tp_lock) { px = (s16)(tp_xy[i][2] * 256 + 128); py = (s16)(tp_xy[i][3] * 256 + 128); tp_lock = 1; flash_t = 8; sfx(S_VON); set_banner("TELEPORT"); break; } }
            if (tp_xy[i][2] == tx && tp_xy[i][3] == ty) on = 1;
        }
        if (!on) tp_lock = 0;
    }
#endif
    if (!(last_fc & 15) && !fk_t) {                           /* a warning one or two tiles ahead (the floor itself is not drawn by the GSU yet) */
        u8 ak = fk_at((s16)(px + COSA(pang)), (s16)(py + SINA(pang)));
        if (!ak || ak == 1 || ak == 5) ak = fk_at((s16)(px + COSA(pang) * 2), (s16)(py + SINA(pang) * 2));
        if (ak == 2) set_banner("LAVA AHEAD");
        else if (ak == 4) set_banner("ELECTRO WATER AHEAD");
        else if (ak == 6) set_banner("DARK HOLE: JUMP");
        else if (ak == 3) set_banner("ACID AHEAD");
        else if (ak == 7 && !super_t) set_banner("RADIATION AHEAD");
    }
#endif
}
static void update_player(u16 dt, u16 held, u16 pressed) {
    if (tour_done && !info_win) cheat_feed(pressed);
    s16 fwd = 0, side = 0, speed = 14;
    u16 k;
    u8 look = (held & KEY_L) != 0;
    if (dead) return;
    running = 0;
    if (shake > 0 && (bst == 9 || bst == 10 || bst == 11 || bst == 12 || bst == 13)) shake--;          /* these states return early: the shake must still fade */
    if ((bst == 9 && !CSP) || bst == 13) { scene_player(dt); return; }      /* cut-scenes: the pad is dead */
    if (bst == 11) {                                            /* the last scene: the camera turns to the boss by itself */
        moving = 0; ppitch = 0; pz = 0; pvz = 0;
        if (!CSP) {
            s16 dx = (s16)(tp_x - px), dy = (s16)(tp_y - py);
            s32 cr = (s32)COSA(pang) * dy - (s32)SINA(pang) * dx, dp = (s32)COSA(pang) * dx + (s32)SINA(pang) * dy, lim = (s32)dist2d(dx, dy) * 24;
            if (dp < 0 || cr > lim || cr < -lim) pang = (u16)((cr >= 0 ? pang + 3 * dt : pang - 3 * dt) & 511);
        }
        if (hurt_t > dt) hurt_t -= dt; else hurt_t = 0;
        if (flash_t > dt) flash_t -= dt; else flash_t = 0;
        return;
    }
    if (bst == 12) {                                            /* the wires: the d-pad chooses a switch, R (or A/B/X) flips it */
        moving = 0; ppitch = 0; pz = 0; pvz = 0;
        if (blink_t) { blink_t += dt; if (blink_t >= 50) blink_t = 0; }                    /* the eyelids close over the finished wires */
        if (pressed & KEY_LEFT) wr_c &= 6;                       /* the switch on the left of the wire */
        if (pressed & KEY_RIGHT) wr_c |= 1;
        if ((pressed & KEY_UP) && wr_c >= 2) wr_c -= 2;
        if ((pressed & KEY_DOWN) && wr_c < 6) wr_c += 2;
        if ((pressed & (KEY_A | KEY_B | KEY_X | KEY_R)) && !wr_t) { wr_s ^= (u8)(1 << wr_c); wr_calc(); wr_pl = 0; wr_ph = 0; wr_d = 0; sfx(S_BLIP); }   /* the circuit changes: the current starts again */
        if (hurt_t > dt) hurt_t -= dt; else hurt_t = 0;
        if (flash_t > dt) flash_t -= dt; else flash_t = 0;
        return;
    }
    if (bst == 10) {                                            /* on the bike: only steering (LEFT/RIGHT), no weapon, no radiation */
        moving = 0; ppitch = 0; pz = 0; pvz = 0; pang = 384;
        if (blink_t) { blink_t += dt; if (blink_t >= 50) blink_t = 0; }                    /* the dark between the chase and the glyph wall */
        hb_s = (u8)((held & KEY_LEFT) ? 0 : (held & KEY_RIGHT) ? 2 : 1);
        if (!CSP) { bike_advance(dt); if (held & KEY_LEFT) px = (s16)(px - 9 * dt); if (held & KEY_RIGHT) px = (s16)(px + 9 * dt); px = clamp16(px, 3700, 4500); if (pressed & KEY_R) { bw_on = 1; sfx(S_FUSER); } }
        if (hurt_t > dt) hurt_t -= dt; else hurt_t = 0;
        if (flash_t > dt) flash_t -= dt; else flash_t = 0;
        return;
    }
    if (blink_t) {                                      /* eyelids: 24 frames shut, wake up at the start, 24 frames open */
        blink_t += dt;
        if (blink_t >= 24 && !blink_tp) { blink_tp = 1;
            { u8 sc = sec_at(px, py); if (sc == 255) sc = psec; if (sc != 255) { px = sec_c[sc][0]; py = sec_c[sc][1]; } else { px = START_X; py = START_Y; } }
            ppitch = 0; pz = 0; pvz = 0; }
        if (blink_t >= 50) blink_t = 0;
    }
    if (look) {
        if (held & KEY_UP)    { ppitch += 3 * dt; l_used = 1; }
        if (held & KEY_DOWN)  { ppitch -= 3 * dt; l_used = 1; }
        ppitch = clamp16(ppitch, -30, 30);
    } else {
        if (held & KEY_UP)    fwd += speed;
        if (held & KEY_DOWN)  fwd -= speed;
        if (prev_held & KEY_L) { if (!l_used) ppitch = 0; l_used = 0; }       /* a tap on L: look straight */
    }
    if (blink_t) { fwd = 0; side = 0; held &= (u16)~(KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_Y | KEY_B | KEY_A); pressed = 0; }
    /* B held = item menu: LEFT/RIGHT switch the class (W weapons / V visors), UP/DOWN pick inside the class, R = use */
    if (held & KEY_B) {
        if (ud_t > dt) ud_t -= dt; else ud_t = 0;
        if (pressed & (KEY_UP | KEY_DOWN)) ud_t = 12;
        /* a diagonal slip of the thumb on the pad must not flip weapons <-> visors: LEFT/RIGHT only count when UP/DOWN are not (or were not just) pressed */
        if (held & (KEY_LEFT | KEY_RIGHT)) { if (lr_t < 200) lr_t += (u8)dt; } else { lr_t = 0; lr_done = 0; }
        if (!lr_done && lr_t >= 6 && ud_t == 0 && !(held & (KEY_UP | KEY_DOWN))) {   /* LEFT/RIGHT must be held for a moment */
            u8 want = (u8)((held & KEY_RIGHT) ? 1 : 0);
            lr_done = 1;
            if (want && !have_d) set_banner("NO VISORS YET");
            else {
                if (want && !(have_d & (1 << cur_d))) cur_d = (u8)((have_d & 1) ? 0 : (have_d & 2) ? 1 : 2);
                if (cls_sel != want) { cls_sel = want; sfx(S_BLIP); wp_shown = 255; }
            }
        }
        if (pressed & (KEY_UP | KEY_DOWN)) {
            s8 st = (s8)((pressed & KEY_DOWN) ? 1 : -1);
            if (!cls_sel) {
                u8 n = 0, p;
                while (w_ord[n] != cur_w) n++;
                do { n = (u8)(n + st >= NW ? 0 : (n + st < 0 ? NW - 1 : n + st)); p = w_ord[n]; } while (!(have_w & (1 << p)));
                w_rel[cur_w] = 0; w_left[cur_w] = 0; w_ammo[cur_w] = w_amax[cur_w]; cur_w = w_ord[n]; wp_shown = 255; wp_pal_dirty = 1; sfx(S_BLIP);
            } else {
                u8 n = cur_d;
                do { n = (u8)(n + st >= 3 ? 0 : (n + st < 0 ? 2 : n + st)); } while (!(have_d & (1 << n)));
                cur_d = n; sfx(S_BLIP);
            }
        }
        held &= (u16)~(KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
        fwd = 0;
    } else {
        if (held & KEY_LEFT)  pang = (pang - 5 * dt) & 511;
        if (held & KEY_RIGHT) pang = (pang + 5 * dt) & 511;
    }
    if (held & KEY_Y) side -= speed - 2;                 /* Y strafe left, A strafe right */
    if (held & KEY_A) side += speed - 2;
    moving = (fwd != 0 || side != 0);
    if (moving) {
        for (k = 0; k < dt; k++) {
            s16 vx = (s16)(((s32)COSA(pang) * fwd - (s32)SINA(pang) * side) >> 8);
            s16 vy = (s16)(((s32)SINA(pang) * fwd + (s32)COSA(pang) * side) >> 8);
            try_move_player(vx, vy);
        }
        { bobph += (u16)(9 * dt); stp_acc = (u16)(stp_acc + (u16)(speed * dt)); if (stp_acc >= 430) { stp_acc = (u16)(stp_acc - 430); if (pz == 0) sfx(S_STEP); } }      /* footsteps: one per stride (distance walked) */
    }
    /* stuck in a wall texture: pushing forward/back for 2 s without getting anywhere moves the hero toward the room centre */
    if (fwd != 0 && !blink_t) {
        if (iabs(px - stk_x) + iabs(py - stk_y) > 60) { stk_x = px; stk_y = py; stuck_t = 0; }
        else if ((stuck_t += dt) >= 60) {
            u8 sc = sec_at(px, py), st;
            stuck_t = 0;
            if (sc == 255) sc = psec;
            if (sc != 255) {
                s16 ddx = (s16)(sec_c[sc][0] - px), ddy = (s16)(sec_c[sc][1] - py);
                u16 dd = dist2d(ddx, ddy);
                u8 ok = 0;
                if (dd > 0) for (st = 1; st <= 4 && !ok; st++) {
                    s16 nx = (s16)(px + (s16)(((s32)ddx * (st * 32)) / (s16)dd)), ny = (s16)(py + (s16)(((s32)ddy * (st * 32)) / (s16)dd));
                    if (!blocked(nx, ny, 80)) { px = nx; py = ny; ok = 1; }
                }
                if (!ok) { px = sec_c[sc][0]; py = sec_c[sc][1]; }
                sfx(S_BLIP);
            }
            stk_x = px; stk_y = py;
        }
    } else { stuck_t = 0; stk_x = px; stk_y = py; }
    /* jump: X */
    if ((pressed & KEY_X) && pz == 0) { pvz = 22; sfx(S_JUMP); }
    if ((held & KEY_X) && !blink_t) {
        x_hold += dt;
        if (x_hold >= 120) { x_hold = 0; blink_t = 1; blink_tp = 0; sfx(S_VOFF); }
    } else if (!(held & KEY_X)) x_hold = 0;
    if (pz > 0 || pvz > 0) {
        for (k = 0; k < dt; k++) {
            pz += pvz;
            pvz -= 3;
            if (pz <= 0) { pz = 0; pvz = 0; break; }
        }
    }
    /* weapon */
    if (fire_cd > dt) fire_cd -= dt; else fire_cd = 0;
    if (flash_t > dt) flash_t -= dt; else flash_t = 0;
    if (hurt_t > dt) hurt_t -= dt; else hurt_t = 0;
    if (wp_t > dt) wp_t -= dt; else wp_t = 0;
    if (pl_shot_t > dt) pl_shot_t -= dt; else pl_shot_t = 0;
    if (koze_t > dt) koze_t -= dt; else koze_t = 0;
    if (wp_t == 0) wp_frame = 0;
    else if (wp_t < 6 && cur_w != 7) wp_frame = 2;
    for (k = 0; k < NW; k++) {
        if (w_left[k] && w_left[k] <= dt && k == cur_w && w_cd[k] > 90) sfx(S_READY);
        if (w_left[k] > dt) w_left[k] -= dt; else w_left[k] = 0;
        if (w_rel[k]) {
            if (w_rel[k] > dt) w_rel[k] -= dt;
            else { w_rel[k] = 0; w_ammo[k] = w_amax[k]; if (k == cur_w) sfx(S_READY); }
        }
    }
    /* R: short = shot / switch the device (on release), long = melee */
    if (held & KEY_R) {
        r_hold += dt;
        if (cur_w == 7 && !cls_sel) {                 /* chaingun: hold R to fire */
            if (w_left[7] == 0 && w_rel[7] == 0) {
                w_left[7] = w_cd[7];
                if (--w_ammo[7] == 0) w_rel[7] = w_rel_t[7];
                flash_t = 2; wp_t = 7; wp_frame = (u8)(1 + (w_ammo[7] & 1)); pl_shot_t = 6; shot_w = 7;
                shake = 2;
                player_fire();
            }
            r_long = 1;
        } else if (r_hold >= 24 && !r_long) { r_long = 1; if (!cls_sel) melee(); }
    } else {
        if (r_hold && !r_long) {
            if (cls_sel) {
                if (vis_block()) { sfx(S_VOFF); set_banner("VISORS BLOCKED"); }
                else if (dev_on & (1 << cur_d)) { dev_on &= (u8)~(1 << cur_d); sfx(S_VOFF); cls_sel = 0; wp_shown = 255; wp_pal_dirty = 1; }
                else if (vis_t[cur_d]) { dev_on = (u8)(1 << cur_d); sfx(S_VON); cls_sel = 0; wp_shown = 255; wp_pal_dirty = 1; }   /* one visor at a time; then back to the game */
            } else if (w_left[cur_w] == 0 && w_rel[cur_w] == 0 && w_ammo[cur_w]) {      /* every press fires while the magazine has rounds */
                w_left[cur_w] = 5;
                if (w_amax[cur_w] != 255 && --w_ammo[cur_w] == 0) w_rel[cur_w] = w_rel_t[cur_w];
                flash_t = 4;
                wp_t = 12;
                wp_frame = 1;
                pl_shot_t = 14;
                shot_w = cur_w;
                if (cur_w == 6) koze_t = 10;
                player_fire();
            }
        }
        r_hold = 0; r_long = 0;
    }
    /* radiation: the whole level is radioactive, one cell of the power bar every 5 s (a bottle switches it off for 30 s) */
    if (super_t > dt) super_t -= dt; else super_t = 0;
    if (!FK_RAD && !super_t && !won && !info_win && !cls_sel) {
        rad_t = (u16)(rad_t + dt);
        if (rad_t >= 900) { rad_t = 0; php -= 10; sfx(S_HURT); check_life(); }
    }
    {                                                   /* every visor has a battery: it drains while worn and charges slowly while off */
        static u8 vrc;
        u8 v;
        vrc = (u8)(vrc + dt);
        for (v = 0; v < 3; v++) {
            if (cls_sel) break;                         /* the visor menu stops the game, and the batteries with it */
            if (dev_on & (1 << v)) { if (vis_t[v] > dt) vis_t[v] -= dt; else { vis_t[v] = 0; dev_on &= (u8)~(1 << v); sfx(S_VOFF); set_banner("VISOR OFF"); } }
            else if ((have_d & (1 << v)) && vis_t[v] < 10800 && vrc >= 2) vis_t[v]++;
        }
        if (vrc >= 2) vrc = 0;
    }
    floor_update(dt);
    if (banner_t > dt) banner_t -= dt; else banner_t = 0;
    if (shake > 0) shake--;
    prev_held = held;
}

static const char *const key_msg[3] = { "NEED BLUE CARD", "NEED YELLOW CARD", "NEED RED CARD" };
static void set_door(u8 d, u8 open) {
    u16 a = (u16)(((u16)door_xyk[d][1] << 5) + door_xyk[d][0]);
    u8 v = open ? 0 : (u8)(10 + door_xyk[d][2]);
    if (map_w[a] == v) return;
    map_w[a] = v;
    if (gpq_n < 24) { gpq_a[gpq_n] = a; gpq_v[gpq_n] = v; gpq_n++; } else gpq_full = 1;
}
static void gp_apply(void) {                          /* the GSU is stopped: the CPU owns Game Pak RAM */
    u8 i;
    if (gpq_full) { u16 j; for (j = 0; j < 2048; j++) *GP_U8(GP_MAP + j) = (u8)(map_w[j] == 15 ? 0 : map_w[j]); }
    else for (i = 0; i < gpq_n; i++) *GP_U8(GP_MAP + gpq_a[i]) = gpq_v[i];
    gpq_n = 0; gpq_full = 0;
}

static u16 g_dt, wu_acc;
static u8 wu_phase;
static u8 locks_dirty = 1, rst_old[NUM_ROOMS];
#ifdef DBG_DT
static u8 dbg_max, dbg_t; static u16 dbg_sum;
#endif
static u8 door_locked(u8 d) {
    u8 r;
    s16 x = door_xyk[d][0], y = door_xyk[d][1];
    for (r = 0; r < NUM_ROOMS; r++)
        if ((rst[r] == 1 || rst[r] == 2) && x >= room_rc[r][0] - 1 && x <= room_rc[r][2] + 1 && y >= room_rc[r][1] - 1 && y <= room_rc[r][3] + 1) return 1;
    return 0;
}

static void arena_update(u8 tx, u8 ty, u16 dt) {
    u8 r;
    for (r = 0; r < NUM_ROOMS; r++) {
        if (rst[r] == 0 && room_rc[r][4] == 2 && tx >= room_rc[r][0] && tx <= room_rc[r][2] && ty >= room_rc[r][1] && ty <= room_rc[r][3]) {
            rst[r] = 1; rt_[r] = 120; set_banner("LOCKED");                    /* 2 s of silence, then the fight */
        } else if (rst[r] == 4 && tx >= room_rc[r][0] && tx <= room_rc[r][2] && ty >= room_rc[r][1] && ty <= room_rc[r][3]) {
            rst[r] = 1; rt_[r] = 120; set_banner("LOCKED");                    /* back in the room: the fight starts again */
        } else if (rst[r] == 1) {
            if (rt_[r] > dt) rt_[r] -= dt; else { rt_[r] = 0; rst[r] = 2; }
        } else if (rst[r] == 2 && room_left[r] == 0) {
            rst[r] = 3; set_banner("DOORS UNLOCKED"); sfx(S_DOOR);
            mus_set(0); mus_hold = 0;                                         /* room cleared: back to the calm song at once */
        }
        if (wv_t[r]) wv_t[r] = (u8)(wv_t[r] > dt ? wv_t[r] - dt : 0);
    }
}

static void sectors_update(void) {
    u8 i, s = sec_at(px, py), ch = 1;
    if (s != 255) psec = s;
    for (i = 0; i < NUM_SECS; i++) sec_act[i] = 0;
    if (psec != 255) sec_act[psec] = 1;
    while (ch) {                                           /* a door that is open links its two sectors */
        ch = 0;
        for (i = 0; i < NUM_DOORS; i++) {
            u8 a = door_sec[i][0], b = door_sec[i][1];
            if (!door_st[i] || a == 255 || b == 255) continue;
            if (sec_act[a] != sec_act[b]) { sec_act[a] = sec_act[b] = 1; ch = 1; }
        }
    }
    for (i = 0; i < MAXE; i++) en_act[i] = (u8)(en[i].type == 6 || sec_on(sec_at(en[i].x, en[i].y)));
}

static void sectors_init(void) {
    u8 i;
    for (i = 0; i < NUM_ITEMS; i++) item_sec[i] = sec_at(item_pos[i][0], item_pos[i][1]);
    for (i = 0; i < NUM_SPAWNS; i++) sp_sec[i] = sec_at(spawn_pos[i][0], spawn_pos[i][1]);
    for (i = 0; i < MAXE; i++) en_act[i] = 1;
}

/* ---- RUN RUN RUN: the chase. The engine drives down a straight 4-cell corridor (the map is swapped for the minigame and given back afterwards);
   the boss rides ahead (the boss' frames 10/11 are the bike), the second enemy slot is a barrel or a saw (frames 10/11 of the class-6 body). */
#define BIKE_SPD 6
#define bk_sg tv_buf[44]                                           /* the boss's side of the hero's axis: 1 left, 2 right, 0 not known yet */
#define BIKE_LANE_L 3840
#define BIKE_LANE_R 4352
static u8 bike_boss(void) { return (u8)(en[0].type == 0 && en[0].st != E_NONE ? 0 : 1); }
static void bike_map(void) {                                       /* the straight 4-cell road replaces the map */
    u16 i;
    for (i = 0; i < 2048; i++) map_w[i] = 1;
    for (i = 1; i < 63; i++) { u8 x; for (x = 14; x < 18; x++) map_w[(i << 5) + x] = 0; map_w[(i << 5) + 13] = 4; map_w[(i << 5) + 18] = 4; }
    gpq_full = 1; gpq_n = 0;
}
static void bike_enter(void) {
    u8 b = bike_boss();
    bike_map(); tp_on = 0;
    px = 4096; py = 14848; pang = 384; ppitch = 0; pz = 0; pvz = 0;
    en[b ^ 1].st = E_NONE;
    en[b].x = 4096; en[b].y = (s16)(py - 600); en[b].st = E_IDLE; en[b].hp = (s16)sq_n[0]; en[b].mark = en[b].hp; en[b].fire_t = 0; en[b].z = 0; en[b].shown_f = 255; en[b].bt = 0; gs_invalidate();
    en_act[b] = 1; dev_on = 0; bw_on = 0; sp_t = 0; xp_t = 0;
    bst_t = 3600; bw_x = 4500; bw_cd = 90; bw_y = 60; bk_sg = 0; php = PLAYER_HP;
    obj_dirty = 1; wp_shown = 255; wp_busy = 0; wp_pal_dirty = 1; hb_s = 1;
}
static void bike_exit(void) {
    u8 b = bike_boss(), q; u16 i;
    for (i = 0; i < 2048; i++) map_w[i] = level_map[i];
    fk_hide();
    for (q = 0; q < NUM_DOORS; q++) if (door_st[q]) set_door(q, 1);
    gpq_full = 1; locks_dirty = 1;
    en[b ^ 1].st = E_NONE;
    px = 4736; py = 1190; pang = 384; ar_t = 0; cr_t = 0;                          /* back in the boss room (the hero rides in from the dark), where the boss respawns */
    en[b].x = BK_X - 480; en[b].y = 440; en[b].hid = 0; en[b].st = E_IDLE; en[b].hp = (s16)sq_n[0]; en[b].mark = en[b].hp; en[b].ph = 0; en[b].pt = 0; en[b].bt = 0; en[b].shown_f = 255; gs_invalidate();
    bst = 13; bw_x = 0; bw_y = 0; wv_k = 0; bst_t = 0; bw_cd = 0; tp_on = 0; bw_on = 0; sp_t = 0;
    obj_dirty = 1; wp_shown = 255; wp_busy = 0; wp_pal_dirty = 1;
}
static void arrival_step(u16 dt) {                                  /* the hero rides up to the glyph wall; his camera follows the boss: bike, run, wall; 5 s at the terminal, then the black card */
    u8 q;
    Enemy *b = 0;
    if (py > 1010) { py = (s16)(py - 4 * (s16)dt); if (py < 1010) py = 1010; }
    for (q = 0; q < MAXE; q++) if (en[q].type == 0 && en[q].hid != 7 && en[q].st != E_NONE) b = &en[q];
    if (b) {
        s16 dx = (s16)(b->x - px), dy = (s16)(b->y - py);
        s32 cr = (s32)COSA(pang) * dy - (s32)SINA(pang) * dx, dp = (s32)COSA(pang) * dx + (s32)SINA(pang) * dy, lim = (s32)dist2d(dx, dy) * 6;
        if (dp < 0 || cr > lim || cr < -lim) pang = (u16)((cr >= 0 ? pang + 3 * dt : pang - 3 * dt) & 511);
    }
    if (bst_t < ARR_T1 || py > 1010) return;
    if (bw_cd < 300) {                                              /* 5 s: the boss works at the terminal, KEY ENCRYPT on top; only then the black card */
        if (!bw_cd) { set_banner("KEY ENCRYPT"); sfx(S_PICK); } else if (!banner_t) set_banner("KEY ENCRYPT");
        bw_cd = (u16)(bw_cd + dt);
        if (bw_cd < 300) return;
    }
    bw_cd = 0;
    for (q = 0; q < MAXE; q++) if (en[q].type == 0 && en[q].hid == 7) en[q].st = E_NONE;
    bst = 3; bw_x = 0; bw_y = 0; wv_k = 0; bst_t = 1800;
    set_banner("WEAR OPTOVISOR"); sfx(S_PICK);
    cs_ph = 2; cs_n = 0; cs_w = 5;
}
static void intro_setup(void) {
    u8 b = 0, q, r;
    Enemy *e;
    for (q = 0; q < MAXE; q++) if (en[q].type == 0 && en[q].st != E_NONE && en[q].hid != 7) b = q;
    e = &en[b];
    bike_map(); tp_on = 0; bw_on = 0; sp_t = 0; dev_on = 0;
    px = 4096; py = 14848; pang = 384; ppitch = 0; pz = 0; pvz = 0;
    e->x = 3800; e->y = (s16)(py - 520); e->st = E_IDLE; e->shown_f = 255; e->shown_v = 255; e->hid = 0; en_act[b] = 1;
    en[b ^ 1].st = E_NONE;
    r = spawn_enemy(4120, e->y, 0, 0);                                 /* its bike: a prop (rider-less pictures from the artist) */
    if (r) { en[r - 1].hid = 7; en[r - 1].pt = 0; en[r - 1].shown_f = 255; en[r - 1].shown_v = 255; en[r - 1].fk = 255; }
    gs_invalidate();
    bw_y = 4; bst_t = 0; obj_dirty = 1; wp_shown = 255; wp_busy = 0; wp_pal_dirty = 1; pal_cur = 255;
}
static void scene_player(u16 dt) {                                   /* the hero in a cut-scene: no pad, he runs, turns his head or rides by himself */
    moving = 0; ppitch = 0; pz = 0; pvz = 0;
    if (hurt_t > dt) hurt_t -= dt; else hurt_t = 0;
    if (flash_t > dt) flash_t -= dt; else flash_t = 0;
    if (blink_t) { blink_t += dt; if (blink_t >= 50) blink_t = 0; }
    if (bst == 13) { arrival_step(dt); return; }
    if (bw_y == 2) {                                                  /* runs to the teleport */
        s16 dx = (s16)(tp_x - px), dy = (s16)(tp_y - py);
        s32 cr = (s32)COSA(pang) * dy - (s32)SINA(pang) * dx, dp = (s32)COSA(pang) * dx + (s32)SINA(pang) * dy, lim = (s32)dist2d(dx, dy) * 24;
        if (dp < 0 || cr > lim || cr < -lim) pang = (u16)((cr >= 0 ? pang + 6 * dt : pang - 6 * dt) & 511);
        else {
            u16 k;
            for (k = 0; k < dt; k++) try_move_player((s16)(((s32)COSA(pang) * 14) >> 8), (s16)(((s32)SINA(pang) * 14) >> 8));
            moving = 1; bobph += (u16)(9 * dt);
            stp_acc = (u16)(stp_acc + (u16)(14 * dt)); if (stp_acc >= 430) { stp_acc = (u16)(stp_acc - 430); sfx(S_STEP); }
        }
    } else if (bw_y >= 5 && (s16)pang < 475) pang = (u16)((pang + 3 * dt) & 511);      /* he turns his head to the right */
}
static void bike_advance(u16 dt) {                             /* the bike moves on EVERY frame (the world logic only runs every second frame): smooth motion */
    u8 b = bike_boss(); Enemy *e = &en[b], *ob = &en[b ^ 1];
    py = (s16)(py - BIKE_SPD * dt);
    if (py < 6144) { py += 9216; e->y += 9216; if (ob->st != E_NONE) ob->y += 9216; }
    e->y = (s16)(py - 600);
    if (ob->st != E_NONE && ob->hid == 2) ob->y = (s16)(e->y + 60);
    if (xp_t) xp_y = (s16)(e->y + 60);                              /* the explosion stays on the spot where the barrel lay */
    dev_on = 0;                                                      /* no visors on the bike */
}
#define BIKE_X0 3700
#define BIKE_X1 4500
static void bike_update(void) {
    u8 b = bike_boss(), o = (u8)(b ^ 1), fire = (u8)(bw_on != 0), sg;
    u16 d = g_dt;
    Enemy *e = &en[b], *ob = &en[o];
    bw_on = 0;
    if (CSP) return;
    if (blink_t >= 24) { bike_exit(); return; }                      /* the screen is dark: the glyph room takes its place */
#ifdef DBG_EXIT
    if (last_fc > DBG_EXIT) { bike_exit(); return; }
#endif
    bobph += (u16)(9 * d);
#ifdef DBG_SPIN
    if (last_fc > 1300) sp_t = 60;
#endif
    if (sp_t) sp_t = (u8)(sp_t > d / 4 ? sp_t - d / 4 : 0);
    if (e->x == bw_x || (bw_x != BIKE_X0 && bw_x != BIKE_X1)) bw_x = (e->x < 4100) ? BIKE_X1 : BIKE_X0;     /* the boss sweeps across the road from border to border (faster while it spins) */
    e->x = (s16)(e->x + clamp16((s16)(bw_x - e->x), (s16)(-(sp_t ? 12 : 5) * (s16)d), (s16)((sp_t ? 12 : 5) * (s16)d)));
    e->y = (s16)(py - 600); e->st = E_IDLE; e->fire_t = 0; e->hp = (s16)sq_n[0]; en_act[b] = 1;
    sg = (u8)(e->x > px ? 2 : 1);
    if (bw_y > d) bw_y = (s16)(bw_y - d); else bw_y = 0;
    if (bk_sg && bk_sg != sg && ob->st == E_NONE && !sp_t && !bw_y) {     /* it crosses the hero's axis: it throws a barrel straight down that axis */
        u8 r = spawn_enemy(e->x, (s16)(e->y + 100), 6, 0);
        if (r) { ob = &en[r - 1]; ob->st = E_IDLE; ob->t = 0; ob->hp = 99; ob->bt = 0; ob->hid = 0; ob->fk = 255; ob->pt = 0; en_act[r - 1] = 1; sfx(S_ESHOT); e->fire_t = 14; }
    }
    bk_sg = sg;
    if (ob->st != E_NONE) {
        s16 dy = (s16)(ob->y - py), dx = (s16)(ob->x - px);
        ob->st = E_IDLE; en_act[o] = 1; ob->z = 0;
        if (ob->hid == 0) {                                         /* it comes at the hero along a vertical line (it moves 1 a tick against the bike's 6) */
            ob->y = (s16)(ob->y + 4 * (s16)d);
            dy = (s16)(ob->y - py);
            if (dy > -140) {
                if (iabs(dx) < 300) {                               /* run into it: only the life bar suffers */
                    php = (s16)(php - 20); hurt_t = 14; shake = 6; sfx(S_HURT); cr_t = 90; cr_x = (u8)(72 + (rngNext() & 111)); cr_y = (u8)(40 + (rngNext() & 31));     /* the crack on the screen for 3 s, no explosion */
                    ob->st = E_NONE; bw_y = 60;
                    if (php <= 0) { php = PLAYER_HP; set_banner("AGAIN"); }
                } else if (dy > 140) { ob->hid = 1; sfx(S_RICO); }  /* dodged: it turns round and flies back along the same vertical line */
            }
        } else if (ob->hid == 1) {                                  /* back to the boss's horizontal axis, straight, no aiming at the boss */
            ob->y = (s16)(ob->y - 24 * (s16)d);
            if (ob->y <= e->y + 60) {
                ob->y = (s16)(e->y + 60);
                if (iabs((s16)(ob->x - e->x)) < 200) { add_fx(ob->x, ob->y, 0); sfx(S_EHIT); ob->st = E_NONE; bw_y = 40; }     /* it hits the boss and is gone */
                else { ob->hid = 2; ob->pt = 1500; sfx(S_RICO); }                                                         /* beside the boss: it stays there for 10 s */
            }
        } else {                                                    /* it lies on the boss's axis: the hero may shoot it */
            ob->y = (s16)(e->y + 60);
            if (fire && iabs((s16)(ob->x - px)) < 300) {
                boom(ob->x, ob->y); sp_t = 60; shake = 6; ob->st = E_NONE; bw_y = 60; e->bt++;     /* the explosion on the barrel; the boss spins for 5 s */
                if (e->bt >= 5 && !blink_t) { blink_t = 1; blink_tp = 1; sfx(S_VOFF); }          /* the fifth barrel: the picture goes dark, the hero arrives at the glyph wall */
            } else if (ob->pt > d) ob->pt = (u16)(ob->pt - d);
            else { ob->st = E_NONE; bw_y = 20; }                    /* not shot in 10 s: it vanishes */
        }
    }
}

static void cs_flash_end(void) {                                    /* the white flash is over */
    if (bst == 9 && bw_y == 3) { intro_setup(); cs_ph = 0; cs_n = 0; }   /* the cut-scene: the road is there, no black card */
    else { cs_ph = 2; cs_n = 0; }
}

static void wire_update(void) {
    u8 k;
    if (!wr_t && wr_w && (u16)(wr_pl | (wr_ph << 8)) >= wr_len()) { wr_t = 1; set_banner("CURRENT FLOWS"); sfx(S_PICK); flash_t = 3; }
    if (wr_t) {
        if (!eye_f) wr_t++;
        if (wr_t > 40 && !eye_f) { eye_f = 2; blink_t = 1; blink_tp = 1; sfx(S_VOFF); }      /* the eyes close ... */
        else if (wr_t > 40 && blink_t >= 24) {               /* ... the dark room is bright when they open: on to the last fight */
            for (k = 0; k < MAXE; k++) if (en[k].type == 0 && en[k].st != E_NONE) {
                Enemy *e = &en[k];
                e->hp = (s16)sq_n[0]; e->mark = e->hp; e->ph = 0; e->pt = 600; e->shotcd = 90; e->x = 6272; e->y = 640; e->st = E_IDLE; BHN = 0; bw_cd = 120;
            }
            { u8 q; for (q = 0; q < 128; q++) obj_cur[q] = obj_pal[q]; obj_dirty = 1; wr_pf = 0; }      /* the sprite palettes go back */
            bw_clear(); bst = 4; tv_buf[61] = 0; wv_k = 0; bw_x = 0; bw_y = 0; bw_cd = 60; bst_t = 0; wr_t = 0; sfx(S_PICK);
            eye_f = 1; wp_shown = 255; wp_busy = 0; wp_pal_dirty = 1;
        }
    }
}

static void boss_cheat(void) {                       /* debug: RIGHT RIGHT LEFT LEFT R L skips the boss minigame that is on */
    u8 k, b = 255;
    Enemy *e;
    for (k = 0; k < MAXE; k++) if (en[k].type == 0 && en[k].st != E_NONE) b = k;
    if (b == 255) return;
    e = &en[b];
    if (bst == 0 || (bst == 7 && bw_y < 5)) { bst = 0; bw_y = 4; boss_next(e); }
    else if (bst == 7) bst_t = 230;
    else if (bst == 8) { bw_clear(); bw_x = 0; bst = 7; bst_t = 150; sfx(S_PICK); }
    else if (bst == 1) boss_next(e);
    else if (bst == 10) { if (!blink_t) { blink_t = 1; blink_tp = 1; } }
    else if (bst == 9) { u8 q; for (q = 0; q < MAXE; q++) if (en[q].type == 0 && en[q].hid == 7) en[q].st = E_NONE; tp_on = 0; bw_y = 7; cs_ph = 2; cs_n = 0; cs_w = 2; }
    else if (bst == 13) py = 1010;
    else if (bst == 2) { tp_on = 0; bst = 3; bw_x = 0; bw_y = 0; wv_k = 0; bst_t = 1800; bw_cd = 0; e->pt = 0; }
    else if (bst == 3) glyph_done(e);
    else if (bst == 12) wr_t = 41;
    else if (bst == 4) { tv_buf[58] = 0; tv_buf[61] = 0x5A; e->hp = 3; e->mark = 3; }
    else return;
    set_banner("CHEAT: SKIP");
}

static void cheat_feed(u16 pressed) {
    static const u16 seq[6] = { 0x0100, 0x0100, 0x0200, 0x0200, 0x0010, 0x0020 };
    static const u16 seq3[6] = { 0x0800, 0x0800, 0x0400, 0x0400, 0x0020, 0x0020 };     /* up up down down L L: the level is won at once (next stage) */
    if (pressed & 0x0c20) {
        if (pressed & seq3[ch3_i]) { if (++ch3_i >= 6) { ch3_i = 0; if (!won && !dead) { won = 1; win_t = 0; sfx(S_WIN); set_banner("CHEAT: NEXT STAGE"); } } }
        else ch3_i = (u8)((pressed & 0x0800) ? 1 : 0);
    }
    if (!(pressed & 0x0330)) return;
    if (pressed & seq[ch_i]) { if (++ch_i >= 6) { ch_i = 0; boss_cheat(); } }
    else ch_i = (u8)((pressed & seq[0]) ? 1 : 0);
}

static u8 room_pt(u8 r, u8 j, s16 *x, s16 *y) {              /* the room's 5 entry points: fixed open cells of its rectangle in the hero's sector */
    u8 w = (u8)(room_rc[r][2] - room_rc[r][0] + 1), h = (u8)(room_rc[r][3] - room_rc[r][1] + 1);
    u16 n = (u16)(w * h), c0 = (u16)((j * 41u + r * 13u) % n), c, t;
    static const s16 mar[3] = { 170, 92, 40 };       /* v13: narrow rooms (2 tiles wide) have no cell with the 170 margin: the fight never started and the doors stayed locked */
    u8 m;
    for (m = 0; m < 3; m++) {
        c = c0;
        for (t = 0; t < n; t++) {
            *x = (s16)(((room_rc[r][0] + c % w) << 8) + 128); *y = (s16)(((room_rc[r][1] + c / w) << 8) + 128);
            if (!blocked(*x, *y, mar[m]) && sec_at(*x, *y) == psec) return 1;
            c = (u16)((c + 13) % n);
        }
    }
    return 0;
}

static void boss_helper(void) {
    u8 k;
    if ((bst == 0 || bst == 4 || bst == 7) && !CSP) {                                           /* wave 1 and the last fight: while the boss is up, one helper (quanton or plasma) joins it so the hero is pulled away from the boss */
        u8 boss = 0, fr = 0, al = 0;
        for (k = 0; k < MAXE; k++) { if (en[k].type == 0 && en[k].st >= E_IDLE && en[k].st != E_DEAD && en[k].st != E_TELE) boss = 1; if (en[k].st == E_NONE) fr = 1; if ((en[k].type == 5 || en[k].type == 6) && en[k].st != E_NONE) al = 1; }
        if (!boss) { if (bst == 4) BHN = 0; }
        else if (fr && !al && BHN < (u8)(bst == 4 ? 0 : 3)) {                            /* 4 helpers per life: class 5, 6, 5, 6, one at a time */
            if (bw_cd > g_dt) bw_cd = (u16)(bw_cd - g_dt);
            else { u8 j = (u8)(rngNext() % 6); if (boss_spawn((u8)(bst != 4 ? 5 : ((BHN & 1) ? 6 : 5)), bsp[j][0], bsp[j][1], 6)) { BHN++; bw_cd = (u16)(240 + (rngNext() & 127)); sfx(S_SPAWN); } else bw_cd = 30; }
        }
    }
}

static void crystal_pos(void) {                    /* the last crystal and its eyes stand by the glyph wall, not under the boss */
    static const s16 cp[5][2] = { { BK_X - 384, 420 }, { BK_X + 384, 420 }, { BK_X, 440 }, { BK_X - 256, 400 }, { BK_X + 256, 400 } };
    u8 j;
    bcry_x = cp[2][0]; bcry_y = cp[2][1];
    for (j = 0; j < 5; j++) if (!blocked(cp[j][0], cp[j][1], 60)) { bcry_x = cp[j][0]; bcry_y = cp[j][1]; break; }
}

/* the eye in the boss room (walking minigames only: waves, barrels, the last fight): it marks a spot for 10 s, then jumps to another one.
   Put the redvisor on while it is there and a medkit appears on that spot for 10 s; it is seen only through the redvisor and only from close by.
   Take the redvisor off: the medkit moves to where the eye is now. State: tv_buf[62] = 128 | eye spot | medkit spot << 3 (these slots belong to the wires minigame otherwise) */
static const s16 ey_p[8][2] = { { 2688, 520 }, { 3328, 840 }, { 3968, 520 }, { 4608, 860 }, { 5248, 520 }, { 5888, 840 }, { 6528, 520 }, { 4352, 360 } };
static u8 eye_spot(u8 skip) {
    u8 t, k, c, ok;
    for (t = 0; t < 12; t++) {
        c = (u8)(rngNext() & 7);
        if (c == skip || blocked(ey_p[c][0], ey_p[c][1], 60)) continue;
        if (bcry_on && dist2d(ey_p[c][0] - bcry_x, ey_p[c][1] - bcry_y) < 500) continue;
        ok = 1;
        for (k = 0; k < MAXE; k++) if (en[k].type == 0 && en[k].st != E_NONE && dist2d(ey_p[c][0] - en[k].x, ey_p[c][1] - en[k].y) < 600) ok = 0;
        if (ok) return c;
    }
    return (u8)((skip + 3) & 7);
}
static void eye_update(void) {
    u8 now = (u8)(last_fc >> 3), ea, ma, on, k, boss = 0;
    u8 tx = (u8)(px >> 8), ty = (u8)(py >> 8);
    if (!tour_done || !(bst == 0 || bst == 1 || bst == 4) || CSP || dead) return;
    for (k = 0; k < MAXE; k++) if (en[k].type == 0 && en[k].st >= E_IDLE && en[k].st != E_DEAD && en[k].st != E_TELE) boss = 1;
    if (!(have_d & 4) || !boss || tx < 8 || tx > 27 || ty < 1 || ty > 3) { if (ey_a & 128) { ey_a = 0; ey_f = 0; } return; }
    if (!(ey_a & 128)) { ey_a = (u8)(128 | eye_spot(8)); ey_f = 1; ey_te = (u8)(now + 75); ey_tm = 0; }
    ea = (u8)(ey_a & 7); ma = (u8)((ey_a >> 3) & 7);
    on = (u8)((dev_on & 4) ? 1 : 0);
    if (on && !(ey_f & 4) && !(ey_f & 2)) { ma = ea; ey_f |= 2; ey_tm = (u8)(now + 75); }      /* the redvisor goes on: a medkit at the old spot of the eye */
    if (!on && (ey_f & 4) && (ey_f & 2)) ma = ea;                                               /* it goes off: the medkit moves to where the eye is now */
    ey_f = (u8)((ey_f & ~4) | (on << 2));
    if ((u8)(now - ey_te) < 128) { ea = eye_spot(ea); ey_te = (u8)(now + 75); }
    if ((ey_f & 2) && (u8)(now - ey_tm) < 128) ey_f &= (u8)~2;
    if ((ey_f & 2) && on && medkits < 5 && dist2d(px - ey_p[ma][0], py - ey_p[ma][1]) < 300) { medkits++; ey_f &= (u8)~2; flash_t = 5; set_banner("MEDKIT"); sfx(S_MEDKIT); }
    ey_a = (u8)(128 | ea | (ma << 3));
#ifdef DBG_EYE
    tv_buf[37] = (u8)((ey_a & 63) | ((ey_f & 3) << 6));

#endif
}

static u8 pause_tick(u16 pressed) {                 /* START: the game stands still (not during cards, cut-scenes, death) */
    if (info_win || tour || CSP || dead || won) { gpause = 0; return 0; }
    if (pressed & KEY_START) { gpause ^= 1; if (gpause) sfx(S_BLIP); else banner_t = 0; }
    if (gpause) set_banner("PAUSE - PRESS START");
    return gpause;
}
static void world_update(void) {
    if (gpause) return;
    u8 i, k, tx = (u8)(px >> 8), ty = (u8)(py >> 8);
#ifdef DBG_B10END
    { if (bst == 10 && last_fc > DBG_B10END) { cs_ph = 0; bike_exit(); } }
#endif
#ifdef DBG_B1END
    { if (bst == 1 && last_fc > DBG_B1END) { u8 q; for (q = 0; q < MAXE; q++) if (en[q].type == 0 && en[q].st != E_NONE) { boss_next(&en[q]); break; } } }
#endif
    if (dev_on && vis_block()) { dev_on = 0; sfx(S_VOFF); set_banner("VISORS BLOCKED"); }       /* the visors are off (and locked) while enemies fight: power and frame rate */
#ifdef DBG_FIN
    { static u8 dn; if (!dn && bst == 3 && last_fc > 920) { dn = 1; bw_y = 3; wv_k = 3; bw_x = 720; } }
#endif
#ifdef DBG_KILLF
    { if (last_fc > DBG_KILLF && (last_fc % 150) < 2) { u8 q; for (q = 0; q < MAXE; q++) if (en[q].type == 6 && en[q].hp != 99 && en[q].st != E_NONE) { en[q].st = E_DEAD; en[q].t = 10; } } }
#endif
#ifdef DBG_BOSS
    { static u8 dn; if (!dn && last_fc > 900 && !info_win) { dn = 1; spawn_enemy(BK_X, 450, 0, 0); en[0].st = E_IDLE; if (bst == 9) { bst_t = 0; } if (bst == 10) { bike_enter();
#ifdef DBG_T10
 bst_t = DBG_T10;
#endif
 } } }
#endif
#ifdef DBG_ATK
    { static u8 dn; if (!dn && last_fc > 900 && !info_win) { dn = 1;
        if (DBG_ATK == 1) { cr_t = 140; cr_x = 120; cr_y = 60; }
        if (DBG_ATK == 2) { bm_t = 200; bm_x = 40; bm_y = 50; }
        if (DBG_ATK == 3) { wh_t = 150; }
        if (DBG_ATK == 4) { s16 vx, vy; aim(px, py - 600, px, py, 10, 0, &vx, &vy); add_bolt(px, py - 600, vx, vy, 2); }
        if (DBG_ATK == 5) { have_d = 7; bcry_on = 1; bcry_x = px; bcry_y = (s16)(py - 350); }
    } }
#endif
    if (tour_done) {                                                   /* enemy attack timers */
        if (rl_cd > g_dt) rl_cd -= g_dt; else rl_cd = 0;
        if (kz_cd > g_dt) kz_cd -= g_dt; else kz_cd = 0;
        if (ar_t) ar_t--;
        if (xp_t) xp_t--;
        if (cr_t) cr_t--;                                              /* the crack goes away by itself after ~5 s (one tick = 2 frames) */
        if (bm_t > g_dt) bm_t -= (u8)g_dt; else bm_t = 0;
        if (wh_t > g_dt) { wh_t -= (u8)g_dt; if (wh_t > 30 && !(wh_t % 20)) sfx(S_RICO); } else wh_t = 0;      /* the ringing in the ears */
    }
    if (bst == 11 && !CSP) {                                          /* the last scene: the camera looks at the boss, then the exit teleport lights up, the boss runs into it, the camera stays on the teleport */
        u8 gone = 1;
        bst_t = (u16)(bst_t + g_dt);
        if (bst_t >= 40 && !tp_on) { tp_on = 1; tp_x = (s16)((EXIT_X0 + EXIT_X1) / 2); tp_y = (s16)((EXIT_Y0 + EXIT_Y1) / 2); sfx(S_SPAWN); }
        for (k = 0; k < MAXE; k++) if (en[k].type == 0 && en[k].st != E_NONE) gone = 0;
        if (gone) { if (bw_cd > g_dt) bw_cd = (u16)(bw_cd - g_dt); else { tp_on = 0; bst = 6; crystals = 9; set_banner("EXIT OPEN"); flash_t = 5; sfx(S_PICK); } }
    }
    if (bst == 10) { bike_update(); return; }
    if (bst == 12) { wire_update(); return; }
    if (bst == 9 && CSP == 2) { bike_enter(); bst = 10; return; }
    if (bst == 9) return;                                            /* the cut-scene: only the actors (enemy_update) move, nothing spawns */
    if (bst == 13) return;
    if (eye_f == 1 && !blink_t && !CSP) { eye_f = 0; cs_ph = 2; cs_n = 0; cs_w = 3; wp_shown = 255; wp_busy = 0; wp_pal_dirty = 1; }      /* the eyes are open: the black card of the last fight */
    sectors_update();
    /* doors: the locks are recomputed only when a room changes state */
    for (i = 0; i < NUM_ROOMS; i++) if (rst_old[i] != rst[i]) { rst_old[i] = rst[i]; locks_dirty = 1; }
    for (i = 0; i < NUM_DOORS; i++) {
        u8 kind = door_xyk[i][2];
        s16 cx = (s16)(((s16)door_xyk[i][0] << 8) + 128), cy = (s16)(((s16)door_xyk[i][1] << 8) + 128);
        u16 d;
        if (!sec_on(door_sec[i][0]) && !sec_on(door_sec[i][1])) continue;
        d = dist2d(px - cx, py - cy);
        if (locks_dirty) { u8 lk = door_locked(i); if (lk != door_lk[i]) { door_lk[i] = lk; set_door(i, lk ? 0 : (u8)(door_st[i] != 0)); } }
        if (door_lk[i]) { if (d < 400 && !banner_t) set_banner("LOCKED"); continue; }
        if (door_st[i] == 2) continue;
        if (kind == 0) {
            u8 near_e = 0, occ = 0;
            for (k = 0; k < MAXE; k++) {
                u16 de;
                if (en[k].st < E_IDLE || en[k].st == E_DEAD) continue;
                de = dist2d(en[k].x - cx, en[k].y - cy);
                if (de < 400) near_e = 1;                    /* an enemy that has seen the hero walks through the door */
                if (de < 420) occ = 1;
            }
            if ((d < 480 || near_e) && !door_st[i]) { door_st[i] = 1; set_door(i, 1); sfx(S_DOOR); }
            else if (d > 600 && !occ && door_st[i]) { door_st[i] = 0; set_door(i, 0); }
        } else if (kind < 4) {
            if (d < 400) {
                if (keys & (1 << (kind - 1))) { door_st[i] = 2; set_door(i, 1); set_banner("ACCESS GRANTED"); }
                else if (!banner_t) set_banner(key_msg[kind - 1]);
            }
        }
    }
    locks_dirty = 0;
    if (!trd_open && (dev_on & 4) && dist2d(px - PLATE_X, py - PLATE_Y) < 420) {      /* the TR button is seen only through the redvisor */
        if (!gate_pow) { if (!banner_t) set_banner("NO POWER"); }
        else {
            trd_open = 1;
            for (i = 0; i < NUM_DOORS; i++) if (door_xyk[i][2] == 4) { door_st[i] = 2; set_door(i, 1); }
            set_banner("BOSS GATE OPEN"); sfx(S_DOOR);
        }
    }
    if ((dev_on & 2) && !gate_pow) {                                 /* optovisor: reading the wall text powers the main gate */
        s16 tsx, tsf, tf;
        if (project(TXT_X, TXT_Y, &tsx, &tsf, &tf) && tf < 2400 && tsx > 20 && tsx < 236 && col_visible(tsx, tf)) {
            read_t = (u16)(read_t + g_dt);
            if (read_t > 30) { gate_pow = 1; ghost_act = 1; rst[LOOK_ROOM] = 2; room_left[LOOK_ROOM] = 18; wv_sp[LOOK_ROOM] = 0; wv_dead[LOOK_ROOM] = 0; wv_t[LOOK_ROOM] = 60; set_banner("GATE POWER ON"); sfx(S_PICK); }
        } else if (read_t) read_t = 0;
    }
    if (bst == 8) {                                                   /* the last fall: three fighters of class 6 (two slots), then the boss gets up and the old sequence goes on */
        u8 alive = 0;
        for (k = 0; k < MAXE; k++) if (en[k].type == 6 && en[k].st != E_NONE) alive = 1;
        if (bw_x < 3) {
            if (!alive) {
                if (bw_cd > g_dt) bw_cd = (u16)(bw_cd - g_dt);
                else if (boss_spawn(6, bsp[bw_x * 2][0], bsp[bw_x * 2][1], 20)) { bw_x++; bw_cd = 60; sfx(S_SPAWN); }
                else bw_cd = 30;
            }
        } else if (!alive) { bw_x = 0; bst = 7; bst_t = 150; sfx(S_PICK); }
    }
    boss_helper();
    arena_update(tx, ty, g_dt);
    if (ghost_act && bst < 4 && ghost_n < 18 && rst[LOOK_ROOM] == 2 && !wv_t[LOOK_ROOM] && wv_sp[LOOK_ROOM] < 3) {
        u8 fr = 0;
        for (k = 0; k < MAXE; k++) if (en[k].st == E_NONE) fr = 1;
        if (fr && spawn_enemy(ghost_pos[wv_sp[LOOK_ROOM]][0], ghost_pos[wv_sp[LOOK_ROOM]][1], 6, 0)) { ghost_n++; wv_sp[LOOK_ROOM]++; sfx(S_SPAWN); }
    }
    /* items (only the ones near enough to matter) */
    for (i = 0; i < NUM_ITEMS; i++) {
        if (item_got[i] || !sec_on(item_sec[i])) continue;
        if (iabs(px - item_pos[i][0]) > 320 || iabs(py - item_pos[i][1]) > 320) continue;
        if (dist2d(px - item_pos[i][0], py - item_pos[i][1]) > 300) continue;
        k = item_kind[i];
        item_got[i] = 1;
        flash_t = 5;
        sfx(k >= 5 && k != 8 && k != 15 && k != 16 ? S_WEAPON : S_PICK);
        if (k == 0) { u8 r; for (r = 0; r < NUM_ROOMS; r++) if (rst[r] == 0 && room_rc[r][4] == 1 && (item_pos[i][0] >> 8) >= room_rc[r][0] && (item_pos[i][0] >> 8) <= room_rc[r][2] && (item_pos[i][1] >> 8) >= room_rc[r][1] && (item_pos[i][1] >> 8) <= room_rc[r][3]) { rst[r] = 2; set_banner("LOCKED"); }
                       crystals++; set_banner(crystals >= 9 ? "EXIT OPEN" : "CRYSTAL"); }
        else if (k == 1) { super_t = 1800; set_banner("NO RADIATION 30 S"); }
        else if (k == 8) { nv_t = 10800; have_d |= 1; set_banner("NIGHTVISOR"); }
        else if (k == 15) { have_d |= 2; vis_t[1] = 10800; set_banner("OPTOVISOR"); }
        else if (k == 16) { have_d |= 4; vis_t[2] = 10800; set_banner("REDVISOR"); }
        else if (k >= 18) trg_apply(k, item_zone[i]);
        else if (k <= 4) { keys |= (u8)(1 << (k - 2)); set_banner("GOT ACCESS CARD"); }
        else { u8 w = (u8)(k == 17 ? 7 : k <= 7 ? k - 4 : k - 8); have_w |= (u8)(1 << w); if (w != cur_w) { w_rel[cur_w] = 0; w_left[cur_w] = 0; w_ammo[cur_w] = w_amax[cur_w]; } cur_w = w; cls_sel = 0; wp_shown = 255; wp_pal_dirty = 1; set_banner(w_name[w]); }
    }
    if (bcry_on && (dev_on & 4) && dist2d(px - bcry_x, py - bcry_y) < 300) {      /* the last crystal is seen and taken only through the redvisor: the boss blows up */
        bcry_on = 0; flash_t = 5; sfx(S_PICK);
        for (k = 0; k < MAXE; k++)
            if (en[k].type == 0 && en[k].st != E_NONE && en[k].st != E_DEAD && en[k].st != E_TELE) {      /* "all crystals found", then the camera turns to the boss: it opens a teleport and vanishes in it */
                bst = 11; bst_t = 0; tp_x = en[k].x; tp_y = en[k].y; en[k].st = E_IDLE; bw_clear(); bw_cd = 45; cs_ph = 1; cs_n = 0; cs_w = 1; sfx(S_EDIE); break; }
    }
    drops_update();
    eye_update();
    for (i = 0; i < 4; i++) {
        if (drops[i].on == 1 && dist2d(px - drops[i].x, py - drops[i].y) < 300) {
            if (medkits >= 5) continue;                      /* the counter is full: it stays on the floor */
            drops[i].on = 0;
            medkits++;
            flash_t = 5; set_banner("MEDKIT"); sfx(S_MEDKIT);
        }
    }
    /* enemy spawn points: a few checked per frame; far away they go back to sleep. waves of up to 2 on screen */
    for (i = 0; i < NUM_SPAWNS; i++) if (sp_cd[i]) sp_cd[i] = (u8)(sp_cd[i] > 2 ? sp_cd[i] - 2 : 0);          /* world_update runs every second frame */
    {
        u8 freeslot = 0, tries;
        for (k = 0; k < MAXE; k++) if (en[k].st == E_NONE) freeslot = 1;
        for (tries = 0; tries < 3 && freeslot; tries++) {
            u16 d;
            i = sp_rr; sp_rr = (u8)(sp_rr + 1 >= NUM_SPAWNS ? 0 : sp_rr + 1);
            s16 spx = spawn_pos[i][0], spy = spawn_pos[i][1];
            if (sp_st[i] != 0 || sp_cd[i] || !sp_left[i]) continue;
            if (spawn_room[i] == 255 && !sec_on(sp_sec[i])) continue;
            if (spawn_cls[i] == 5 && !trd_open) continue;
            if (spawn_room[i] != 255) { u8 rr = spawn_room[i]; if (rst[rr] != 2 || (rr != LOOK_ROOM && wv_sp[rr] >= wv_sz(rr)) || !room_pt(rr, (u8)((sp_rr + kills) % 5), &spx, &spy)) continue; d = 0; }       /* the rooms: continuous, from 5 points, until the hero has cleared them */
            else {
                if (iabs(px - spx) > 2400 || iabs(py - spy) > 2400) continue;
                d = dist2d(px - spx, py - spy);
            }
            {   /* close spawns only out of the hero's sight (the skip is relaxed after 40 tries) */
                s16 vx, vf, vs; u8 ty = cls_type[spawn_cls[i]];
                if (project(spx, spy, &vx, &vs, &vf) && col_visible(vx, vf) && dist2d(px - spx, py - spy) < (u16)(near_r(ty) * 3) && sk_n < 40) { sk_n++; continue; }
            }
            if (spawn_room[i] != 255 || (d < 2400 && (d < 1000 || los(spx, spy, px, py)))) {
                if (spawn_enemy(spx, spy, cls_type[spawn_cls[i]], i)) { sk_n = 0; sp_st[i] = 1; sp_left[i]--; if (spawn_room[i] != 255) wv_sp[spawn_room[i]]++; sfx(S_SPAWN); }
                break;
            }
        }
    }
    for (i = 0; i < MAXE; i++) {
        if (en[i].st == E_NONE || en[i].st == E_DEAD || en[i].st == E_TELE) continue;
        if (en[i].type != 6 && bst < 2 && dist2d(px - en[i].x, py - en[i].y) > 3800 && !scr_vis[i]) { sp_st[en[i].sp] = 0; sp_left[en[i].sp]++; if (spawn_room[en[i].sp] != 255 && wv_sp[spawn_room[en[i].sp]]) wv_sp[spawn_room[en[i].sp]]--; en[i].st = E_NONE; }
    }
    /* exit */
    if (px >= EXIT_X0 && px < EXIT_X1 && py >= EXIT_Y0 && py < EXIT_Y1 && !won) {
        if (crystals >= 9) { won = 1; win_t = 0; sfx(S_WIN); }
        else if (!banner_t) set_banner("NEED ALL CRYSTALS");
    }
    (void)tx; (void)ty;
}

/* ---------------------------------------------------------------- scene reset */
static u8 mus_mode = 0;                       /* 0 calm (State), 1 battle (Darker), 2 boss (WIRED96R) */
static u8 mus_boss;                           /* 1 = the boss module is loaded instead of the music module */
#include "snd/music_pos.h"

/* calm and battle songs live in ONE module: switching = a jump to another order (no upload, no lag) */
static void mus_set(u8 mode) {
    if (mode == mus_mode) return;
    mus_mode = mode;
    if (mode == 2) {
        snesmodStop();
        snesmodLoadModule(MOD_BOSS);
        sfx_init();
        snesmodSetModuleVolume(160);
        snesmodPlay(0);
        mus_boss = 1;
        return;
    }
    if (mus_boss) {
        snesmodStop();
        snesmodLoadModule(MOD_MUSIC);
        sfx_init();
        snesmodSetModuleVolume(160);
        mus_boss = 0;
    }
    snesmodPlay(mode == 1 ? MUS_POS_BATTLE : MUS_POS_CALM);
}

static void respawn_player(void) {                  /* 3 resurrections on the spot; then back to the start point */
    u8 i;
    if (fk_inst) { fk_inst = 0; cp_goto(); }        /* lava / electro water / dark hole: back to the last checkpoint, no resurrection used */
    else if (lives) lives--;
    else {                                          /* out of resurrections: thrown to the last checkpoint (the start at first), the fights restart */
        u8 r;
        lives = 3;
        cp_goto();
        for (r = 0; r < NUM_ROOMS; r++) {
            if (rst[r] != 1 && rst[r] != 2) continue;
            rst[r] = 4;                             /* dormant: doors unlock, the fight restarts when the hero comes back */
            for (i = 0; i < MAXE; i++)
                if (en[i].st != E_NONE && en[i].st != E_DEAD && en[i].type != 6 && spawn_room[en[i].sp] == r) {
                    sp_st[en[i].sp] = 0; sp_left[en[i].sp]++; if (wv_sp[r]) wv_sp[r]--; en[i].st = E_NONE;       /* unkilled enemies vanish for now */
                }
        }
        locks_dirty = 1;
    }
    if (bst == 8) { bw_clear(); bw_x = 0; bw_cd = 90; }
    if (bst == 7) { bst = 0; }
    if (bst == 3) { bw_clear(); bw_x = 0; bw_y = 0; wv_k = 0; bst_t = 1800; have_d |= 2; }               /* the decryption minigame starts again */
    if (bst == 2) pick_tp();
    ppitch = 0; pz = 0; pvz = 0; dh_d = 0; fk_t = 0; tp_lock = 0;
    medkits = 5;                                    /* every resurrection: a full set of medkits again */
    php = PLAYER_HP; dead = 0; hurt_t = 0; flash_t = 0; shake = 0; rad_t = 0; pl_shot_t = 0;
    for (i = 0; i < MAXB; i++) bolts[i].on = 0;
    set_banner("RESURRECTED");
}

static void new_game(void) {
    u8 i;
    lives = 3;
    if (tour_done) ATK_RESET();
    bst = 0; bst_t = 0; tp_on = 0; bw_on = 0; bw_cd = 120; bw_y = 0; bw_x = 0; eye_f = 0;
    mus_set(0);
#ifdef DBG_MUS
    mus_set(2);
#endif
    px = START_X; py = START_Y; pang = 384; ppitch = 0; pz = 0; pvz = 0;
    cp_cur = 0; fk_om[0] = fk_om[1] = fk_om[2] = 0; fk_inst = 0; fk_t = 0; dh_d = 0; tp_lock = 0;
    php = PLAYER_HP; dead = 0; won = 0; kills = 0; hurt_t = 0; flash_t = 0;
    fire_cd = 0; wp_t = 0; wp_lower = 0; wp_frame = 0; shake = 0; bobph = 0;
    for (i = 0; i < MAXE; i++) { en[i].st = E_NONE; en[i].shown_f = 255; en[i].shown_v = 255; en[i].up = 0; }
    for (i = 0; i < MAXB; i++) bolts[i].on = 0;
    for (i = 0; i < MAXFX; i++) fxs[i].on = 0;
    for (i = 0; i < MAXE; i++) scr_vis[i] = 0;
    for (i = 0; i < NUM_SPAWNS; i++) { sp_st[i] = 0; sp_cd[i] = 0; sp_left[i] = spawn_cnt[i]; }
    for (i = 0; i < NUM_ROOMS; i++) { rst[i] = 0; room_left[i] = 0; }
    for (i = 0; i < NUM_ROOMS; i++) room_left[i] = room_tot[i];          /* totals and per-point counts come from the map (rooms: multiplier x enemies) */
    for (i = 0; i < NUM_DOORS; i++) door_lk[i] = 0;
    sectors_init(); psec = 255;
    ghost_act = 0; ghost_n = 0; ghost_kill = 0; BHN = 0; wv_k = 0;
    for (i = 0; i < 4; i++) drops[i].on = 0;
    bcry_on = 0;
    nv_t = 0; rad_t = 0; medkits = 5; a_hold = r_hold = 0; pl_shot_t = 0;
    for (i = 0; i < NUM_ROOMS; i++) { wv_sp[i] = 0; wv_dead[i] = 0; wv_n[i] = 0; wv_t[i] = 0; }
    gpause = 0;
    info_win = 1; info_row = 0; info_t = 0; info_pg = 0;                      /* the briefing card at the start of the level */
#if defined(DBG_CS) && !defined(DBG_INFO)
    info_win = 0;
#endif
    for (i = 0; i < NUM_ITEMS; i++) item_got[i] = 0;
    for (i = 0; i < NUM_ITEMS; i++) if (item_kind[i] == 0) {          /* the crystal lying next to the boss is taken out: the boss hands it over */
        u8 q; for (q = 0; q < NUM_SPAWNS; q++) if (spawn_cls[q] == 5 && dist2d(item_pos[i][0] - spawn_pos[q][0], item_pos[i][1] - spawn_pos[q][1]) < 768) item_got[i] = 1;
    }
    for (i = 0; i < NUM_DOORS; i++) { door_st[i] = 0; set_door(i, 0); }
    trd_open = 0; crystals = 0; keys = 0; have_w = 1; cur_w = 0; super_t = 0; banner_t = 0; vis_t[0] = vis_t[1] = vis_t[2] = 0; koze_t = 0;
    blink_t = 0; x_hold = 0;
    have_d = 0; nv_gone = 0; dev_on = 0; cls_sel = 0; cur_d = 0; gate_pow = 0; read_t = 0;
    cs_ph = 0; cs_n = 0; cs_w = 0; cs_win = 0; cs_dim = 0;
#ifdef DBG_CS
    tour_done = 1; bst = DBG_BST; if (DBG_BST == 12) { wire_init(); wp_shown = 255; wp_busy = 0; wp_pal_dirty = 0;
#ifdef DBG_WS
        wr_s = DBG_WS; wr_calc();
#endif
    } if (DBG_BST == 10) bst_t = 3600; cs_ph = DBG_CS; cs_w = DBG_CW; bcry_on = (DBG_BST == 5); crystal_pos(); if (DBG_BST == 2) { bw_cd = 0; pick_tp(); }
#endif
#ifdef DBG_TP
    ch_i = 0; eye_f = 0; px = DBG_TPX; py = DBG_TPY; pang = DBG_TPA; trd_open = DBG_TRO; have_d = 7; vis_t[0] = vis_t[1] = vis_t[2] = 10800; dev_on = DBG_DEV;
#ifdef DBG_GP
    trd_open = 0; gate_pow = 1;
#endif
#endif
    for (i = 0; i < NW; i++) { w_left[i] = 0; w_ammo[i] = w_amax[i]; w_rel[i] = 0; }
    wp_shown = 255; wp_pal_dirty = 1;
    set_banner("FIND 9 CRYSTALS");
}

static const u8 note_of[8] = { 255, 255, 1, 4, 0, 2, 3, 255 };
static void draw_weapon(u16 fc) {
    s16 ox = 16, oy = 94, bx, by;
    if (CSP == 2 && note_of[cs_w & 7] != 255) { oamHideRange(0, 4); oamHideRange(OAM_CHG, 8); wp_want = (u8)(8 + note_of[cs_w & 7]); if (wp_shown == wp_want && !(fc & 7)) { wp_busy = 1; wp_row = 0; } return; }      /* the note's tiles are written again every 8 frames: a lost write heals */      /* the small note line lives in the weapon tile slot */
    if (bst == 12) { oamHideRange(0, 4); oamHideRange(OAM_CHG, 8); wp_want = 0; return; }
    if (bst == 10) {                                            /* the bike: the handlebars (left / straight / right) in the weapon's tile slot, a 128x32 strip */
        oamHideRange(OAM_CHG, 8);
        wp_want = hb_s;
        if (!(fc & 15)) wp_pal_dirty = 1;
        if (wp_shown == wp_want && !(fc & 31) && !wp_busy) { wp_busy = 1; wp_row = 0; }
        oamSet(0, 64, (u16)(VIEW_Y + 96), 128, 1, 2, 0); oamSetSize(0, 1);
        oamSet(1, 96, (u16)(VIEW_Y + 96), 132, 1, 2, 0); oamSetSize(1, 1);
        oamSet(2, 128, (u16)(VIEW_Y + 96), 192, 1, 2, 0); oamSetSize(2, 1);
        oamSet(3, 160, (u16)(VIEW_Y + 96), 196, 1, 2, 0); oamSetSize(3, 1);
        if (wp_shown == 255 || CSP) oamHideRange(0, 4);
        return;
    }
    u8 f = (u8)(cur_w == 7 ? wp_frame : 0);                /* one picture only: changing the tiles takes 4 blanks and showed two halves of different frames */
    s16 ph = (s16)(bobph & 255);
    s16 amp = running ? 7 : 4;
    bx = (s16)((SINA(bobph * 2 / 3) * amp) >> 8);
    by = (s16)((iabs(COSA(bobph * 2 / 3)) * (amp - 1)) >> 8);
    if (!moving) { bx = (s16)((SINA(fc * 3) * 1) >> 8); by = (s16)((SINA(fc * 2) * 1) >> 8); }
    (void)ph;
    ox += bx;
    oy += by + wp_lower * 2;
    if (dead) oy += 70;
    if (pl_shot_t && !dead) oy += (s16)(pl_shot_t >= 12 ? 4 : pl_shot_t >= 8 ? 2 : 1);      /* a small recoil: the weapon never leaves the screen */
    if (cls_sel && !dead) {                               /* the visor menu: no weapon, the three visor icons instead */
        u8 i;
        oamHide(0); oamHide(1); oamHide(2); oamHide(3);
        koze_on = 0; rail_on = 0;
        for (i = 0; i < 8; i++) oamHide((u8)(OAM_CHG + i));
        /* no N / O / R panel: the strip at the bottom names the visor and its ON / OFF */
        return;
    }
      /* the dark stage: the weapon is not seen either */
    wp_want = f;                                          /* the tiles are copied in the blank (upload_pending side) */
    oamSet(0, (u16)ox,        (u16)oy,        128, 1, 2, 0); oamSetSize(0, 1);
    oamSet(1, (u16)(ox + 32), (u16)oy,        132, 1, 2, 0); oamSetSize(1, 1);
    oamSet(2, (u16)ox,        (u16)(oy + 32), 192, 1, 2, 0); oamSetSize(2, 1);
    oamSet(3, (u16)(ox + 32), (u16)(oy + 32), 196, 1, 2, 0); oamSetSize(3, 1);
    if (wp_shown == 255) { oamHide(0); oamHide(1); oamHide(2); oamHide(3); }       /* tiles still on their way to VRAM */
    {                                                     /* shot effects: flash at the muzzle (in the sprite); beamchain: a thin beam, rocket: a blast at the cross-hair,
                                                             railgun: white screen, kozematy: two blue lines (BG2 row) */
        u8 i;
        s16 mx = (s16)(ox + weapon_muz[cur_w][0]), my = (s16)(oy + weapon_muz[cur_w][1]);
        for (i = 0; i < 8; i++) oamHide((u8)(OAM_CHG + i));
        koze_on = (u8)(koze_t && !dead ? (koze_t * 16 + 9) / 10 : 0);   /* the two lines shrink to a point at the cross-hair */
        if (pl_shot_t >= 11 && !dead && cur_w != 7 && shot_w != 3 && shot_w != 4 && shot_w != 5) {      /* the muzzle flash is a sprite of its own now */
            oamSet((u8)(OAM_CHG + 7), (u16)(mx - 8), (u16)(my - 8), (u16)((8 + 2) * 16 + 8 + 4), 2, 3, 0); oamSetSize((u8)(OAM_CHG + 7), 0);
        }
        rail_on = (u8)(shot_w == 3 && pl_shot_t >= 11 && !dead);
        if (pl_shot_t && !dead) {
            if (shot_w == 5 && pl_shot_t >= 8 && mx < 120) {
                u8 cellg = (u8)(((104 - my) * 100 / (128 - mx)) > -26 ? 62 : 63);
                u8 fl = (u8)(my < 104 ? 0x80 : 0);
                for (i = 0; i < 8; i++) {
                    s16 cx = (s16)(mx + 16 * i + 8), cy;
                    if (cx - 8 >= 124) break;
                    cy = (s16)(my + (s16)(((s32)(104 - my) * (cx - mx)) / (128 - mx)));
                    oamSet((u8)(OAM_CHG + i), (u16)(cx - 8), (u16)(cy - 8), glyph_tile(cellg), 2, 3, fl);
                    oamSetSize((u8)(OAM_CHG + i), 0);
                }
            } else if (shot_w == 4 && pl_shot_t >= 4) {
                u8 age = (u8)(14 - pl_shot_t);
                s16 dd = (s16)(age * 3);
                oamSet(OAM_CHG, 120, (u16)(VIEW_Y + 56), (u16)((8 + 2) * 16 + 8 + 4), 2, 3, 0); oamSetSize(OAM_CHG, 0);
                if (age >= 2) {
                    oamSet((u8)(OAM_CHG + 1), (u16)(120 - dd), (u16)(VIEW_Y + 56 - dd), (u16)((8 + 2) * 16 + 8 + 6), 2, 3, 0); oamSetSize((u8)(OAM_CHG + 1), 0);
                    oamSet((u8)(OAM_CHG + 2), (u16)(120 + dd), (u16)(VIEW_Y + 56 - dd), (u16)((8 + 2) * 16 + 8 + 6), 2, 3, 0x40); oamSetSize((u8)(OAM_CHG + 2), 0);
                    oamSet((u8)(OAM_CHG + 3), (u16)(120 - dd), (u16)(VIEW_Y + 56 + dd), (u16)((8 + 2) * 16 + 8 + 6), 2, 3, 0x80); oamSetSize((u8)(OAM_CHG + 3), 0);
                    oamSet((u8)(OAM_CHG + 4), (u16)(120 + dd), (u16)(VIEW_Y + 56 + dd), (u16)((8 + 2) * 16 + 8 + 6), 2, 3, 0xC0); oamSetSize((u8)(OAM_CHG + 4), 0);
                }
                if (age >= 4) { oamSet((u8)(OAM_CHG + 5), 120, (u16)(VIEW_Y + 56), (u16)((8 + 4) * 16 + 8), 2, 3, 0); oamSetSize((u8)(OAM_CHG + 5), 0); }
            }
        }
    }
}

#define OAM_BOLT 5
#define OAM_FX   13
#define OAM_ENEMY 21
#define OAM_HUD  53

/* choose the weapon lowering from the wall distance straight ahead */
static void update_weapon_lowering(u16 dt) {
    u8 c;
    s16 minz = 32000, tgt;
    for (c = VIEW_COLS / 2 - 20; c < VIEW_COLS / 2 + 20; c += 3) {
        s16 z = (s16)zbuf_w[c];
        if (z < minz) minz = z;
    }
    tgt = 0;
    if (minz < 300) tgt = (s16)((300 - minz) / 3);       /* close to a wall: put the weapon away */
    if (tgt > 38) tgt = 38;
    if (wp_lower < tgt) wp_lower += 3 * dt;
    if (wp_lower > tgt) wp_lower -= 3 * dt;
    if (wp_lower < 0) wp_lower = 0;
}

/* ---- the close-up half of the hybrid renderer: the Super FX scales the enemy (gsu_s.sfx) --------------------- */
typedef struct { s16 c0, nc, u0, ust, v0, vst, ys, nr; u8 on, fr; } GsP;
static GsP gsp[MAXE];
static u8 gs_tp[MAXE] = { 255, 255 }, gs_fr[MAXE] = { 255, 255 };
static void gs_invalidate(void) { u8 k; for (k = 0; k < MAXE; k++) { gs_tp[k] = 255; gs_fr[k] = 255; } }      /* the next painted frame is loaded again */
#define GS_C0 0x40
static void gs_box(u8 slot, Enemy *e, s16 f, s16 sx) {
    u32 sc = enemy_kc[e->type] / (u32)(f < 64 ? 64 : f);
    s16 D = (s16)((sc * 289) >> 10), zpx, top, left, cs, nc, skip, c0, n, ys, sk, nr, vst, feet;
    s16 bt, bb, bl, br;
    u8 frame = enemy_frame(e), flip;
    GsP *g = &gsp[slot];
    if (D > 160) D = 160;
    if (D < 16) return;
    if (sx > e->lsx) e->side = 0; else if (sx < e->lsx) e->side = 1;
    e->lsx = sx;
    flip = (e->st == E_STRAFE && e->side) ? 1 : 0;
    feet = (s16)(g_horizon + ((s32)g_cam_h8 * 128) / f);
    zpx = (s16)(((s32)e->z * 128) / f);
    bt = enemy_bb[e->type][0][frame][0]; bb = enemy_bb[e->type][0][frame][1];
    bl = enemy_bb[e->type][0][frame][2]; br = enemy_bb[e->type][0][frame][3];
    if (flip) { s16 t = bl; bl = (s16)(63 - br); br = (s16)(63 - t); }
    top = (s16)(feet - (s16)(((s32)(bb + 1) * D) >> 6) - zpx);
    left = (s16)((sx - (D >> 1)) & ~1);
    scr_x0[slot] = (s16)(left + (s16)(((s32)bl * D) >> 6));
    scr_x1[slot] = (s16)(left + (s16)(((s32)(br + 1) * D) >> 6));
    scr_y0[slot] = (s16)(top + (s16)(((s32)bt * D) >> 6));
    scr_y1[slot] = (s16)(top + (s16)(((s32)(bb + 1) * D) >> 6));
    scr_f[slot] = f;
    scr_vis[slot] = col_visible((s16)(sx < 0 ? 0 : (sx > 255 ? 255 : sx)), f);
    vst = (s16)(16384 / D);
    cs = (s16)(left >> 1);
    nc = (s16)((D + 1) >> 1);
    skip = (s16)(cs < 0 ? -cs : 0);
    c0 = (s16)(cs + skip);
    n = (s16)(nc - skip);
    if (c0 + n > 128) n = (s16)(128 - c0);
    if (n <= 0) return;
    ys = top; sk = 0;
    if (ys < 0) { sk = (s16)(-ys); ys = 0; }
    nr = (s16)(top + D);
    if (nr > 128) nr = 128;
    nr = (s16)(nr - ys);
    if (nr <= 0) return;
    g->c0 = c0; g->nc = n;
    if (flip) { g->u0 = (s16)(16383 - skip * 2 * vst); g->ust = (s16)(-2 * vst); }
    else { g->u0 = (s16)(skip * 2 * vst); g->ust = (s16)(2 * vst); }
    g->v0 = (s16)(sk * vst); g->vst = vst; g->ys = ys; g->nr = nr;
    g->fr = frame;
    g->on = 1;
}

#define GPICK(t, h) ((t) == 1 ? gspr_e1_##h : (t) == 2 ? gspr_e2_##h : (t) == 3 ? gspr_e3_##h : (t) == 4 ? gspr_e4_##h : (t) == 5 ? gspr_e5_##h : (t) == 6 ? gspr_e6_##h : gspr_e0_##h)
static void gs_one(u8 i) {
    GsP *g = &gsp[i];
    u8 t = en[i].type, fr = g->fr;
    if (gs_tp[i] != t || gs_fr[i] != fr || ((last_fc + i * 32) & 63) < 2) {      /* also refreshed once a second: a stale picture in the Game Pak RAM can not stay */                   /* the frame's 2 KB: ROM -> WRAM ($7F) by DMA, then WRAM -> Game Pak RAM */
        const u8 *src = (fr < 10) ? GPICK(t, a) + (u16)(fr * 2048) : GPICK(t, b) + (u16)((fr - 10) * 2048);
        u32 a = (u32)src;
        *(volatile u8 *)0x2181 = 0; *(volatile u8 *)0x2182 = 0; *(volatile u8 *)0x2183 = 1;
        *(volatile u8 *)0x4340 = 0x00; *(volatile u8 *)0x4341 = 0x80;
        *(volatile u8 *)0x4342 = (u8)a; *(volatile u8 *)0x4343 = (u8)(a >> 8); *(volatile u8 *)0x4344 = (u8)(a >> 16);
        *(volatile u8 *)0x4345 = 0x00; *(volatile u8 *)0x4346 = 0x08;
        *(volatile u8 *)0x420B = 0x10;
        if (i) gsMove1(); else gsMove0();
        gs_tp[i] = t; gs_fr[i] = fr;
    }
    *GP_U16(GS_C0) = (u16)g->c0; *GP_U16(GS_C0 + 2) = (u16)g->nc; *GP_U16(GS_C0 + 4) = (u16)g->u0; *GP_U16(GS_C0 + 6) = (u16)g->ust;
    *GP_U16(GS_C0 + 8) = (u16)g->v0; *GP_U16(GS_C0 + 10) = (u16)g->vst; *GP_U16(GS_C0 + 12) = (u16)g->ys; *GP_U16(GS_C0 + 14) = (u16)g->nr;
    *GP_U16(GS_C0 + 16) = (u16)(0xC000 + (u16)i * 0x800); *GP_U16(GS_C0 + 18) = (u16)scr_f[i];
    gsuStartCached(0);
    gsuWait();
}

static void gs_draw(void) {
    u8 a = 0, b = 1;
    if (!gsp[0].on && !gsp[1].on) return;
    if (gsp[0].on && gsp[1].on && scr_f[1] > scr_f[0]) { a = 1; b = 0; }      /* far one first */
    gsuLoadS();
    if (gsp[a].on) gs_one(a);
    if (gsp[b].on) gs_one(b);
}

static u8 gm_occl(s16 sx, s16 sy, s16 f) {                     /* a close enemy painted by the Super FX hides floor objects (portals, holes) that are behind it */
    u8 o;
    for (o = 0; o < MAXE; o++)
        if (en[o].gm && gsp[o].on && scr_f[o] < f && sx + 16 >= scr_x0[o] && sx - 16 <= scr_x1[o] && sy + 6 >= scr_y0[o] && sy - 30 <= scr_y1[o]) return 1;
    return 0;
}

static void draw_world_sprites(void) {
    u8 order[MAXE], i, j, n = 0, id;
    s16 sx[MAXE], sfeet[MAXE], f[MAXE];
    for (i = 0; i < MAXE; i++) {
        scr_vis[i] = 0;
        gsp[i].on = 0;
        if (en[i].st == E_NONE || !en_act[i]) continue;
        if (!project(en[i].x, en[i].y, &sx[i], &sfeet[i], &f[i])) continue;
        if (sfeet[i] > 127) sfeet[i] = 127;                  /* very close: the feet stay at the bottom edge of the view, never lifted off the floor */
        if (en[i].type == 0 && bst == 1 && !(dev_on & 1)) continue;          /* wave 2: the boss is only seen through the nightvisor */
        if (en[i].type == 2 && !(have_d & 1) && !nv_gone) continue;          /* cloaked: only the nightvisor shows them, always */
        if (en[i].type == 6 && !(have_d & 2) && bst != 10 && bst != 1 && bst != 3 && !(bst == 4 && en[i].hp == 99) && !BHN) continue;          /* class 6: only the optovisor shows them */
        order[n++] = i;
    }
    /* nearest first (lowest OAM id = front) */
    for (i = 0; i + 1 < n; i++)
        for (j = i + 1; j < n; j++)
            if (f[order[j]] < f[order[i]]) { u8 t = order[i]; order[i] = order[j]; order[j] = t; }
    id = OAM_ENEMY;
    for (i = 0; i < MAXE; i++, id += 16) {
        Enemy *e;
        u8 slot, frame, lvl, pal, flip, cd, nch, cx, cy, vis;
        u16 nb;
        s16 left, top, z, bt, bb, bl, br, x0, x1, xs;
        oamHideRange(id, 16);
        if (i >= n) continue;
        slot = order[i];
        e = &en[slot];
        if (e->gm) { gs_box(slot, e, f[slot], sx[slot]); continue; }  /* close: the Super FX paints it into the picture */
        if (e->shown_f == 255) continue;                             /* nothing uploaded yet */
        frame = e->shown_f;                                          /* what is really in VRAM */
        lvl = e->shown_v;
        cd = (lvl < 3) ? 64 : (lvl < 6 ? 32 : 16);
        nch = cd >> 4;
        z = (s16)(((s32)e->z * 128) / f[slot]);
        if (e->st == E_TELE) { if (e->t > 22) continue; z = 0; }                  /* materialises inside the teleport cylinder */
        bt = enemy_bb[e->type][lvl][frame][0]; bb = enemy_bb[e->type][lvl][frame][1];
        bl = enemy_bb[e->type][lvl][frame][2]; br = enemy_bb[e->type][lvl][frame][3];
        if (sx[slot] > e->lsx) e->side = 0; else if (sx[slot] < e->lsx) e->side = 1;   /* heads face the way it moves */
        e->lsx = sx[slot];
        flip = (e->st == E_STRAFE && e->side) ? 0x40 : 0;
        if (flip) { s16 t = bl; bl = (s16)(cd - 1 - br); br = (s16)(cd - 1 - t); }
        left = sx[slot] - (cd >> 1);
        top = sfeet[slot] - (bb + 1) - z;                            /* the art's lowest row stands exactly on the floor line */
        x0 = left + bl;
        x1 = left + br;
        scr_x0[slot] = x0;
        scr_x1[slot] = x1;
        scr_y0[slot] = top + bt;
        scr_y1[slot] = top + bb;
        scr_f[slot] = f[slot];
        {                                                            /* behind a close-up enemy painted by the Super FX: hidden */
            u8 o;
            for (o = 0; o < MAXE; o++)
                if (o != slot && en[o].gm && gsp[o].on && f[o] < f[slot] && x0 <= scr_x1[o] && x1 >= scr_x0[o]) break;
            if (o < MAXE) continue;
        }
        if (top > 126 || top + cd < 0) continue;
        pal = (e->st == E_HURT) ? 3 : (u8)(slot ? 4 : 0);
        nb = (u16)(slot * 8);
        vis = 0;
        /* 16x16 pieces; each one is shown only where the wall in front of it is further than the enemy */
        for (cx = 0; cx < nch; cx++) {           /* cx = screen column */
            u8 ok, sc = flip ? (u8)(nch - 1 - cx) : cx;   /* sc = source tile column */
            xs = left + cx * 16;
            if (xs + 15 < x0 || xs > x1) continue;
            ok = (u8)col_visible((s16)(xs + 3), f[slot]) + (u8)col_visible((s16)(xs + 8), f[slot]) + (u8)col_visible((s16)(xs + 13), f[slot]);
            if (ok < 3) continue;
            vis = 1;
            for (cy = 0; cy < nch; cy++) {
                s16 ys = top + cy * 16;
                if (ys + 15 < top + bt || ys > top + bb || ys >= 128 || ys < 0) continue;
                oamSet((u8)(id + cx * nch + cy), (u16)xs, (u16)(ys + VIEW_Y), (u16)(nb + sc * 2 + cy * 32), pal, 2, flip);
                oamSetSize((u8)(id + cx * nch + cy), 0);
            }
        }
        scr_vis[slot] = vis;
    }
}

/* decide which animation frame / size level each enemy shows this frame. The tiles themselves are
   copied in the blank right after (upload_pending), before the picture starts, so the OAM
   built from these numbers and the tile data always belong together. */
static void fade_pal(u8 slot, u8 type, u8 k) {
    const u16 *bp = bg_pal[pal_cur < 9 ? pal_cur : 0];
    u16 tmp[16];
    u8 i;
    for (i = 0; i < 16; i++) {
        u16 c = enemy_pal_data[type][i], t = bp[1];
        u16 r = (u16)((((c & 31) * (16 - k)) + ((t & 31) * k)) >> 4);
        u16 g = (u16)(((((c >> 5) & 31) * (16 - k)) + (((t >> 5) & 31) * k)) >> 4);
        u16 b = (u16)(((((c >> 10) & 31) * (16 - k)) + (((t >> 10) & 31) * k)) >> 4);
        tmp[i] = (i == 0) ? 0 : (u16)(r | (g << 5) | (b << 10));
    }
    set_obj_pal(slot ? 4 : 0, tmp);
}

#ifndef GM_ON
#define GM_ON 2000000000UL                       /* the scale from which the Super FX paints an enemy into the picture (hysteresis: GM_OFF) */
#define GM_OFF 1999000000UL
#endif
static void plan_enemy_sprites(void) {
    u8 i, budget = 4;
    for (i = 0; i < MAXE; i++) {
        Enemy *e = &en[i];
        u8 frame, lvl, cost;
        s16 sx, sfeet, f;
        u32 sc;
        if (e->st == E_NONE) { e->gm = 0; continue; }
        if (!project(e->x, e->y, &sx, &sfeet, &f)) { e->gm = 0; continue; }
        sc = enemy_kc[e->type] / (u32)(f < 64 ? 64 : f);
        if (IS_PROP(e)) { e->gm = 0; if (e->fk != 0) { e->fk = 0; set_obj_pal(i ? 4 : 0, prop_pal); } }
        else if (e->gm) { if (sc < GM_OFF) e->gm = 0; }
        else if (sc >= GM_ON && (e->st != E_TELE || e->t <= 22)) e->gm = 1;   /* close: straight into the painted form, also while it materialises */
        if (!IS_PROP(e)) {                                   /* the colours slide to the wall palette on the way to the Super FX picture, and back */
            u8 k = 0;                   /* v13: the painter is off, so the sprite keeps its own palette (the blend aimed at the old 4 bpp BG colours and turned close enemies into a dark silhouette) */
            (void)sc;
            if (k != e->fk) { e->fk = k; fade_pal(i, e->type, k); }
        }
        frame = enemy_frame(e);
        lvl = enemy_level(e->type, f, e->shown_v);
        if (e->shown_f != frame || e->shown_v != lvl || (e->shown_f != 255 && ((last_fc + (u16)i * 16) & ((e->type == 0 && (bst == 2 || bst == 3 || bst == 9 || bst == 10 || bst == 13)) ? 15 : 63)) < 2)) {      /* also uploaded again every so often: a DMA that missed the blank leaves a half-painted picture */
            cost = (lvl < 3) ? 3 : 1;
            if (cost > budget) continue;
            e->shown_f = frame;
            e->shown_v = lvl;
            e->up = 1;
            budget -= cost;
        }
    }
}

static void upload_pending(void) {
    u8 i;
    for (i = 0; i < MAXE; i++) {
        if (en[i].up) {
            { if (IS_PROP(&en[i])) upload_prop(i, en[i].shown_v); else upload_enemy(i, en[i].type, en[i].shown_v, en[i].shown_f); }
            en[i].up = 0;
        }
    }
}

static void draw_bolts_fx(void) {
    u8 i, id = OAM_BOLT;
    s16 sx, sfeet, f;
    for (i = 0; i < MAXB; i++) {
        Bolt *b = &bolts[i];
        if (b->on && project(b->x, b->y, &sx, &sfeet, &f) && col_visible(sx, f)) {
            s16 y = g_horizon + (s16)(((s32)g_cam_h8 * 128 - 22 * 128) / f) - 8;
            if (b->kind == 0) {
                oamSet(id, (u16)(sx - 8), (u16)(y + VIEW_Y), (u16)((8 + 0) * 16 + 8), 2, 2, 0);
                oamSetSize(id, 0);
            } else if (b->kind == 2) {                                   /* rocket: red ball with a white ring */
                oamSet(id, (u16)(sx - 8), (u16)(y + VIEW_Y), (u16)((8 + 0) * 16 + 8 + 4), 2, 2, 0);
                oamSetSize(id, 0);
            } else if (f < 1000 && big_shown == 255 && big_want == 255) {
                oamSet(id, (u16)(sx - 16), (u16)(y - 8 + VIEW_Y), (u16)(256 + 12 * 16), 2, 2, (u8)((((last_fc >> 2) + i) & 3) << 6));
                oamSetSize(id, 1);
            } else {
                oamSet(id, (u16)(sx - 8), (u16)(y + VIEW_Y), (u16)((8 + 2) * 16 + 8 + (i % 3) * 2), 2, 2, 0);
                oamSetSize(id, 0);
            }
        } else {
            oamHide(id);
        }
        id++;
    }
    for (i = 0; i < 4; i++) oamHide((u8)(OAM_OVAL + i));
    for (i = 0; i < MAXE; i++) {                                   /* spawn holes: a black oval on the floor */
        if (en[i].st == E_TELE && (en[i].type != 6 || (have_d & 2) || bst == 3 || BHN) && project(en[i].x, en[i].y, &sx, &sfeet, &f) && col_visible(sx, f) && !gm_occl(sx, sfeet, f)) {
            s16 y = (s16)(sfeet - 9 + VIEW_Y);
            oamSet((u8)(OAM_OVAL + i * 2), (u16)(sx - 16), (u16)y, (u16)((8 + 4) * 16 + 10), 2, 1, 0);
            oamSetSize((u8)(OAM_OVAL + i * 2), 0);
            oamSet((u8)(OAM_OVAL + i * 2 + 1), (u16)sx, (u16)y, (u16)((8 + 4) * 16 + 12), 2, 1, 0);
            oamSetSize((u8)(OAM_OVAL + i * 2 + 1), 0);
        }
    }
    if ((dev_on & 1) && !(en[0].st == E_TELE || en[1].st == E_TELE)) {   /* night vision: crisp [ ] marks on the two nearest doors / gates */
        s16 bf[2], bsx[2], bsf[2]; u8 n = 0, j;
        bf[0] = bf[1] = 32000;
        for (i = 0; i < NUM_DOORS; i++) {
            s16 dsx, dsf, df, ddx = (s16)(door_xyk[i][0] * 256 + 128 - px), ddy = (s16)(door_xyk[i][1] * 256 + 128 - py);
            if (iabs(ddx) + iabs(ddy) > 2600 || !project((s16)(door_xyk[i][0] * 256 + 128), (s16)(door_xyk[i][1] * 256 + 128), &dsx, &dsf, &df) || df > 2200 || dsx < 24 || dsx > 232 || !col_visible(dsx, (s16)(df - 200))) continue;     /* walls hide the mark */
            if (df < bf[0]) { bf[1] = bf[0]; bsx[1] = bsx[0]; bsf[1] = bsf[0]; bf[0] = df; bsx[0] = dsx; bsf[0] = dsf; }
            else if (df < bf[1]) { bf[1] = df; bsx[1] = dsx; bsf[1] = dsf; }
        }
        for (j = 0; j < 2; j++) {
            s16 hw, ym;
            if (bf[j] == 32000) continue;
            hw = (s16)(16384 / bf[j]); if (hw > 100) hw = 100;
            ym = (s16)(2 * g_horizon - bsf[j] - 4 + VIEW_Y);
            oamSet((u8)(OAM_OVAL + n), (u16)(bsx[j] - hw - 8), (u16)ym, glyph_tile(GLYPH_BRL), 2, 3, 0); oamSetSize((u8)(OAM_OVAL + n), 0); n++;
            oamSet((u8)(OAM_OVAL + n), (u16)(bsx[j] + hw), (u16)ym, glyph_tile(GLYPH_BRR), 2, 3, 0); oamSetSize((u8)(OAM_OVAL + n), 0); n++;
        }
    }
    if (bw_on && bst != 1 && bst != 10 && project(bw_x, bw_y, &sx, &sfeet, &f) && col_visible(sx, f)) put_text((u8)OAM_OVAL, (s16)(sx - 24), (s16)(sfeet - 8 + VIEW_Y), "----");
    for (i = 0; i < 4; i++) oamHide((u8)(OAM_TXT + 4 + i));
    if (bst == 1 && bw_on == 1 && !CSP && (dev_on & 1) && dist2d(bw_x - px, bw_y - py) < 1400 && ((last_fc >> 3) & 1)) {      /* a barrel comes: blinking jump arrows on both sides of the hero */
        oamSet((u8)(OAM_TXT + 4), 36, (u16)(VIEW_Y + 52), glyph_tile(GLYPH_ARN), 2, 3, 0); oamSetSize((u8)(OAM_TXT + 4), 0);
        oamSet((u8)(OAM_TXT + 5), 204, (u16)(VIEW_Y + 52), glyph_tile(GLYPH_ARN), 2, 3, 0); oamSetSize((u8)(OAM_TXT + 5), 0);
    }
    for (i = 0; i < MAXE; i++) {                                   /* the ring climbs the teleport cylinder, then sinks while the enemy appears */
        if (en[i].st == E_TELE && (en[i].type != 6 || (have_d & 2) || bst == 3 || BHN) && project(en[i].x, en[i].y, &sx, &sfeet, &f) && col_visible(sx, f)) {
            s16 h = (en[i].shown_v == 255) ? 40 : (s16)((en[i].shown_v < 3 ? 64 : (en[i].shown_v < 6 ? 32 : 16)) - 8);
            s16 fr = (s16)(en[i].t > 22 ? 44 - en[i].t : en[i].t);
            s16 y = (s16)(sfeet - 9 - (s16)(((s32)h * fr) / 22) + VIEW_Y);
            if (y < VIEW_Y - 8) y = VIEW_Y - 8;
            oamSet((u8)(OAM_TXT + 4 + i * 2), (u16)(sx - 16), (u16)y, (u16)((8 + 4) * 16 + 10), 2, 2, 0);
            oamSetSize((u8)(OAM_TXT + 4 + i * 2), 0);
            oamSet((u8)(OAM_TXT + 5 + i * 2), (u16)sx, (u16)y, (u16)((8 + 4) * 16 + 12), 2, 2, 0);
            oamSetSize((u8)(OAM_TXT + 5 + i * 2), 0);
        }
    }
    {                                                              /* exit portal in the boss room once all crystals are found */
        s16 ex = (s16)((EXIT_X0 + EXIT_X1) / 2), ey = (s16)((EXIT_Y0 + EXIT_Y1) / 2);
        u8 pshow = (u8)(crystals >= 9 && !won);
        u8 cp = 0;
        if (tp_on) { ex = tp_x; ey = tp_y; pshow = 1; }
        else if (!pshow) for (i = 0; i < 4; i++) if (drops[i].on == 2) { ex = drops[i].x; ey = drops[i].y; pshow = 1; cp = 1; }
        for (i = 0; i < 4; i++) oamHide((u8)(OAM_FX + MAXFX + i));
        if (pshow && project(ex, ey, &sx, &sfeet, &f) && col_visible(sx, f) && !gm_occl(sx, sfeet, f)) {
            s16 y0 = (s16)(sfeet - 9 + VIEW_Y), up = (s16)(((SINA(last_fc * 5) + 256) * 22) >> 9);
            if (cp) { y0 = (s16)(g_horizon + (s16)(((s32)(g_cam_h8 - 504) * 128) / f) + VIEW_Y); up = 0; }
            oamSet((u8)(OAM_FX + MAXFX), (u16)(sx - 16), (u16)y0, (u16)((8 + 4) * 16 + 10), 2, 2, 0); oamSetSize((u8)(OAM_FX + MAXFX), 0);
            oamSet((u8)(OAM_FX + MAXFX + 1), (u16)sx, (u16)y0, (u16)((8 + 4) * 16 + 12), 2, 2, 0); oamSetSize((u8)(OAM_FX + MAXFX + 1), 0);
            oamSet((u8)(OAM_FX + MAXFX + 2), (u16)(sx - 16), (u16)(y0 - 6 - up), (u16)((8 + 4) * 16 + 10), 2, 2, 0); oamSetSize((u8)(OAM_FX + MAXFX + 2), 0);
            oamSet((u8)(OAM_FX + MAXFX + 3), (u16)sx, (u16)(y0 - 6 - up), (u16)((8 + 4) * 16 + 12), 2, 2, 0); oamSetSize((u8)(OAM_FX + MAXFX + 3), 0);
        }
    }
    id = OAM_FX;
    for (i = 0; i < MAXFX; i++) {
        Fx *x = &fxs[i];
        if (x->on && project(x->x, x->y, &sx, &sfeet, &f) && col_visible(sx, f)) {
            {
                s16 y = g_horizon + (s16)(((s32)g_cam_h8 * 128 - 32 * 128) / f) - 8;
                u8 base = (x->kind == 2) ? 12 : 6;
                u8 fr = (x->kind == 2) ? 12 : (u8)(6 + (x->f > 2 ? 2 : x->f));
                (void)base;
                oamSet(id, (u16)(sx - 8), (u16)(y + VIEW_Y),
                       (u16)((8 + (fr / 4) * 2) * 16 + 8 + (fr % 4) * 2), 2, 2, 0);
                oamSetSize(id, 0);
            }
        } else {
            oamHide(id);
        }
        id++;
    }
}

/* item icons live in free 16x16 cells: 0..7 in the FX region of table 0, 8..11 in glyph cells 44..47 of table 1 */
static const u8 fx_free[8] = { 1, 3, 4, 5, 11, 13, 14, 15 };
static u16 item_tile(u8 cell) {
    if (cell < 8) { u8 c = fx_free[cell]; return (u16)((8 + (c >> 2) * 2) * 16 + 8 + (c & 3) * 2); }
    if (cell < 10) return glyph_tile((u8)(cell - 8 + 46));
    if (cell >= 14) return glyph_tile((u8)(60 + cell - 14));
    if (cell == 12) return glyph_tile(37);
    if (cell == 13) return glyph_tile(39);
    cell = (u8)(cell - 10);
    return (u16)(256 + (12 + 2 * (cell >> 1)) * 16 + 12 + 2 * (cell & 1));
}

static u8 item_oam;
/* the 32x32 block of table 1 (the big plasma ball) is lent to the nearest item as a 2x copy of its icon */

/* weapon pick-ups: seven round icons (the user's sketch) are copied on demand into two shared 16x16 cells (glyph cells 54, 55) */
static u8 wik_want[2] = { 255, 255 }, wik_shown[2] = { 255, 255 }, wik_used;
static u8 wik_idx(u8 k) { return (u8)(k == 5 ? 0 : k == 6 ? 1 : k == 7 ? 2 : k == 12 ? 3 : k == 13 ? 4 : k == 14 ? 5 : 6); }
static s16 item_zh;                                   /* height above the floor of the item being drawn (a falling medkit) */
static void item_sprite(s16 wx, s16 wy, u8 k, u16 fc, u8 i) {
    s16 sx, sfeet, f, y;
    u8 cell, pal;
    if (item_oam >= OAM_ITEM + 8) return;
    if (!project(wx, wy, &sx, &sfeet, &f)) return;
    if (!col_visible(sx, f)) return;
    {
        u8 bi = (u8)(k == 5 || k == 6 || k == 7 || k == 12 || k == 13 || k == 14 || k == 17 ? 16 + wik_idx(k) : 255);
        if (bi == 255) {
            u8 c = (u8)(k == 0 ? 0 : k == 1 ? 2 : k <= 4 ? (u8)(4 + (k - 2) * 2) : k == 8 ? 12 : k == 9 ? 13 : k == 15 ? 14 : k == 16 ? 15 : 10);
            if (k <= 4) c = (u8)(c + ((fc >> 4) & 1));
            bi = c;
        }
        if (f < BIG_F && (big_cand == 255 || f < big_cand_f)) { big_cand = bi; big_cand_f = f; }
        if (f < BIG_F && bi == big_shown) {
            u8 bp = (bi >= 16) ? 6 : (u8)((k == 0 || k == 1 || k == 8 || k == 15 || k == 16) ? 5 : 7);
            y = (s16)(g_horizon + (s16)(((s32)g_cam_h8 * 128 - (20 + item_zh) * 128) / f) - 32 + ((SINA(fc * 4 + i * 40) * 3) >> 8));
            if (y > 94) y = 94;
            if (y < 0) return;
            oamSet(item_oam, (u16)(sx - 16), (u16)(y + VIEW_Y), (u16)(256 + 12 * 16), bp, 2, 0);
            oamSetSize(item_oam, 1);
            item_oam++;
            return;
        }
    }
    if (k == 5 || k == 6 || k == 7 || k == 12 || k == 13 || k == 14 || k == 17) {
        u8 ix = wik_idx(k), sl;
        if (wik_want[0] == ix && !(wik_used & 1)) sl = 0;
        else if (wik_want[1] == ix && !(wik_used & 2)) sl = 1;
        else if (!(wik_used & 1)) sl = 0;
        else if (!(wik_used & 2)) sl = 1;
        else return;
        wik_used |= (u8)(1 << sl); wik_want[sl] = ix;
        y = (s16)(g_horizon + (s16)(((s32)g_cam_h8 * 128 - (20 + item_zh) * 128) / f) - 16 + ((SINA(fc * 4 + i * 40) * 3) >> 8));
        if (y > 118) y = 118;
        if (y < 0) return;
        oamSet(item_oam, (u16)(sx - 8), (u16)(y + VIEW_Y), glyph_tile((u8)(54 + sl)), 6, 2, 0);
        oamSetSize(item_oam, 0);
        item_oam++;
        return;
    }
    cell = (u8)(k == 0 ? 0 : k == 1 ? 2 : k <= 4 ? (u8)(4 + (k - 2) * 2) : k <= 7 ? 10 : k == 8 ? 12 : k == 9 ? 13 : k == 15 ? 14 : k == 16 ? 15 : 10);
    if (k <= 7 || (k >= 12 && k <= 14)) cell = (u8)(cell + ((fc >> 4) & 1));
    pal = (k == 0 || k == 1 || k == 8 || k == 15 || k == 16) ? 5 : 7;
    y = (s16)(g_horizon + (s16)(((s32)g_cam_h8 * 128 - (20 + item_zh) * 128) / f) - 16 + ((SINA(fc * 4 + i * 40) * 3) >> 8));
    if (y > 118) y = 118;                                 /* close up: the item stays in view until picked up */
    if (y < 0) return;
    oamSet(item_oam, (u16)(sx - 8), (u16)(y + VIEW_Y), item_tile(cell), pal, 2, 0);
    oamSetSize(item_oam, 0);
    item_oam++;
}

#ifdef DBG_EYE2
#define DBGX 1
#else
#define DBGX 0
#endif
static void draw_items(u16 fc) {
    u8 i;
    item_oam = OAM_ITEM;
    wik_used = 0;
    big_cand = 255;
    for (i = 0; i < NUM_ITEMS; i++) {
        if (item_got[i] || !sec_on(item_sec[i])) continue;
        if (iabs(item_pos[i][0] - px) > 3600 || iabs(item_pos[i][1] - py) > 3600) continue;   /* far rooms cost nothing */
        item_sprite(item_pos[i][0], item_pos[i][1], item_kind[i], fc, i);
    }
    for (i = 0; i < 4; i++) if (drops[i].on) { item_zh = drops[i].z; item_sprite(drops[i].x, drops[i].y, 9, fc, (u8)(30 + i)); item_zh = 0; }
    if (bcry_on && (dev_on & 4)) item_sprite(bcry_x, bcry_y, 0, fc, 40);
    if ((ey_a & 128) && (ey_f & 2) && ((dev_on & 4) || DBGX) && (bst == 0 || bst == 1 || bst == 4) && dist2d(px - ey_p[(ey_a >> 3) & 7][0], py - ey_p[(ey_a >> 3) & 7][1]) < 640) item_sprite(ey_p[(ey_a >> 3) & 7][0], ey_p[(ey_a >> 3) & 7][1], 9, fc, 34);      /* the medkit of the eye: only through the redvisor and only close by */
    while (item_oam < OAM_ITEM + 8) { oamHide(item_oam); item_oam++; }
    big_want = big_cand;
    if (big_want == 255 && (look_vis & 2)) big_want = 24;      /* the eyes (TR button / redvisor mark) */
    else if (big_want == 255 && (look_vis & 4)) big_want = 25;      /* boss room: lamps + glyph wall */
    else if (big_want == 255 && (look_vis & 1)) big_want = 23;
    if (!(look_vis & 4)) for (i = 0; i < MAXB; i++) if (bolts[i].on && bolts[i].kind == 1) big_want = 255;     /* a plasma ball needs the block */
    if (cr_t && tour_done && !tour) big_want = 26;
    if (xp_t && tour_done && !tour) big_want = 27;                  /* the crack */
}

static void arrow_sprite(u8 id, s16 cx, s16 cy, s16 x, s16 y) {     /* x right, y up */
    s16 ax = iabs(x), ay = iabs(y);
    u8 cellg, fl = 0;
    if (ay * 12 > ax * 29) { cellg = GLYPH_ARN; if (y < 0) fl |= 0x80; }
    else if (ax * 12 > ay * 29) { cellg = GLYPH_ARE; if (x < 0) fl |= 0x40; }
    else { cellg = GLYPH_ARNE; if (x < 0) fl |= 0x40; if (y < 0) fl |= 0x80; }
    oamSet(id, (u16)(cx - 8), (u16)(cy - 8), glyph_tile(cellg), 2, 3, fl);
    oamSetSize(id, 0);
}

static void draw_aim(void) {
    u8 i, best = 255, id = OAM_AIM, n, k, cells;
    s16 bf = 32000, y, mx;
    for (i = 0; i < MAXE; i++) {
        if (!scr_vis[i] || en[i].st < E_IDLE || en[i].st == E_DEAD || BOSS_DOWN(i) || ((bst == 3 || bst == 13) && !en[i].type)) continue;      /* no life bar over the boss at the glyph wall */
        if (128 >= scr_x0[i] - 22 && 128 <= scr_x1[i] + 22 && 64 >= scr_y0[i] - 16 && 64 <= scr_y1[i] + 16 && scr_f[i] < bf) {
            bf = scr_f[i]; best = i;
        }
    }
    if (bst == 10 || bst == 9) best = 255;                      /* the bike: only the 5-barrel bar, no life bar over the boss */
    if (best != 255) {
        Enemy *e = &en[best];
        s16 hp = e->hp > 0 ? e->hp : 0;
        n = (e->type == 6 && bst >= 4) ? 20 : sq_n[e->type];
        cells = (u8)((n + 1) >> 1);
        y = (s16)((scr_y0[best] + scr_y1[best]) / 2 - 8);
        oamSet(id, (u16)(scr_x0[best] - 10), (u16)(y + VIEW_Y), glyph_tile(GLYPH_BRL), 2, 3, 0); oamSetSize(id, 0); id++;
        oamSet(id, (u16)(scr_x1[best] - 6), (u16)(y + VIEW_Y), glyph_tile(GLYPH_BRR), 2, 3, 0); oamSetSize(id, 0); id++;
        y = (s16)(scr_y0[best] - 12);
        if (y < 2) y = 2;
        mx = (s16)((scr_x0[best] + scr_x1[best]) / 2 - cells * 5);
        for (k = 0; k < cells; k++) {                               /* two small squares per cell */
            u8 g = (hp >= 2 * k + 2) ? GLYPH_SQ2 : (hp >= 2 * k + 1) ? GLYPH_SQ1 : GLYPH_SQ0;
            oamSet(id, (u16)(mx + k * 10), (u16)(y + VIEW_Y), glyph_tile(g), 2, 3, 0);
            oamSetSize(id, 0); id++;
        }
    }
    while (id < OAM_AIM + 12) { oamHide(id); id++; }
}

/* Wolf3D style minimap: BG2 overlay, 12x12 cells around the player, 4 px per cell */
#define MM_X 204
#define MM_Y 44
static u16 pal_t[272];                   /* static: the DMA source must not be a stack slot; also the minimap's tile staging (used only at start-up) */
#define mm_tiles ((u8 *)pal_t)
static u16 mm_map[36];
static s16 mm_ox = -100, mm_oy = -100;
static u8 mm_dirty = 1, mm_timer;

/* bottom HUD strip: BG2 tiles (map rows 14/15 = lines 156..168), so it costs no sprites */
#define HT_BASE 32
static const u16 hud_pal[4] = { 0, 0x7FFF, (u16)((4 << 10) | (26 << 5) | 31), 0 };
static u16 hud_row[32], hud_last[32];
static u8 strip_last[6], hud_osig[10], hud_oend;
static u8 hud_dirty, top_wr;
static u16 ht(char c) {
    u8 g;
    if (c >= '0' && c <= '9') g = (u8)(c - '0');
    else if (c >= 'A' && c <= 'Z') g = (u8)(10 + c - 'A');
    else if (c == '[') g = 36;
    else if (c == ']') g = 37;
    else if (c == '-') g = 38;
    else if (c == '/') g = 39;
    else if (c == ':') g = 46;
    else if (c == '@') g = 41;          /* ammo */
    else if (c == '~') g = 42;          /* infinity */
    else if (c == '#') g = 43;          /* bar full */
    else if (c == '_') g = 44;          /* bar empty */
    else g = 40;
    return (u16)(0x2800 | (HT_BASE + g));
}

/* The backdrop colour is the room's lifted colour 0 inside the view lines: the 16 lines above the view would show it as two stripes around the black text bar.
   BG2 rows 29 and 31 paint them black: a full black tile row, and a tile row that is black in its upper 4 lines only (the lower 4 are the view's first lines). */
static const u16 blk_row29[32] = {0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848,0x2848};
static const u16 blk_row31[32] = {0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850,0x2850};
static const u8 blk_tile[32] = {255,255,255,255,255,255,255,255,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static void blk_rows(void) { dmaCopyVram((const u8 *)blk_row29, (u16)(VRAM_BG2_MAP + 29 * 32), 64); dmaCopyVram((const u8 *)blk_row31, (u16)(VRAM_BG2_MAP + 31 * 32), 64); }

static void minimap_init(void) {
    u8 p, r;
    for (p = 0; p < 16; p++) {
        for (r = 0; r < 8; r++) {
            u8 q0 = (u8)(r < 4 ? 0 : 2), m1 = 0, m2 = 0;
            if ((p >> q0) & 1) m1 |= 0xF0; else m2 |= 0xF0;
            if ((p >> (q0 + 1)) & 1) m1 |= 0x0F; else m2 |= 0x0F;
            mm_tiles[p * 32 + r * 2] = m1;
            mm_tiles[p * 32 + r * 2 + 1] = m2;
        }
    }
    dmaCopyVram(mm_tiles, VRAM_BG2_CHR, 544);
    dmaCopyCGram((const u8 *)mm_pal, 16, 8);
    { static const u16 dot_red = (u16)((4 << 10) | (4 << 5) | 31), dot_blue = (u16)((31 << 10) | (14 << 5) | 4);      /* the map's dots: enemies red, the boss blue (colours 4 and 8 of the map's palette) */
      dmaCopyCGram((const u8 *)&dot_red, 20, 2); dmaCopyCGram((const u8 *)&dot_blue, 24, 2); }
    dmaCopyCGram((const u8 *)hud_pal, 32, 8);
    {
        static const u16 koze_pal[4] = { 0, (u16)((31 << 10) | (30 << 5) | 22), (u16)((31 << 10) | (9 << 5) | 3), 0 };
        u8 q;
        dmaCopyCGram((const u8 *)koze_pal, 48, 8);
        for (q = 0; q < 32; q++) { koze_row[q] = BLANK_TILE; koze_blank[q] = BLANK_TILE; }
    }
    dmaCopyVram(hud_tiles, (u16)(VRAM_BG2_CHR + HT_BASE * 16), HUD_NTILES * 32);
    dmaCopyVram(blk_tile, (u16)(VRAM_BG2_CHR + 80 * 16), 32);
    blk_rows();
    for (p = 0; p < 32; p++) { hud_row[p] = ht(' '); hud_last[p] = 0xFFFF; }
    strip_last[0] = 255; hud_osig[0] = 255;
    dmaCopyVram((const u8 *)hud_row, (u16)(VRAM_BG2_MAP + 15 * 32), 64);
    hud_dirty = 1;
}

#define mm_dot(k) (&tv_buf[48 + (k) * 4])         /* per dot: tile pattern, quadrant, colour (0 none, 1 red, 2 blue) */
#define mm_sig (*(u16 *)&tv_buf[56])               /* the dots' position signature (only after the HUD tour: the tour owns tv_buf before that) */
static void minimap_update(void) {
    u8 tx, ty, q, k;
    s16 ox = (s16)((px >> 8) - 6), oy = (s16)((py >> 8) - 6);
    u16 sig = 0;
    for (k = 0; k < MAXE && tour_done; k++) if (en[k].st != E_NONE && en[k].st != E_DEAD && !IS_BARREL(&en[k]) && en[k].hid != 7)
        sig = (u16)(sig * 131u + (u16)((u8)(en[k].x >> 8) * 64u + (u8)(en[k].y >> 8)) + (en[k].type ? 5000u : 9000u));
    if (ox == mm_ox && oy == mm_oy && (!tour_done || sig == mm_sig) && ++mm_timer < 20) return;
    mm_timer = 0; mm_ox = ox; mm_oy = oy; if (tour_done) mm_sig = sig;
    for (ty = 0; ty < 6; ty++) for (tx = 0; tx < 6; tx++) {
        u8 pat = 0;
        for (q = 0; q < 4; q++) {
            s16 cx = (s16)(ox + tx * 2 + (q & 1)), cy = (s16)(oy + ty * 2 + (q >> 1));
            if (cx < 0 || cx > 31 || cy < 0 || cy > 63 || map_w[(cy << 5) + cx]) pat |= (u8)(1 << q);
        }
        mm_map[ty * 6 + tx] = (u16)(pat | 0x2400);
    }
    for (k = 0; k < MAXE && tour_done; k++) {                                       /* the enemies and the boss: a coloured cell of the map */
        u8 *d = mm_dot(k);
        s16 cx = (s16)((en[k].x >> 8) - ox), cy = (s16)((en[k].y >> 8) - oy);
        d[2] = 0;
        if (en[k].st == E_NONE || en[k].st == E_DEAD || IS_BARREL(&en[k]) || en[k].hid == 7 || cx < 0 || cx > 11 || cy < 0 || cy > 11) continue;
        tx = (u8)(cx >> 1); ty = (u8)(cy >> 1);
        d[0] = (u8)(mm_map[ty * 6 + tx] & 15); d[1] = (u8)((cx & 1) | ((cy & 1) << 1)); d[2] = (u8)(en[k].type ? 1 : 2);
        mm_map[ty * 6 + tx] = (u16)((17 + k) | 0x2400);
    }
    mm_dirty = 1;
}

static void minimap_upload(void) {
    u8 ty;
    if (!mm_dirty || info_win || info_clr) return;
    for (ty = 0; ty < 6; ty++) dmaCopyVram((const u8 *)&mm_map[ty * 6], (u16)(VRAM_BG2_MAP + ty * 32), 12);
    for (ty = 0; ty < MAXE && tour_done; ty++) {                                    /* the dot tiles: the wall pattern of the tile with one quadrant painted */
        u8 *d = mm_dot(ty), t[32], r;
        if (!d[2]) continue;
        for (r = 0; r < 8; r++) {
            u8 q0 = (u8)(r < 4 ? 0 : 2), m1 = 0, m2 = 0, mk = (u8)((d[1] & 1) ? 0x0F : 0xF0), c = 0;
            if ((d[0] >> q0) & 1) m1 |= 0xF0; else m2 |= 0xF0;
            if ((d[0] >> (q0 + 1)) & 1) m1 |= 0x0F; else m2 |= 0x0F;
            if ((r >> 2) == (d[1] >> 1)) { m1 &= (u8)~mk; m2 &= (u8)~mk; c = mk; }
            t[r * 2] = m1; t[r * 2 + 1] = m2; t[16 + r * 2] = (u8)(d[2] == 1 ? c : 0); t[17 + r * 2] = (u8)(d[2] == 2 ? c : 0);
        }
        dmaCopyVram(t, (u16)(VRAM_BG2_CHR + (17 + ty) * 16), 32);
    }
    mm_dirty = 0;
}

static void draw_minimap(void) {
    s16 mx = (s16)(MM_X + ((px - mm_ox * 256) >> 6)), my = (s16)(MM_Y + ((py - mm_oy * 256) >> 6));
    arrow_sprite(OAM_MAP, mx, my, COSA(pang), (s16)(-SINA(pang)));
}

/* weapon charge bar: eight steps, in the colour of the weapon (palette entry 15) */
__attribute__((unused)) static void draw_charge(void) {
    u8 i, n = 0;
    if (!cls_sel && w_cd[cur_w] > 90 && w_left[cur_w]) n = (u8)(((s32)(w_cd[cur_w] - w_left[cur_w]) * 8) / w_cd[cur_w]);
    for (i = 0; i < 8; i++) {
        if (i < n) { oamSet((u8)(OAM_CHG + i), (u16)(64 + i * 16), (u16)(VIEW_Y + 108), glyph_tile(GLYPH_CHG), 1, 3, 0); oamSetSize((u8)(OAM_CHG + i), 0); }
        else oamHide((u8)(OAM_CHG + i));
    }
}

static u8 eye_glyph(s16 wx, s16 wy) {                       /* 49 straight, 56 pupils left, 48 pupils right (the hero on the camera-right of the axis: right) */
    s16 dx = 1200, dy = 1200, s, lat, rv;
    for (s = 96; s <= 1152; s += 96) {                      /* which pair of walls is closer: left/right or up/down? */
        if (dx == 1200 && (blocked((s16)(wx + s), wy, 40) || blocked((s16)(wx - s), wy, 40))) dx = s;
        if (dy == 1200 && (blocked(wx, (s16)(wy + s), 40) || blocked(wx, (s16)(wy - s), 40))) dy = s;
    }
    if (dx <= dy) { lat = (s16)(px - wx); rv = COSA((pang + 128) & 511); }      /* walls left and right: the axis runs along y */
    else { lat = (s16)(py - wy); rv = SINA((pang + 128) & 511); }
    if (lat > -32 && lat < 32) return 49;
    if (rv < 0) lat = (s16)(-lat);
    return (u8)(lat > 0 ? 56 : 48);
}

/* things the visors show: wall text, gate lamps, the TR button, the way to go */
static void draw_overlays(void) {
    u8 id = OAM_TXT, i;
    s16 sx, sfeet, f, y;
    oamHideRange(OAM_TXT, 4);
    oamHideRange((u8)(OAM_TXT + 8), 8);
    oamHide(OAM_TRB);
    oamHide(OAM_OBJA);
    look_vis = 0;
    if ((bst == 3 || bst == 13) && !((dev_on & 2) && bw_y != 3) && project(BK_X, 262, &sx, &sfeet, &f) && f < 3000 && col_visible(sx, f)) {      /* the monitor on the wall behind the boss: KEY, and the 0 and 1 once the key is decrypted */
        put_text((u8)(OAM_TXT + 12), (s16)(sx - (bw_y == 3 ? 24 : 18)), (s16)(g_horizon - 62 + VIEW_Y), bw_y == 3 ? "0101" : "KEY");
    }
    if (project(TXT_X, TXT_Y, &sx, &sfeet, &f) && f < 2400 && col_visible(sx, f)) {      /* always on the wall; the optovisor turns it into POWER ON */
        y = (s16)(g_horizon - 26 + VIEW_Y);
        if ((dev_on & 2) && gate_pow) {
            id = put_text(OAM_TXT, (s16)(sx - 36), y, "POWE");
            put_glyph((u8)(OAM_TXT + 8), (s16)(sx + 12), y, glyph_of('R'));
            put_text((u8)(OAM_TXT + 9), (s16)(sx - 12), (s16)(y + 14), "ON");
        } else {                                                       /* the wall says LOOK in alien glyphs (the big block holds them) */
            look_vis |= 1;
            if (BIG_OK(23)) { u8 q; for (q = 0; q < 4; q++) id = put_glyph(id, (s16)(sx - 36 + q * 18), y, (u8)(48 + (q & 1) + (q >> 1) * 8)); }
        }
    }
    if (bst < 6 && project(BK_X, BK_Y, &sx, &sfeet, &f) && f < 2400 && col_visible(sx, f)) {          /* the glyph wall behind the boss: two glyphs repeated */
        y = (s16)(g_horizon - 26 + VIEW_Y);
        if ((dev_on & 2) && bst >= 2) { }                                   /* the optovisor reads the glyphs: the text stands on the wall to the right of the boss (below) */
        else {
            look_vis |= 4;
            if (BIG_OK(25)) { u8 q; for (q = 0; q < 4; q++) put_glyph((u8)(OAM_TXT + 8 + q), (s16)(sx - 36 + q * 18), y, (u8)((q & 1) ? 57 : 56)); }
        }
    }
    if ((dev_on & 4) && project(PLATE_X, PLATE_Y, &sx, &sfeet, &f) && f < 1600 && col_visible(sx, f))      /* the TR button, seen through the redvisor */
        put_text((u8)(OAM_TXT + 8), (s16)(sx - 48), (s16)(g_horizon - 26 + VIEW_Y), trd_open ? "GATE OPEN" : gate_pow ? "POWER ON" : "NO POWER");
    if ((dev_on & 2) && bst >= 2 && bst < 6 && project((s16)(BK_X + 520), BK_Y, &sx, &sfeet, &f) && f < 2600 && col_visible(sx, f)) {
        u8 q; y = (s16)(g_horizon - 26 + VIEW_Y);
        q = put_text((u8)(OAM_TXT + 8), (s16)(sx - 18), y, "KEY"); q = put_text(q, (s16)(sx - 24), (s16)(y + 14), "DECR"); put_text((u8)(OAM_TXT + 4), (s16)(sx + 24), (s16)(y + 14), "YPT");
    }
    if (gate_pow && !trd_open) {
        for (i = 0; i < 5; i++) {
            if (!project((s16)((15 + i) * 256 + 128), (s16)(6 * 256 + 40), &sx, &sfeet, &f) || !col_visible(sx, f)) continue;
            y = (s16)(g_horizon + (s16)(((s32)g_cam_h8 * 128 - 70 * 128) / f) - 8 + VIEW_Y);
            oamSet((u8)(OAM_TXT + 11 + i), (u16)(sx - 8), (u16)y, (u16)((8 + 0) * 16 + 8), 2, 2, 0);
            oamSetSize((u8)(OAM_TXT + 11 + i), 0);
        }
    }
    {                                                                   /* the eye: "put on the visor here" (the TR button: optovisor, the last crystal: redvisor) */
        for (i = 0; i < 2; i++) {
            s16 wx = i ? ey_p[ey_a & 7][0] : PLATE_X, wy = i ? ey_p[ey_a & 7][1] : PLATE_Y;
            if (!tour_done || tour) continue;
            if (i ? (!(ey_a & 128) || !(ey_f & 1) || !(bst == 0 || bst == 1 || bst == 4) || !(have_d & 4) || (dev_on & 4)) : (trd_open || !(have_d & 4) || (dev_on & 4))) continue;
            if (project(wx, wy, &sx, &sfeet, &f) && col_visible(sx, f)) {
                y = (s16)(g_horizon + (s16)(((s32)g_cam_h8 * 128 - 44 * 128) / f) - 8 + VIEW_Y);
                look_vis |= 2;
                if (BIG_OK(24)) {                         /* two eyes that look at the player: the pupils move toward the middle of the screen */
                    u8 g = eye_glyph(wx, wy);   /* the eyes look along the walls that hold them: the pupils follow which side of that axis the hero is on, seen from the camera */
                    put_glyph(OAM_TRB, (s16)(sx - 17), y, g); put_glyph((u8)(OAM_TXT + 10), (s16)(sx + 1), y, g);     /* two eyes for both the TR button and the redvisor mark */
                }
            }
        }
    }
    if (bst == 2 && tp_on) { u8 q = put_glyph(93, 100, (s16)(VIEW_Y + 20), (u16)((bst_t + 59) / 60)); put_text(q, 118, (s16)(VIEW_Y + 20), "SEC"); }          /* the teleport's life: 3, 2, 1 */
    else if (bst == 10 && !CSP) { u8 hits = 0, k; for (k = 0; k < MAXE; k++) if (en[k].type == 0 && en[k].st != E_NONE) hits = en[k].bt; for (i = 0; i < 5; i++) put_glyph((u8)(93 + i), (s16)(103 + i * 10), (s16)(VIEW_Y + 20), (u8)(i < 5 - hits ? GLYPH_SQ2 : GLYPH_SQ0)); }     /* the boss life bar: 5 barrels */
    else if (bst == 3 && (bw_y || bw_x) && dist2d(px - BK_X, py - 400) <= FIELD_R + 700) {                       /* KEY DECRYPT: 12 cells, 1 per second */
        u8 cells = (u8)(bw_x / 60);
        for (i = 0; i < 6; i++) { s16 n = (s16)cells - 2 * i; put_glyph((u8)(93 + i), (s16)(103 + i * 10), (s16)(VIEW_Y + 20), (u8)(n >= 2 ? GLYPH_SQ2 : n == 1 ? GLYPH_SQ1 : GLYPH_SQ0)); }
    }
    if (dev_on & 2) {                                                   /* the arrow: nearest crystal, then the exit */
        s16 tx = (EXIT_X0 + EXIT_X1) / 2, ty = (EXIT_Y0 + EXIT_Y1) / 2, bd = 32000, dx, dy;
        if (crystals < 9) {
            for (i = 0; i < NUM_ITEMS; i++) {
                if (item_kind[i] != 0 || item_got[i]) continue;
                dx = item_pos[i][0] - px; dy = item_pos[i][1] - py;
                if ((s16)dist2d(dx, dy) < bd) { bd = (s16)dist2d(dx, dy); tx = item_pos[i][0]; ty = item_pos[i][1]; }
            }
        }
        if (bcry_on) { tx = bcry_x; ty = bcry_y; }
        dx = tx - px; dy = ty - py;
        arrow_sprite(OAM_OBJA, 128, (s16)(VIEW_Y + 14),
                     (s16)((((s32)dy * COSA(pang)) - ((s32)dx * SINA(pang))) >> 8), (s16)((((s32)dx * COSA(pang)) + ((s32)dy * SINA(pang))) >> 8));
    }
}

static u8 put_digit(u8 id, s16 x, s16 y, u16 v) { return put_glyph(id, x, y, (u8)(v % 10)); }

static void hud_put(u8 i, const char *t) {              /* cell i of the strip sits at x = 4 + 8 i; map column (7 + i) & 31 */
    while (*t && i < 31) { hud_row[(u8)((7 + i) & 31)] = ht(*t); t++; i++; }
}

static void draw_strip(void) {
    u8 k, n;
    const char *nm;
    char b[3];
    {                                                   /* nothing changed: the strip stays as it is */
        u8 sg[6];
        sg[0] = (u8)(cls_sel | (info_win ? 2 : 0) | ((tour >= 5 && tour <= 7) ? (u8)(tour << 2) | ((last_fc & 16) ? 128 : 0) : 0));
        sg[1] = cls_sel ? cur_d : cur_w;
        sg[2] = cls_sel ? (u8)((dev_on & 7) | (have_d << 3)) : (w_amax[cur_w] == 255 ? 0 : w_ammo[cur_w]);
        sg[3] = cls_sel ? (u8)((u32)vis_t[cur_d] * 10 / 10800) : (w_rel[cur_w] ? (u8)(1 + (u8)((u32)w_rel[cur_w] * 10 / w_rel_t[cur_w])) : 0);
        sg[4] = (!cls_sel && w_cd[cur_w] > 90 && w_left[cur_w]) ? (u8)(1 + (u8)((u32)(w_cd[cur_w] - w_left[cur_w]) * 10 / w_cd[cur_w])) : 0;
        sg[5] = (u8)(have_d | (vis_block() ? 128 : 0));
        if (!info_win && sg[0] == strip_last[0] && sg[1] == strip_last[1] && sg[2] == strip_last[2] && sg[3] == strip_last[3] && sg[4] == strip_last[4] && sg[5] == strip_last[5]) return;
        for (k = 0; k < 6; k++) strip_last[k] = sg[k];
    }
    for (k = 0; k < 32; k++) hud_row[k] = ht(' ');
    if (info_win) { hud_put(8, "PRESS R FOR EXIT MENU"); hud_dirty = 1; return; }
    if (cls_sel) nm = vis_block() ? "VISORS BLOCKED" : have_d ? d_name[cur_d] : "NO VISORS"; else nm = w_name[cur_w];
    hud_put(0, "[");
    hud_put(1, nm);
    for (n = 0; nm[n]; n++) {}
    hud_put((u8)(1 + n), "]");
    if (!cls_sel) {
        if (w_rel[cur_w]) {
            u8 full = (u8)(10 - (u8)(((u32)w_rel[cur_w] * 10) / w_rel_t[cur_w]));
            hud_put(14, "RELOAD");
            for (k = 0; k < 10; k++) hud_row[(u8)((7 + 21 + k) & 31)] = ht(k < full ? '#' : '_');
        } else {
            hud_put(14, "@");
            if (w_amax[cur_w] == 255) hud_put(16, "~");
            else {
                if (w_amax[cur_w] == 100) {
                    char c[9]; u8 a = w_ammo[cur_w];
                    c[0] = (char)('0' + a / 100); c[1] = (char)('0' + (a / 10) % 10); c[2] = (char)('0' + a % 10);
                    c[3] = '/'; c[4] = '1'; c[5] = '0'; c[6] = '0'; c[7] = 0;
                    hud_put(16, c);
                } else {
                b[0] = (char)('0' + w_ammo[cur_w]); b[1] = '/'; b[2] = 0;
                hud_put(16, b);
                b[0] = (char)('0' + w_amax[cur_w]); b[1] = 0;
                hud_put(18, b);
                }
            }
            if (w_cd[cur_w] > 90 && w_left[cur_w]) {                     /* charging between shots */
                u8 full = (u8)(((u32)(w_cd[cur_w] - w_left[cur_w]) * 10) / w_cd[cur_w]);
                for (k = 0; k < 10; k++) hud_row[(u8)((7 + 21 + k) & 31)] = ht(k < full ? '#' : '_');
            }
        }
    } else if (have_d && !vis_block()) {
        hud_put(14, (dev_on & (1 << cur_d)) ? "ON" : "OFF");
        {
            u8 full = (u8)(((u32)vis_t[cur_d] * 10) / 10800);
            hud_put(21 - 0, "");
            for (k = 0; k < 10; k++) hud_row[(u8)((7 + 21 + k) & 31)] = ht(k < full ? '#' : '_');
        }
    }
    if (tour >= 5 && tour <= 7) {                                       /* the tour: the explained part of the strip blinks */
        u8 a = (u8)(tour == 5 ? 0 : 14), b = (u8)(tour == 5 ? 13 : tour == 6 ? 20 : 30);
        if (tour == 7) { hud_put(14, "RELOAD"); for (k = 0; k < 10; k++) hud_row[(u8)((7 + 21 + k) & 31)] = ht(k < 6 ? '#' : '_'); }
        if (last_fc & 16) for (k = a; k <= b; k++) hud_row[(u8)((7 + k) & 31)] = ht(' ');
    }
    for (k = 0; k < 32; k++) if (hud_row[k] != hud_last[k]) { hud_dirty = 1; break; }
}

static void tv_add(u8 id, s16 x, u8 g, u8 e) {
    u8 k;
    for (k = 0; k < tv_n; k++) if (tv_g[k] == g && tv_e[k] == e) break;
    if (k == tv_n) { if (tv_n >= 4) k = (u8)(tv_n - 1); else { tv_g[tv_n] = g; tv_e[tv_n] = e; tv_n++; } }
    tv_id[tv_cnt] = id; tv_x[tv_cnt] = (u8)x; tv_k[tv_cnt] = k; tv_cnt++;
}

static void tour_build(void) {                      /* which HUD sprites carry the frame in this step (ids and positions are those of draw_hud) */
    u8 i, full = (u8)((php + 9) / 10);
    tv_n = 0; tv_cnt = 0;
    hud_osig[0] = 255;                              /* the HUD sprites are written again with the right cells */
    tv_ready = 9;                                  /* the frame shows once the new cells have been written */
    if (0) for (i = 0; i < 10; i++) tv_add((u8)(OAM_HUD + i), (s16)(2 + i * 9), i < full ? GLYPH_BARFULL : GLYPH_BAREMPTY, (u8)((i == 0 ? 1 : 0) | (i == 9 ? 2 : 0)));
    if (0) { tv_add(OAM_HUD + 10, 106, 12, 1); tv_add(OAM_HUD + 11, 118, (u8)(crystals % 10), 0); tv_add(OAM_HUD + 12, 130, 36, 0); tv_add(OAM_HUD + 13, 142, 9, 2); }
}

static void tv_mask(u8 base, u8 y, u8 m) {
    tv_buf[base + 2 * y] &= (u8)~m; tv_buf[base + 2 * y + 1] |= m;
    tv_buf[base + 16 + 2 * y] &= (u8)~m; tv_buf[base + 17 + 2 * y] &= (u8)~m;
}

static u16 tv_dst;
static u8 tv_have;
static void tv_prepare(void) {                    /* outside the blank (it is only ~6000 cycles): one 64-byte chunk of a cell - glyph + frame lines + the edge - ready for the blank to copy */
    u8 n, k, q, y, e, i;
    const u8 *src;
    tv_have = 0;
    if (!tour || !tour_rep || info_clr) return;
    n = (u8)(tour_rep & 7); k = (u8)(n >> 1); q = (u8)(n & 1);
    if (k >= tv_n) return;
    e = tv_e[k];
    src = spr_obj1 + (u16)(glyph_tile(tv_g[k]) - 256) * 32 + q * 512;
    for (i = 0; i < 64; i++) tv_buf[i] = src[i];
    for (y = 0; y < 8; y++) {
        u8 line = q ? (u8)(y >= 6) : (u8)(y < 2);
        tv_mask(0, y, line ? 0xFF : (u8)((e & 1) ? 0xC0 : 0));
        tv_mask(32, y, line ? 0xFF : (u8)((e & 2) ? 0x03 : 0));
    }
    tv_dst = (u16)(VRAM_OBJ1 + (glyph_tile(tv_cell[k]) - 256) * 16 + q * 256);
    tv_have = 1;
}

/* enemy attack visuals: the beam / rail line (8 sprites of the player's beam art) and the crack on the camera (4 flipped copies of one 32x32 quarter) */
static void draw_wires(u16 fc) {                             /* matrix rain, four thick wires, round switches, a white current */
    u8 id = 5, r, k, sl, q;
    u16 len, p, df;
    s16 x, y;
    oamHideRange(0, 112);
    if (!wr_pf) {                                            /* the sprite palettes of the minigame (put back when it ends) */
        set_obj_pal(1, wire_pal_sw); set_obj_pal(3, wire_pal_mx);
        for (k = 0; k < 4; k++) set_obj_pal((u8)(4 + k), wire_pal_w[k]);
        wr_pf = 1; wr_t0 = fc;
    }
    df = (u16)(fc - wr_lf); if (df > 6) df = 1;
    wr_lf = fc;
    if (wr_pf < 3 && (u16)(fc - wr_t0) > (u16)(wr_pf == 1 ? 24 : 90)) { wp_shown = 255; wp_busy = 0; wr_pf++; }   /* a DMA lost in a busy blank would leave holes in the switches: send the tiles again twice */
    len = wr_len(); p = (u16)(wr_pl | (wr_ph << 8));
    if (!wr_t) {                                             /* the current runs; a dead end sends it back along the circuit */
        if (!wr_d) { p = (u16)(p + 2 * df); if (p >= len) { p = len; if (!wr_w) wr_d = 1; } }
        else { if (p <= 2 * df) { p = 0; wr_d = 0; } else p = (u16)(p - 2 * df); }
        wr_pl = (u8)p; wr_ph = (u8)(p >> 8);
    }
    /* A line of the screen takes at most 34 sprite tiles and the lowest sprites are dropped first: the rain (small, 2 tiles) goes first, the switches last. */
    for (k = 0; k < 12; k++) {                               /* falling 0 and 1, green, like in the film */
        oamSet(id, (u16)(6 + k * 20), (u16)(VIEW_Y + (s16)(((u16)(k * 53) + fc * (u16)(1 + (k % 3))) % 112)), glyph_tile((u8)(((k * 5) + (fc >> 3)) & 1)), 3, 3, 0); oamSetSize(id, 0); id++;
    }
    for (q = 0; q < 3; q++) {                                /* the white current: a head and a tail */
        s16 pp = (s16)((s16)p - 8 * q);
        if (pp < 0 || (wr_t && ((wr_t >> 1) & 1))) { id++; continue; }
        wr_xy((u16)pp, &x, &y);
        oamSet(id, (u16)(x - 8), (u16)(y - 8), 196, 1, 3, 0); oamSetSize(id, 0); id++;
    }
    for (r = 0; r < 4; r++)                                  /* the switches; the chosen one has a yellow ring and a green face (matrix palette) */
        for (k = 0; k < 2; k++) {
            oamSet(id, (u16)(48 + 32 * wr_sl[r][k]), (u16)(VIEW_Y + 32 * r), (u16)(((wr_s >> (r * 2 + k)) & 1) ? 128 : 132), (u8)((wr_c == r * 2 + k) ? 3 : 1), 3, 0); oamSetSize(id, 1); id++;
        }
    for (r = 0; r < 4; r++)                                  /* the thick wires between the switches */
        for (sl = 0; sl < 5; sl++) {
            if (sl == wr_sl[r][0] || sl == wr_sl[r][1]) continue;
            oamSet(id, (u16)(48 + 32 * sl), (u16)(VIEW_Y + 32 * r), 192, (u8)(4 + r), 3, 0); oamSetSize(id, 1); id++;
        }
    { const char *t = "VOLTAGE"; for (k = 0; k < 7; k++) { id = put_glyph(id, 22, (s16)(VIEW_Y + 2 + 9 * k), glyph_of(t[k])); } }
    { const char *t = "GROUND"; for (k = 0; k < 6; k++) { id = put_glyph(id, 226, (s16)(VIEW_Y + 72 + 9 * k), glyph_of(t[k])); } }
}

static void draw_attacks(void) {
    u8 i;
    if (!tour_done || tour) return;
    if (bm_t && !pl_shot_t && !dead) {                      /* eight short white pieces along the line from the enemy to its target */
        s16 sx0 = bm_x, sy0 = (s16)(bm_y + VIEW_Y), ddx = (s16)((s16)bm_tx - sx0), ddy = (s16)((s16)(bm_ty + VIEW_Y) - sy0);
        u8 cellg = (u8)((iabs(ddy) * 100 / (iabs(ddx) + 1)) > 26 ? 63 : 62);
        u8 fl = (u8)(((ddx > 0) == (ddy > 0)) ? 0x80 : 0);
        for (i = 0; i < 8; i++) {
            s16 cx = (s16)(sx0 + (s16)(((s32)ddx * (i * 2 + 1)) / 16)), cy = (s16)(sy0 + (s16)(((s32)ddy * (i * 2 + 1)) / 16));
            oamSet((u8)(OAM_CHG + i), (u16)(cx - 8), (u16)(cy - 8), glyph_tile(cellg), 2, 3, fl);
            oamSetSize((u8)(OAM_CHG + i), 0);
        }
    }
    {                                                         /* the boss shield: a ring of yellow sparks round its feet */
        s16 sx, sf, f; u8 k;
        u8 fxo = (u8)((bst == 3 && !dead) || (bst == 10 && !CSP) || xp_t);       /* the effect sprites share the item slots: take them only while an effect is on, otherwise the pickups stay visible */
        if (bst == 0 || bst == 4) for (k = 0; k < MAXE; k++) if (en[k].type == 0 && en[k].st != E_NONE && !en[k].ph && en[k].pt && en[k].st != E_DEAD) fxo = 1;
        if (fxo) for (k = 0; k < 8; k++) oamHide((u8)(OAM_ITEM + k));
        if (bst == 0 || bst == 4) for (k = 0; k < MAXE; k++) if (en[k].type == 0 && en[k].st != E_NONE && !en[k].ph && en[k].pt && project(en[k].x, en[k].y, &sx, &sf, &f) && f > 60) {
            s16 rx = (s16)(34000 / f), ry, a;
            if (rx < 14) rx = 14; if (rx > 90) rx = 90;
            ry = (s16)(rx / 3);
            for (a = 0; a < 8; a++) {
                u16 an = (u16)(a * 64 + (last_fc << 2));
                oamSet((u8)(OAM_ITEM + a), (u16)(sx + ((rx * COSA(an)) >> 8) - 8), (u16)(sf - 8 - ry + ((ry * SINA(an)) >> 8) + VIEW_Y), (u16)((8 + 2) * 16 + 8 + 4), 2, 2, 0); oamSetSize((u8)(OAM_ITEM + a), 0);
            }
        }
        if (bst == 3 && !dead) {                               /* the forbidden zone round the boss at the glyph wall: a blue ring */
            for (k = 0; k < 8; k++) {
                u16 an = (u16)(k * 64 + (last_fc >> 2));
                if (project((s16)(BK_X + (((s32)COSA(an) * FIELD_R) >> 8)), (s16)(400 + (((s32)SINA(an) * FIELD_R) >> 8)), &sx, &sf, &f) && col_visible(sx, f)) {
                    oamSet((u8)(OAM_ITEM + k), (u16)(sx - 8), (u16)(sf - 9 + VIEW_Y), (u16)((8 + 4) * 16 + 10 + (k & 1) * 2), 2, 1, 0); oamSetSize((u8)(OAM_ITEM + k), 0);
                }
            }
        }
        if (xp_t && BIG_OK(27) && project(xp_x, xp_y, &sx, &sf, &f) && f > 0) {
            s16 y = (s16)(g_horizon + (s16)(((s32)g_cam_h8 * 128 - 40 * 128) / f));
            oamSet((u8)(OAM_ITEM + 7), (u16)(sx - 16), (u16)(y - 16 + VIEW_Y), (u16)(256 + 12 * 16), 2, 3, 0); oamSetSize((u8)(OAM_ITEM + 7), 1);
        }
    }
    if (cr_t && !dead && BIG_OK(26)) {
        u16 t = (u16)(256 + 12 * 16);
        oamSet(OAM_OVAL, (u16)(cr_x - 32), (u16)(cr_y - 32 + VIEW_Y), t, 2, 3, 0);
        oamSet((u8)(OAM_OVAL + 1), (u16)cr_x, (u16)(cr_y - 32 + VIEW_Y), t, 2, 3, 0x40);
        oamSet((u8)(OAM_OVAL + 2), (u16)(cr_x - 32), (u16)(cr_y + VIEW_Y), t, 2, 3, 0x80);
        oamSet((u8)(OAM_OVAL + 3), (u16)cr_x, (u16)(cr_y + VIEW_Y), t, 2, 3, 0xC0);
        for (i = 0; i < 4; i++) oamSetSize((u8)(OAM_OVAL + i), 1);
    }
}

static u8 combat_now(void) {                       /* a fight is on: any live enemy while the level / the first wave / the last fight / the fighters are up */
    u8 i;
    if (bst != 0 && bst != 4 && bst != 8) return 0;
    for (i = 0; i < MAXE; i++) if (en[i].st != E_NONE && en[i].st != E_DEAD) return 1;
    return 0;
}

static u8 bike_view(void) { return (u8)(bst == 10 || (bst == 9 && bw_y >= 4)); }     /* the chase road is on screen (also in the cut-scene before the chase) */
static u8 vis_block(void) { if ((ey_a & 128) && cur_d == 2) return 0; return (u8)(bst == 10 || (bst == 3 && bw_y == 2) || combat_now()); }       /* (the redvisor stays allowed while the eye of the boss room is up) */       /* the visors are locked: fights and the bike */

static u8 enm_calc(u8 *kk, u8 *tt) {              /* the enemy counter: killed / all of the fight (arena rooms, the helpers of the boss) */
    u8 r, i, boss = 0, al = 0;
    for (i = 0; i < MAXE; i++) {
        if (en[i].type == 0 && en[i].st != E_NONE && en[i].st != E_DEAD) boss = 1;
        if (en[i].type != 0 && en[i].st != E_NONE && en[i].st != E_DEAD) al++;
    }
    (void)al;
    if (bst == 4 && tv_buf[61] == 0x5A && tv_buf[58]) { *tt = 5; *kk = (u8)(5 - tv_buf[58]); return 1; }           /* the last fight: the barrels shot at the boss (the shield's charges) */
    if ((bst == 0 && boss) || (bst == 7 && bw_y < 5)) { u8 t = 3; *tt = t; *kk = (u8)(wv_k > t ? t : wv_k); return 1; }       /* wave 1: the 3 class-5 helpers of the whole fight (the final fight: 4) */       /* the whole fight: 4 helpers for each of the 5 lives of the boss, summed over all of them */
    if (bst == 3 && bw_y == 2) { *tt = 1; *kk = 0; return 1; }
    if (bst == 8) { *tt = 3; *kk = (u8)(wv_k > 3 ? 3 : wv_k); return 1; }
    if (bst == 0) for (r = 0; r < NUM_ROOMS; r++) if (rst[r] == 2) {
        u8 t = (u8)(r == LOOK_ROOM ? 18 : room_tot[r]);
        *tt = t; *kk = (u8)(t > room_left[r] ? t - room_left[r] : 0); return 1;
    }
    return 0;
}

static void put_bg(u16 *row, u8 sc, const char *t) { while (*t) { row[(u8)((sc + 7) & 31)] = ht(*t); sc++; t++; } }

static void hud_extra(void) {                      /* BG2 text: row 30 = REP / MDK under the top bar, row 15 = ENM (bottom right); written every frame, 128 bytes */
    u16 *r = info_buf;
    u8 k, ek = 0, et = 0, on;
    if (info_win || info_clr || info_wn) return;
    for (k = 0; k < 64; k++) r[k] = ht(' ');
    if (!banner_t && !(tour == 3 && (last_fc & 16))) { put_bg(r, 20, "REP:"); r[(u8)((24 + 7) & 31)] = ht((char)('0' + (lives > 9 ? 9 : lives))); }
    if (!banner_t && !(tour == 4 && (last_fc & 16))) { put_bg(r, 26, "MDK:"); r[(u8)((30 + 7) & 31)] = ht((char)('0' + (medkits > 9 ? 9 : medkits))); }
    on = (u8)(tour == 8);
    if (bst == 5 && !CSP && !tour && !won) { char c[8]; u16 s = bst_t >= 1200 ? 0 : (u16)((1200 - bst_t + 59) / 60); c[0] = 'T'; c[1] = 'I'; c[2] = 'M'; c[3] = 'E'; c[4] = ':'; c[5] = (char)('0' + s / 10); c[6] = (char)('0' + s % 10); c[7] = 0; put_bg(r + 32, 22, c); }
    if (on) { ek = 4; et = 10; } else if (!CSP && !won) on = enm_calc(&ek, &et);
#ifdef DBG_ENM
    on = 1; ek = 4; et = 10;
#endif
    if (on && !(tour == 8 && (last_fc & 16))) {
        char c[10];
        c[0] = 'E'; c[1] = 'N'; c[2] = 'M'; c[3] = ':'; c[4] = (char)('0' + ek / 10); c[5] = (char)('0' + ek % 10); c[6] = '/'; c[7] = (char)('0' + et / 10); c[8] = (char)('0' + et % 10); c[9] = 0;
        put_bg(r + 32, 22, c);
    }
    top_wr = 1;
}

static void draw_hud(u16 fc) {
    u8 id = OAM_HUD, i;
    s16 y = VIEW_Y - 16;
    (void)fc;
    if (won) {
        hud_osig[0] = 255;
        oamHideRange(0, 53);
        oamHideRange(OAM_AIM, 51);
        id = put_text(id, 70, 100, "NICE JOB");
        id = put_text(id, 56, 140, "PRESS START");
        while (id < OAM_AIM) { oamHide(id); id++; }
        return;
    }
    if (CSP != tv_buf[41] || bst != tv_buf[45]) { tv_buf[41] = CSP; tv_buf[45] = bst; hud_osig[0] = 255; }       /* the HUD sprites are written again after a card or a stage change */
    oamSet(4, 120, (u16)(VIEW_Y + 64 - 8), glyph_tile(GLYPH_CROSS), 2, 3, 0);
    oamSetSize(4, 0);
    {
        u8 sg[10], same = 1, q;
        sg[0] = (u8)(banner_t ? 1 + ((u16)(u32)banner_txt & 127) : 0); sg[1] = (u8)((php + 9) / 10); sg[2] = (u8)(medkits | (super_t ? 16 : 0));
        sg[3] = crystals; sg[4] = lives; sg[5] = keys; sg[6] = have_d; sg[7] = (u8)(dev_on & 7); sg[8] = (u8)(info_win | (info_clr ? 4 : 0) | ((tour == 1 || tour == 2) ? (tour == 1 ? 8 : 16) | ((last_fc & 16) ? 32 : 0) : 0)); sg[9] = (u8)(banner_t ? (u8)((u16)(u32)banner_txt >> 7) : 0);
        for (q = 0; q < 10; q++) if (sg[q] != hud_osig[q]) { same = 0; break; }
        if (same) id = hud_oend;
        else {
            for (q = 0; q < 10; q++) hud_osig[q] = sg[q];
    if (banner_t) {
            u8 n = 0;
            const char *p = banner_txt;
            while (*p++) n++;
            id = put_text(id, (s16)(128 - n * 6), y, banner_txt);
        } else {
            u8 full = (u8)((php + 9) / 10);
            if (tour == 1 && (last_fc & 16)) { for (i = 0; i < 10; i++) oamHide((u8)(id + i)); id += 10; }          /* the tour blinks the part it explains */
            else for (i = 0; i < 10; i++) id = put_glyph(id, (s16)(2 + i * 9), y, i < full ? GLYPH_BARFULL : GLYPH_BAREMPTY);
            if (tour == 2 && (last_fc & 16)) { for (i = 0; i < 4; i++) oamHide((u8)(id + i)); id += 4; }
            else {
            id = put_text(id, 106, y, "C");
            id = put_digit(id, 118, y, crystals);
            id = put_text(id, 130, y, "/9");
            }
    #ifdef DBG_DT
            id = put_digit(id, 214, y, dbg_max / 10); id = put_digit(id, 226, y, dbg_max % 10);
    #endif
        }
        {                                                   /* second row over the picture: cards and devices */
            s16 x = 4;
            if (keys & K_BLUE)   { id = put_text(id, x, VIEW_Y + 2, "B"); x += 12; }
            if (keys & K_YELLOW) { id = put_text(id, x, VIEW_Y + 2, "Y"); x += 12; }
            if (keys & K_RED)    { id = put_text(id, x, VIEW_Y + 2, "R"); x += 12; }
            x += 6;
            if (have_d & 1) { id = put_text(id, x, VIEW_Y + 2, (dev_on & 1) ? "N" : "-"); x += 12; }
            if (have_d & 2) { id = put_text(id, x, VIEW_Y + 2, (dev_on & 2) ? "O" : "-"); x += 12; }
            if (have_d & 4) { id = put_text(id, x, VIEW_Y + 2, (dev_on & 4) ? "R" : "-"); x += 12; }
            if (super_t) id = put_text(id, (s16)(x + 6), VIEW_Y + 2, "NO RAD");
        }
        while (id < OAM_AIM) { oamHide(id); id++; }
        hud_oend = id;
        }
    }
    if (dead) { oamHideRange(OAM_AIM, 51); }
    else { draw_aim(); draw_minimap(); draw_overlays(); }
    hud_extra();
    draw_strip();
}

static void video_setup(void) {                       /* mode 3: BG1 = the 160 x 104 bitmap in 8 bpp, BG2 = the HUD */
    u8 r, c;
    setMode(BG_MODE3, 0);
    bgSetGfxPtr(0, VRAM_BG1_CHR);
    bgSetGfxPtr(1, VRAM_BG2_CHR);
    bgSetMapPtr(0, VRAM_BG1_MAP, BG_MAP_32x32);
    bgSetMapPtr(1, VRAM_BG2_MAP, BG_MAP_32x32);
    for (r = 0; r < 32; r++) {
        for (c = 0; c < 32; c++) pal_t[c] = (u16)((r < 13 && c < 28) ? c * 13 + r : BLANK8);      /* the GSU's bitmap tiles are column-major */
        dmaCopyVram((const u8 *)pal_t, (u16)(VRAM_BG1_MAP + r * 32), 64);
    }
    bgSetScroll(0, (u16)(256 - VIEW_XS), (u16)(256 - VIEW_TOP));
    dmaFillVRAM(BLANK_TILE, VRAM_BG2_MAP, 2048);
}

/* ---------------------------------------------------------------- title and stage screens */
static void audio_frame(void) {
    snesmodProcess();
    sfx_tick();
    WaitForVBlank();
}

static void pal_scaled_n(const u16 *p, u8 lv, u8 first, u16 n) {
    u16 i;
    for (i = 0; i < n; i++) {
        u16 c = p[i];
        u16 r = (u16)(((c & 31) * lv) / 15), g = (u16)((((c >> 5) & 31) * lv) / 15), b = (u16)((((c >> 10) & 31) * lv) / 15);
        pal_t[i] = (u16)((b << 10) | (g << 5) | r);
    }
    dmaCopyCGram((const u8 *)pal_t, first, (u16)(n * 2));
}

/* the view palette through a colour effect (shifts and one multiply only: the write must fit in the blank) */
static void view_fx(const u16 *p, u8 kind, u8 k) {
    u8 i;
    for (i = 0; i < VPN; i++) {
        u16 c = p[i], r = (u16)(c & 31), g = (u16)((c >> 5) & 31), b = (u16)((c >> 10) & 31);
        if (kind == 1 || kind == 2) {
            u16 l = (u16)(((r * 3 + g * 6 + b) * 13) >> 7);
            if (kind == 1) { r = l; g = l; b = l; } else { r = (u16)(l >> 3); g = (u16)((l * 3) >> 3); b = (u16)(l >> 3); }
        } else if (kind == 3) { r = (u16)(((r << 2) + r) >> 3); g = (u16)(((g << 2) + g) >> 3); b = (u16)(((b << 2) + b) >> 3); }
        else { r = (u16)(r + (((31 - r) * k) >> 4)); g = (u16)(g + (((31 - g) * k) >> 4)); b = (u16)(b + (((31 - b) * k) >> 4)); }
        pal_t[i] = (u16)((b << 10) | (g << 5) | r);
    }
    cg_view(pal_t);
}

static void blink_draw(void) {                                       /* eyelids: the whole picture darkens, then opens again */
    u8 bl = (u8)(blink_t < 24 ? 15 - (blink_t * 15) / 24 : (blink_t < 25 ? 0 : (((blink_t - 25) * 15) / 25)));
    if (bl > 15) bl = 15;
    setBrightness(bl);
    blink_was = 1;
}
static void blink_end(void) { blink_was = 0; setBrightness(15); }

static void pal_dim_bike(void) { view_fx(bg_pal[0], 3, 0); }
static void pal_blend_white(const u16 *p, u8 k) { view_fx(p, 4, k); pal_cur = 254; }     /* the view palette pulled k/16 toward white */

static u16 scr_row[32];
/* the title: a true-colour picture (BG mode 3, 256 colours, 1:1), faded through the palette, 2 s hold */
static void picture_show(const u8 *pa, const u8 *pb, const u16 *pal) {
    u16 r, c, w;
    u8 lv;
    setScreenOff();
    setMode(BG_MODE3, 0);
    dmaClearVRAM();
    setMainScreen(LAYER_BG1);
    bgSetGfxPtr(0, 0x0000);
    bgSetMapPtr(0, 0x6000, BG_MAP_32x32);
    bgSetScroll(0, 0, 0);
    for (c = 0; c < 18432; c += 2048) dmaCopyVram(pa + c, (u16)(c >> 1), 2048);
    for (c = 0; c < 18432; c += 2048) dmaCopyVram(pb + c, (u16)(9216 + (c >> 1)), 2048);
    for (r = 0; r < 32; r++) {
        for (c = 0; c < 32; c++) scr_row[c] = (r >= 5 && r < 23) ? (u16)((r - 5) * 32 + c) : 1023;
        dmaCopyVram((const u8 *)scr_row, (u16)(0x6000 + r * 32), 64);
    }
    pal_scaled_n(pal, 0, 0, 256);
    setScreenOn();
    for (lv = 1; lv <= 15; lv++) {
        for (w = 0; w < 3; w++) audio_frame();
        pal_scaled_n(pal, lv, 0, 256);
    }
    for (w = 0; w < 120; w++) audio_frame();
    for (lv = 14; lv > 0; lv--) {
        for (w = 0; w < 3; w++) audio_frame();
        pal_scaled_n(pal, lv, 0, 256);
    }
    for (w = 0; w < 3; w++) audio_frame();
    pal_scaled_n(pal, 0, 0, 256);
    setScreenOff();
    setMode(BG_MODE1, 0);
    dmaClearVRAM();
}

static void title_show(void) { picture_show(scr_0a, scr_0b, scr_pal); }
#if HAS_BRIEF
static void briefing_show(void) { picture_show(scr_1a, scr_1b, brief_pal); }
#endif

/* STAGE n: white sprite-font text on black, fade in / 2 s / fade out */
static void stage_show(u8 n) {
    u8 id, lv;
    u16 w;
    setScreenOff();
    setMode(BG_MODE1, 0);
    setMainScreen(LAYER_OBJ);
    dmaCopyVram(spr_obj1, VRAM_OBJ1, 8192);  big_shown = 255; big_want = 255;
    for (w = 0; w < 128; w++) obj_cur[w] = obj_pal[w];
    dmaCopyCGram((const u8 *)obj_cur, 128, 256);
    pal_t[0] = 0;
    dmaCopyCGram((const u8 *)pal_t, 0, 2);
    oamInit(OBJ_SIZE16_L32, OBJ_NAME_BASE(VRAM_OBJ0));
    oamClear();
    oamHideRange(0, 128);
    id = put_text(0, 76, 92, "STAGE");
    id = put_digit(id, 148, 92, n);
    (void)id;
    setBrightness(0);
    setScreenOn();
    for (lv = 1; lv <= 15; lv++) { for (w = 0; w < 3; w++) audio_frame(); setBrightness(lv); }
    for (w = 0; w < 120; w++) audio_frame();
    for (lv = 14; lv > 0; lv--) { for (w = 0; w < 3; w++) audio_frame(); setBrightness(lv); }
    for (w = 0; w < 3; w++) audio_frame();
    setScreenOff();
    oamHideRange(0, 128);
    audio_frame();
    setBrightness(15);
}

/* ---------------------------------------------------------------- main */
/* colour-math strips: white at death, the cyan crack-wash, the kozematy white-out */
static void screen_strips(void) {
        if ((dead && !won && dead_t >= 50) || (ar_t && !dead && tour_done && !tour)) {                  /* a white strip climbs from the bottom to the top at death; a cyan one sweeps the crack away from top to bottom */
            u8 *p = dth_tab;
            s16 y = dead ? (s16)(236 - (s16)(((u32)(dead_t - 50) * 250) / 80)) : (s16)(236 - (s16)(((u16)(40 - ar_t) * 250) / 40)), top = (s16)(y - 16), n;
            static const u8 lvw[4] = { 0xE6, 0xEE, 0xFF, 0xEE }, lvc[4] = { 0xC6, 0xCE, 0xDF, 0xCE };
            const u8 *lv = lvw; (void)lvc;
            u8 q;
            #define DTH_ADD(cnt, v) { n = (cnt); while (n > 0) { u8 c = (u8)(n > 100 ? 100 : n); *p++ = c; *p++ = (v); n -= c; } }
            if (top < 0) top = 0;
            if (top > 224) top = 224;
            DTH_ADD(top, 0xE0);
            for (q = 0; q < 4; q++) { s16 e2 = (s16)(top + 4 * q + 4); if (e2 > 224) e2 = 224; if (e2 > top + 4 * q) { DTH_ADD((s16)(e2 - (top + 4 * q)), lv[q]); } }
            if (top + 16 < 225) { DTH_ADD((s16)(225 - (top + 16)), 0xE0); }
            *p = 0;
            #undef DTH_ADD
            if (!dth_on) { REG_CGWSEL = 0; REG_CGADSUB = 0x3F; hdmaSetup(HDMA_CHANNEL_2, HDMA_MODE_1REG, HDMA_DEST_COLDATA, dth_tab); hdmaEnableMask(1 << HDMA_CHANNEL_2); dth_on = 1; }
        } else if (dth_on) { hdmaDisable(2); REG_CGADSUB = 0; dth_on = 0; }
        if (tour_done && !tour) {                            /* kozematy: everything white, then it fades */
            if (wh_t && !dead && !dth_on && !CSP) { REG_CGWSEL = 0; REG_CGADSUB = 0x3F; REG_COLDATA = (u8)(0xE0 | (wh_t > 30 ? 31 : (wh_t * 31) / 30)); wh_on = 1; }
            else if (wh_on) { if (!dth_on) REG_CGADSUB = 0; REG_COLDATA = 0xE0; wh_on = 0; }
        }
}

int main(void) {
    u16 i, held, pressed, fc, dt;
    u8 first_frame = 1;
    consoleInit();
    snesmodInit();
    snesmodSetSoundbank(SOUNDBANK_BANK);
    snesmodLoadModule(MOD_MUSIC);
    sfx_init();
    snesmodSetModuleVolume(160);
    snesmodPlay(0);
    setMode(BG_MODE1, 0);
    bgSetGfxPtr(0, VRAM_BG1_CHR);
    bgSetMapPtr(0, VRAM_BG1_MAP, BG_MAP_32x32);
    title_show();
    stage_no = 1;
    stage_show(1);
#if HAS_BRIEF
    briefing_show();                     /* the picture of the stage, only when the map has one */
#endif
    dmaClearVRAM();                      /* real hardware/Mesen power on with random VRAM */
    gsuInit();
    gsu_scmr = 0x1B;                    /* 8bpp, 128 rows, RAN + RON */
    gsu_scbr = 0x20;
    gsu_dma_src_hi = 0x80;

    /* video memory layout */
    video_setup();
    minimap_init();
    bgSetScroll(1, (u16)(256 - MM_X), (u16)(256 - MM_Y));
    dmaCopyVram(spr_obj1, VRAM_OBJ1, 8192);  big_shown = 255; big_want = 255;
    for (i = 0; i < 8; i++)
        dmaCopyVram(spr_fx0 + i * 256, (u16)(VRAM_OBJ0 + ((8 + i) * 16 + 8) * 16), 256);
    set_bg_pal(0);
    for (i = 0; i < 128; i++) obj_cur[i] = obj_pal[i];
    dmaCopyCGram((const u8 *)obj_cur, 128, 256);
    oamInit(OBJ_SIZE16_L32, OBJ_NAME_BASE(VRAM_OBJ0));
    oamClear();
    oamHideRange(0, 128);
    { u8 q; wp_want = 0; wp_busy = 0; for (q = 0; q < 4; q++) upload_weapon_step(); }
    wik_shown[0] = wik_shown[1] = 255;

    /* the GSU's tables, map and textures */
    copyGpram();
    for (i = 0; i < 512; i++) sin8w[i] = sin8[i];
    mirror_tables();
    for (i = 0; i < 2048; i++) { *GP_U8(GP_MAP + i) = level_map[i]; map_w[i] = level_map[i]; }
    fk_hide();
    level_no = 1;
    *GP_U16(V_TEXF) = GP_TEXF;
    *GP_U16(V_TEXC) = GP_TEXF + 2 * 256;

    setMainScreen(LAYER_BG1 | LAYER_BG2 | LAYER_OBJ);
    new_game();
    last_fc = getFrameCount();
    setScreenOff();
    set_bg_pal(0);
    setBrightness(15);

    nmiSet(pad_tick); nmiSetLag(pad_tick);
    for (;;) {
        s16 hor, zs;
        fc = getFrameCount();
        dt = (u16)(fc - last_fc);
        last_fc = fc;
        if (dt < 1) dt = 1;
        if (dt > 6) dt = 6;
        held = pad_cur;
        pressed = pad_edge; pad_edge &= (u16)~pressed;          /* every press seen by any frame since the last round, once */

        if (info_win) {                                      /* the visor card: the game is frozen (dt = 0), any button closes it */
            if (info_win == 2 && info_t > 24) { if (pressed & 0x0010) info_win = 3; else if (pressed & 0x0300) { info_pg ^= 1; info_row = 0; sfx(S_BLIP); } }      /* R leaves, left / right turn the page */
            if (info_win == 2) info_t += dt;
            dt = 0; held = 0; pressed = 0;
        }
        if (tour) {                                          /* the HUD tour: controls blocked, the game frozen, 3 s a step */
            tour_t += dt; banner_t = 0;
            if (tour_t >= 150) { tour_t = 0; tour++; tour_rep = 24; if (tour > 8) { tour = 0; tour_done = 1; pal_cur = 255; hud_osig[0] = 255; cs_ph = cs_n = cs_w = cs_win = cs_dim = 0; ATK_RESET(); } tour_build(); }
            dt = 0; held = 0; pressed = 0;
        }

        if (pause_tick(pressed)) { dt = 0; held = 0; pressed = 0; }

        if (CSP) {                                         /* boss cut-scenes: white flash, black text screen, the halves part; the game is frozen */
            { u16 ct = (u16)(cs_n + dt); cs_n = (u8)(CSP == 2 && ct > 250 ? 250 : ct); }
            banner_t = 0; wh_t = 0; hurt_t = 0; flash_t = 0; cr_t = 0; xp_t = 0; bm_t = 0; ar_t = 0;       /* nothing of the fight may reach the card (white-out, crack, flash)  */
            if (CSP == 1 && cs_n >= 24) cs_flash_end();
            else if (CSP == 2 && cs_n >= 30 && (pressed & 0x0010)) { cs_ph = 3; cs_n = 0; sfx(S_SPAWN); }        /* the black card waits for R */
            else if (CSP == 3 && cs_n >= 44) { cs_ph = 0; cs_win = 1; }
            dt = 0; held = 0; pressed = 0;
        }

        if (dead || won) {
            dead_t += dt;
            if (dead && !won && dead_t >= DEATH_END) {
                respawn_player(); dead_t = 0;
            } else if (won && (pressed & KEY_START) && win_t > 60) {
                if (won) {
                    level_no++;
                    stage_no = (u8)((level_no - 1) % 6 + 1);
                    hdmaDisable(1);
                    stage_show(stage_no);                      /* the stages replay this level for now */
#if HAS_BRIEF
                    briefing_show();
#endif
                    setScreenOff();
                    video_setup();
                    bgSetScroll(1, (u16)(256 - MM_X), (u16)(256 - MM_Y));
                    minimap_init();
                    mm_dirty = 1; mm_ox = -100;
                    pal_cur = 255;
                    setMainScreen(LAYER_BG1 | LAYER_BG2 | LAYER_OBJ);
                    first_frame = 1;
                }
                new_game();
                dead_t = 0; win_t = 0;
            }
        }

        {                                                   /* up up down down L R: god mode */
            static const u16 seq[6] = { KEY_UP, KEY_UP, KEY_DOWN, KEY_DOWN, KEY_L, KEY_R };
            if (pressed & (KEY_UP | KEY_DOWN | KEY_L | KEY_R)) {
                if (pressed & seq[cheat_i]) {
                    if (++cheat_i == 6) {
                        cheat_i = 0; player_god ^= 1; white_t = 180; sfx(S_PICK);
                        set_banner(player_god ? "GOD MODE ON" : "GOD MODE OFF");
                    }
                } else cheat_i = (u8)((pressed & KEY_UP) ? 1 : 0);
            }
            if (white_t > dt) white_t -= dt; else white_t = 0;
        }
        {                                                   /* left left right right L R: debug skip - everything but the boss and the 9th crystal */
            static const u16 seq2[6] = { KEY_LEFT, KEY_LEFT, KEY_RIGHT, KEY_RIGHT, KEY_L, KEY_R };
            if (pressed & (KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R)) {
                if (pressed & seq2[cheat2_i]) {
                    if (++cheat2_i == 6) {
                        u8 q;
                        cheat2_i = 0;
                        for (q = 0; q < MAXE; q++) if (en[q].st != E_NONE && en[q].type != 0) en[q].st = E_NONE;
                        for (q = 0; q < NUM_SPAWNS; q++) if (cls_type[spawn_cls[q]] != 0) { sp_st[q] = 2; sp_left[q] = 0; }
                        for (q = 0; q < NUM_ROOMS; q++) { rst[q] = 3; room_left[q] = 0; wv_sp[q] = 0; wv_t[q] = 0; }
                        for (q = 0; q < NUM_ITEMS; q++) item_got[q] = 1;
                        for (q = 0; q < 3; q++) vis_t[q] = 10800;
                        nv_t = 10800; have_d = 7; have_w = (u8)((1 << NW) - 1); crystals = 8; bcry_on = 0;
                        gate_pow = 1; ghost_act = 0; trd_open = 1;
                        for (q = 0; q < NUM_DOORS; q++) if (door_xyk[q][2] == 4) { door_st[q] = 2; set_door(q, 1); }
                        locks_dirty = 1;
                        px = PLATE_X; py = PLATE_Y;
                        white_t = 180; sfx(S_PICK); set_banner("SKIP: GO TO BOSS");
                    }
                } else cheat2_i = (u8)((pressed & KEY_LEFT) ? 1 : 0);
            }
        }
        if (cls_sel) { if (!(held & KEY_B)) held &= (u16)(KEY_R | KEY_B | KEY_L); pressed &= (u16)~KEY_X; }     /* the visor menu: the game stands, only the menu keys work */
        update_player(dt, held, pressed);
        /* camera */
        hor = 64 + ppitch;
        zs = (s16)(pz - dh_d);
        if (moving) hor += (s16)((iabs(SINA(bobph * 2)) * (running ? 5 : 3)) >> 8) - 2;
        hor += (shake & 1) ? shake : -shake;
        if (dead) {                                          /* the hero falls over: eye to the floor, the view rolls up to the ceiling */
            u16 f = dead_t < 40 ? dead_t : 40;
            zs = (s16)(pz - (s16)(f * 3));
            hor = (s16)(hor + (s16)f);
        }
        if (zs > 120) zs = 120;
        if (zs < -120) zs = -120;
        hor = clamp16(hor, 52, 75);                          /* the GSU's floor/ceiling row table has 64 rows */
        hor &= (s16)~1;                                      /* the GSU draws floor and ceiling in pairs of rows */
        {
            static s16 last_zs = 0;
            if (zs != last_zs) {                                 /* floor and ceiling follow the eye height */
                u16 ff = (u16)((zs < -48 ? 16 : 64 + zs) * 4), cc = (u16)((((192 - zs) * 4) / 3));
                u8 r;
                for (r = 1; r < 64; r++) {
                    u32 vf = ((u32)rowd_f[r] * ff) >> 8, vc = ((u32)rowd_c[r] * cc) >> 8;
                    *GP_U16(GP_ROWT + r * 8) = (u16)(vf > 32767 ? 32767 : vf);
                    *GP_U16(GP_ROWT + r * 8 + 6) = (u16)(vc > 32767 ? 32767 : vc);
                }
                last_zs = zs;
            }
        }
        g_horizon = hor;
        g_cam_h8 = 128 + 2 * zs;                      /* same eye height as the wall/floor: (64+zs)*2 */
        *GP_U16(V_TEXF) = (u16)(GP_TEXF + (bike_view() ? 3 * 256 : 0));      /* the chase room: the road texture */
        *GP_U16(V_POSX) = (u16)px;
        *GP_U16(V_POSY) = (u16)py;
        *GP_U16(V_ANG) = pang & 511;
        zb_ang_pend = pang & 511;
        *GP_U16(V_HOR) = (u16)(hor - 12);
        *GP_U16(V_ZS) = (u16)(zs * 256);

        gp_apply();
        gsuLoadFc();
        gsuStartCached(0);                                   /* floor and ceiling: the GSU runs from its cache, the logic goes on meanwhile */
        if (first_frame) gsuSetupHdmaBlanking(24, 56);
        tv_prepare();
        /* the wait for the blank and everything that needs it: the GSU is already drawing the next frame, so the long wait for the next VBlank is not lost */
        WaitForVBlank();                                     /* pads, scroll, OAM flush happen in the NMI */
        /* everything below still runs in the blank */
        if (info_win) koze_rep = 0;
        if (info_wn) {                                       /* the card's rows were built outside the blank: only the copy happens here (the blank is only ~6000 CPU cycles long) */
            u8 q;
            for (q = 0; q < info_wn; q++) {
                dmaCopyVram((const u8 *)(info_buf + q * 32), (u16)(VRAM_BG2_MAP + (info_wr + q) * 32), 64);
                if (info_wr + q < 13) dmaCopyVram(card_tiles + (u16)(info_wr + q + 13 * info_pg) * 832, (u16)(VRAM_BG2_CHR + (CARD_T0 + (info_wr + q) * 26) * 16), 832);
            }
            info_wn = 0;
        }
        if (info_win == 3) {
            info_clr = 12; info_win = 0;
            if (!tour_done) { tour = 1; tour_t = 0; tour_rep = 24; tour_build(); }
        }
        if (koze_on != koze_shown) { koze_shown = koze_on; koze_rep = 4; }   /* the row is written first and again for 4 frames: a write that misses the blank must not stick */
        if (koze_rep) {
            u8 q;
            for (q = 0; q < 32; q++) { u8 dc = (u8)((q - 22) & 31); koze_row[q] = (u16)((koze_shown >= 16 || dc < koze_shown || dc > 32 - koze_shown) ? (0x2C00 | (HT_BASE + 45)) : BLANK_TILE); }   /* BG column 22 = the cross-hair */
            dmaCopyVram((const u8 *)koze_row, (u16)(VRAM_BG2_MAP + 7 * 32), 64);
            koze_rep--;
        }
        if (tour_rep && !info_clr) {                         /* one 64-byte chunk a round, many rounds: a write that misses the blank must not stick */
            if (tour) { if (tv_have) dmaCopyVram(tv_buf, tv_dst, 64); }
            else {                                            /* back to the original marker cells */
                u8 n = (u8)(tour_rep & 7), k = (u8)(n >> 1), q = (u8)(n & 1);
                u16 t = glyph_tile(tv_cell[k & 3]);
                dmaCopyVram(spr_obj1 + (u16)((t - 256 + q * 16) * 32), (u16)(VRAM_OBJ1 + (t - 256) * 16 + q * 256), 64);
            }
            tour_rep--;
            if (tv_ready) tv_ready--;
        }
        if (CSP == 3) {                                    /* the picture opens from the centre: BG1 and sprites are visible inside a window that widens */
            u8 hw = (u8)((cs_n * 128) / 44);
            REG_W12SEL = 0x33; REG_WOBJSEL = 0x03; REG_WH0 = (u8)(128 - hw); REG_WH1 = (u8)(127 + hw); REG_WBGLOG = 0; REG_TMW = 0x13;
            cs_win = 1;
        } else if (tour_done && cs_win) { REG_TMW = 0; REG_W12SEL = 0; REG_WOBJSEL = 0; cs_win = 0; }
        if (CSP == 2) setMainScreen(LAYER_OBJ);
        else if (won) setMainScreen(LAYER_OBJ); else if (bst == 12) setMainScreen(LAYER_BG1 | LAYER_OBJ); else if (info_win) setMainScreen(LAYER_BG2 | LAYER_OBJ); else setMainScreen(LAYER_BG1 | LAYER_BG2 | LAYER_OBJ);
        if (info_win) { static const u16 blk[1] = { 0 }; dmaCopyCGram((const u8 *)blk, 0, 2); pal_cur = 253; }   /* the visor card: a black backdrop */
        else if (tour) { if (pal_cur != 252) { view_fx(bg_pal[0], 1, 0); pal_cur = 252; } }      /* grey picture: BG1 only, the HUD keeps its colours */
        else if (CSP == 2) { static const u16 blk[1] = { 0 }; dmaCopyCGram((const u8 *)blk, 0, 2); pal_cur = 253; }
        else if (CSP == 1) { pal_blend_white(bg_pal[0], (u8)(cs_n >= 24 ? 13 : (cs_n * 13) / 24)); pal_cur = 254; }
        else if (rail_on) { if (pal_cur != 248) { view_fx(bg_pal[0], 4, 14); pal_cur = 248; } }
        else if (dead) set_bg_pal(3);
        else if (white_t)             /* god mode: 3 s of white tones */
            pal_blend_white(bg_pal[0], (u8)(white_t > 120 ? 13 : (white_t * 13) / 120));
        else if (bst == 12) {                                   /* the matrix: the room as dark green tones */
            if (pal_cur != 249) { view_fx(bg_pal[0], 2, 0); pal_cur = 249; }
        }
        else if (bike_view()) { if (pal_cur != 250 || !(last_fc & 15)) { pal_dim_bike(); pal_cur = 250; } }      /* written again every 16 frames: a palette write that misses the blank must not stick */      /* the tunnel: dim */
        else if (hurt_t) set_bg_pal(2);
        else if (flash_t) set_bg_pal(1);
        else if (dev_on & 1) set_bg_pal(6);
        else if (dev_on & 2) set_bg_pal(7);
        else if (dev_on & 4) set_bg_pal(8);
        else set_bg_pal(0);
        screen_strips();
        minimap_upload();
        if (info_clr) {                                      /* the clean-up is written again for a few frames: a write that misses the blank must not stick */
            u8 r, q = (u8)((info_clr & 3) * 4);
            for (r = q; r < q + 4 && r < 14; r++) dmaCopyVram((const u8 *)koze_blank, (u16)(VRAM_BG2_MAP + r * 32), 64);
            if (--info_clr == 0) { koze_shown = 255; koze_rep = 0; hud_dirty = 1; mm_dirty = 1; }
        }
        if (wp_pal_dirty) { set_obj_pal(1, bst == 12 ? wire_pal_sw : bst == 10 ? hbar_pal : weapon_pal_data[cur_w]); wp_pal_dirty = 0; }
        if (obj_dirty) {
            dmaCopyCGram((const u8 *)obj_cur, 128, 256);
            obj_dirty = 0;
        }
        upload_pending();
        if (wp_want != wp_shown || wp_busy) upload_weapon_step();
        {
            u8 sl;
            for (sl = 0; sl < 2; sl++) if (wik_want[sl] != 255 && wik_want[sl] != wik_shown[sl]) {
                u16 d = (u16)(VRAM_OBJ1 + (u16)((u16)(glyph_tile((u8)(54 + sl)) - 256) * 16));
                dmaCopyVram(spr_wicons + (u16)wik_want[sl] * 128, d, 64);
                dmaCopyVram(spr_wicons + (u16)wik_want[sl] * 128 + 64, (u16)(d + 256), 64);
                wik_shown[sl] = wik_want[sl];
            }
        }
        if (big_want != big_shown) { big_shown = big_want; big_rep = 5; }
        if (big_rep) {                                       /* the block is written again for a few frames: a write that misses the blank must not stick */
            u8 r;
            const u8 *bs = (big_shown == 255) ? spr_obj1 + 12 * 512 : spr_big + (u16)big_shown * 512;
            for (r = 0; r < 4; r++) dmaCopyVram(bs + (u16)r * (big_shown == 255 ? 512 : 128), (u16)(VRAM_OBJ1 + (12 + r) * 256), 128);
            big_rep--;
        }
        if (top_wr && !info_win && !info_wn) { dmaCopyVram((const u8 *)info_buf, (u16)(VRAM_BG2_MAP + 30 * 32), 64); dmaCopyVram((const u8 *)(info_buf + 32), (u16)(VRAM_BG2_MAP + 13 * 32), 64); top_wr = 0; }
        if (!(last_fc & 63) && !info_win && !info_wn) blk_rows();
        if (hud_dirty) { dmaCopyVram((const u8 *)hud_row, (u16)(VRAM_BG2_MAP + 14 * 32), 64); for (i = 0; i < 32; i++) hud_last[i] = hud_row[i]; hud_dirty = 0; }
        if (blink_t) blink_draw(); else if (blink_was) blink_end();
        if (first_frame) { setScreenOn(); first_frame = 0; }
#ifdef DBG_TM
        REG_TM = DBG_TM;
#endif
        wu_acc = (u16)(wu_acc + dt);
        if (!dead && !won && !cls_sel && (wu_phase ^= 1)) { g_dt = wu_acc; wu_acc = 0;
#ifdef DBG_DT
            dbg_sum += dt; if (++dbg_t >= 32) { dbg_max = (u8)(dbg_sum * 10 / 32); dbg_t = 0; dbg_sum = 0; }
#endif
            world_update(); minimap_update(); }
        {                                                   /* music: calm section (orders 0..) / action section when enemies are around */
            u8 combat = 0;
            for (i = 0; i < MAXE; i++) if (en[i].st != E_NONE && en[i].st != E_DEAD && en_act[i]) combat = (en[i].type == 0) ? 2 : (combat ? combat : 1);
            if (combat && !dead && !won) {
                mus_hold = 600;                          /* battle music stays until 10 s without enemies */
                if (combat > mus_mode || mus_mode == 3) mus_set(combat);
            } else if (mus_mode) {
                if (mus_hold > dt) mus_hold -= dt;
                else mus_set(0);
            }
        }
        if (!cls_sel) for (i = 0; i < MAXE; i++) if (en_act[i]) enemy_update(&en[i], dt);
        gsuWait();
        gp_apply();
        gsuLoadW();
        gsuStartCached(0);                                   /* the walls: bullets, sprites and the HUD are prepared meanwhile (they use the last frame's wall distances) */
        if (!cls_sel) { bolts_update(dt); fx_update(dt); }
        if (won) win_t += dt;
        update_weapon_lowering(dt);
#ifdef GS_TEST
        static u16 gtc;
        if (!info_win) gtc++;
        if (gtc == 5) spawn_enemy(px, py, 1, 0);
        if (gtc > 5) {
            s16 d = (s16)(1500 - (s16)((gtc - 5) * 10)); if (d < 260) d = 260;
            en[0].x = (s16)(px + (s16)(((s32)COSA(pang) * d) >> 8)); en[0].y = (s16)(py + (s16)(((s32)SINA(pang) * d) >> 8));
            if (en[0].st == E_TELE || en[0].st >= E_IDLE) { if (en[0].t < 3) en[0].t = 3; }
            if (en[0].st != E_TELE) { en[0].st = E_IDLE; en[0].hp = 99; }
        }
#endif
        plan_enemy_sprites();
        draw_world_sprites();
        if (tour) {
            const char *tt = tour_txt[tour - 1]; u8 n = 0;
            while (tt[n]) n++;
            put_text((u8)(OAM_ENEMY + 16), (s16)(104 - n * 6), 66, tt);
        }
        draw_bolts_fx();
        draw_items(fc);
        draw_weapon(fc);
        draw_hud(fc);
        draw_attacks();
        if (bst == 12) draw_wires(fc);
        if (tour && !tv_ready) { u8 j; for (j = 0; j < tv_cnt; j++) { oamSet(tv_id[j], (u16)tv_x[j], 24, glyph_tile(tv_cell[tv_k[j]]), 2, 3, 0); oamSetSize(tv_id[j], 0); } }
        if (info_win == 1 || info_win == 2) {                /* the card: two BG rows per round (narrow text tiles come ready-made from ROM, see tools/gen_card.py); once all 14 are there they go round again, so a missed write heals */
            u8 q, c;
            info_wr = info_row; info_wn = 2;
            for (q = 0; q < 2; q++) {
                u16 *m = info_buf + q * 32;
                for (c = 0; c < 32; c++) m[c] = BLANK_TILE;
                if (info_row < 13) for (c = 0; c < card_nt[info_row + 13 * info_pg]; c++) m[(u8)((8 + c) & 31)] = (u16)(0x2800 | (CARD_T0 + info_row * 26 + c));
                info_row++;
            }
            if (info_row >= 14) { if (info_win == 1) { info_win = 2; info_t = 0; info_clr = 0; } info_row = 0; }
        }
        if (CSP == 2) {                                    /* the black screen: the title, a second title line, the small note */
            static const char *const cs_a[8] = { "JUMP TO SURVIVE", "ALL CRYSTALS FOUND", "CATCH UP OR GET OUT", "GET AWAY FROM", "JUMP UP ON A CLOUD", "WAIT NOT TO LATE", "TURN ON THE LIGHT", "" };
            static const char *const cs_b[8] = { "IN THE DARKNESS", "TIME TO ESCAPE", "", "THE NIGHT", "", "", "", "" };
            u8 id, w = (u8)(cs_w & 7), n = note_of[w], k, l1 = 0, l2 = 0;
            const char *p;
            oamHideRange(0, 128);
            for (p = cs_a[w]; *p; p++) l1++;
            for (p = cs_b[w]; *p; p++) l2++;
            id = OAM_ENEMY;
            id = put_text(id, (s16)((256 - 12 * l1) / 2), (s16)(n == 255 ? 84 : 76), cs_a[w]);
            put_text(id, (s16)((256 - 12 * l2) / 2), (s16)(n == 255 ? 112 : 94), cs_b[w]);
            if (n != 255 && wp_shown == (u8)(8 + n)) for (k = 0; k < note_n[n]; k++) {
                oamSet(k, (u16)(note_x[n] + 16 * k), 148, (u16)(128 + 32 * (k >> 2) + 2 * (k & 3)), 2, 3, 0); oamSetSize(k, 0);
            }
        }
        if (info_win) {                                      /* visor card: only the three icons stay on the sprite layer */
            oamHideRange(0, 128);
        }

        gsuWait();
        zb_ang = zb_ang_pend;
        gs_draw();
        {                                                   /* this frame's wall distances for the next one: Game Pak RAM -> WRAM through the WRAM port */
            u16 wa = (u16)(u32)zbuf_w;
            *(volatile u8 *)0x2181 = (u8)wa; *(volatile u8 *)0x2182 = (u8)(wa >> 8); *(volatile u8 *)0x2183 = 0;
            *(volatile u8 *)0x4340 = 0x00; *(volatile u8 *)0x4341 = 0x80;
            *(volatile u8 *)0x4342 = (u8)GP_ZBUF; *(volatile u8 *)0x4343 = (u8)(GP_ZBUF >> 8); *(volatile u8 *)0x4344 = 0x70;
            *(volatile u8 *)0x4345 = (u8)(VIEW_COLS * 2); *(volatile u8 *)0x4346 = 0x00;
            *(volatile u8 *)0x420B = 0x10;
        }
        dbg_it++;
        gsuDmaFrame8();                                      /* 23.3 KB into VRAM, in the PAL letterbox */

        snesmodProcess();
        sfx_tick();
    }
    return 0;
}
