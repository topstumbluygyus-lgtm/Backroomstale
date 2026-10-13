// BACKROOMSTALE - fan RPG for PSP. Original art drawn with code.
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspaudio.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

PSP_MODULE_INFO("Backroomstale", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

#define RGB(r,g,b) (0xFF000000u|((b)<<16)|((g)<<8)|(r))
#define WHITE  0xFFFFFFFFu
#define BLACK  RGB(10,10,10)
#define RED    RGB(255,0,0)
#define YEL    RGB(255,235,0)
#define GRN    RGB(0,220,0)
#define ORANGE RGB(255,140,0)
#define WALL   RGB(200,184,77)
#define WALL2  RGB(176,160,60)
#define GRAY   RGB(150,150,150)
#define DGRAY  RGB(70,70,80)
#define PINK   RGB(255,120,190)
#define DRED   RGB(130,20,20)

static unsigned int *vram = (unsigned int *)0x44000000;
static SceCtrlData pad;
static unsigned int pressed, oldb;

static int exit_cb(int a, int b, void *c) { sceKernelExitGame(); return 0; }
static int cb_thread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_cb(void) {
    int t = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);
}

static void rect(int x, int y, int w, int h, unsigned c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 480) w = 480 - x;
    if (y + h > 272) h = 272 - y;
    for (int j = 0; j < h; j++) {
        unsigned int *p = vram + (y + j) * 512 + x;
        for (int i = 0; i < w; i++) p[i] = c;
    }
}
static void frame(int x, int y, int w, int h, unsigned c) {
    rect(x, y, w, 2, c); rect(x, y + h - 2, w, 2, c);
    rect(x, y, 2, h, c); rect(x + w - 2, y, 2, h, c);
}
static void txt(int px, int py, unsigned col, const char *s) {
    char line[96];
    int cx = px / 7, cy = py / 8;
    pspDebugScreenSetTextColor(col);
    while (*s) {
        int n = 0;
        while (*s && *s != '\n' && n < 90) line[n++] = *s++;
        line[n] = 0;
        if (*s == '\n') s++;
        pspDebugScreenSetXY(cx, cy++);
        pspDebugScreenPrintf("%s", line);
    }
}

// ---------- DATA ----------
typedef struct {
    const char *name; int maxhp; int pat;
    const char *check; const char *talk[3]; const char *flavor;
} EDef;

static const EDef defs[4] = {
    {"SMILER", 30, 0,
     "SMILER - ATK 4 DEF 0\nIt only smiles in the dark. Tired of it.",
     {"You say the lights are nicer when\nit's off. Smiler blinks.",
      "You admit you're lost too.\nIts grin wobbles.",
      "You offer a torch. Smiler looks shy."},
     "Two white eyes float in the dark."},
    {"SKIN-STEALER", 40, 1,
     "SKIN-STEALER - ATK 6\nWears a person it never met.",
     {"You ask its name.\nIt mimics your voice.",
      "You compliment its borrowed face.\nIt hesitates.",
      "You hand it a spare hat.\nIt wants to go home too."},
     "It tries on your silhouette."},
    {"PARTYGOER", 50, 2,
     "PARTYGOER - ATK 5\nHappy birthday. Nobody's birthday.",
     {"You sing happy birthday. Off-key.",
      "You say it's YOUR birthday.\nConfetti falls.",
      "You share your bread as cake.\nIt cries glitter."},
     "Balloons drift. Nothing is celebrated."},
    {"THE MANAGER", 80, 3,
     "THE MANAGER - ATK 8\nKeeper of Level 0's exit. Wants staff.",
     {"You politely decline the job offer.",
      "You say you only want to go home.",
      "You ask who hired the Manager.\nSilence. Then, a sigh."},
     "The fluorescent hum gets louder."}
};

static const char *story[4] = {
    "Steve was a miner.\nAlways digging down, always one block deeper.",
    "One night he dug where no map showed ground.\nHis pickaxe struck... nothing.\nThe floor gave way.",
    "He fell through the world and landed on\ndamp yellow carpet. Buzzing lights.\nNo sky. No bedrock. An empty room.",
    "A voice: 'Level 0. Noclip complete.'\nTo get home, Steve must reach the exit.\nFight the entities... or be kind to them."
};

static const char *ending[3] = {
    // pacifist
    "PACIFIST ENDING\n\nThe Manager tears up the staff list.\nSmiler leaves the lights on for you.\nThe Partygoer throws one last party.\nThe door opens onto a grass field.\nSteve sees the sun. Everyone waves.\n\nYou befriended the Backrooms.",
    // genocide
    "GENOCIDE ENDING\n\nThe hum stops. All lights go out.\nThe door opens... onto nothing.\nSteve walks home through the static.\nBut he never sees the sun again.\nThe carpet remembers every step.\n\nYou are alone in the Backrooms.",
    // neutral
    "NEUTRAL ENDING\n\nThe door opens. Steve gets home.\nSome entities cheer. Some are gone.\nHe digs a little less deep now.\n\nSomething in between."
};

// ---------- STATE ----------
enum { S_TITLE, S_STORY, S_WORLD, S_BATTLE, S_END, S_OVER };
enum { B_MENU, B_FIGHT, B_ACT, B_MSG, B_DODGE, B_FIN };

static int state, page;
static int ehp[4], gone[4], kills, spares, php;
static int sx, sy, endkind;
static const int ex[3] = {70, 215, 330};
static const int ey[3] = {60, 150, 50};

static int eid, bphase, sel, asel, curs, mercy, after, dodgeT, inv, notice;
static float hx, hy;
static char msg[220];

#define BX 170
#define BY 110
#define BW 140
#define BH 90

typedef struct { float x, y, vx, vy; int on; } Bul;
static Bul bl[40];

static void reset_game(void) {
    for (int i = 0; i < 4; i++) { ehp[i] = defs[i].maxhp; gone[i] = 0; }
    kills = spares = 0; php = 20; sx = 20; sy = 130;
    state = S_STORY; page = 0; notice = 0;
}

static void say(const char *m, int nx) {
    strncpy(msg, m, sizeof(msg) - 1); msg[sizeof(msg) - 1] = 0;
    after = nx; bphase = B_MSG;
}

static void start_battle(int id) {
    eid = id; mercy = 0; sel = 0; bphase = B_MENU; state = S_BATTLE;
    memset(bl, 0, sizeof(bl));
}

static void start_dodge(void) {
    dodgeT = 270; inv = 0;
    hx = BX + BW / 2; hy = BY + BH / 2;
    memset(bl, 0, sizeof(bl));
}

static void spawn(float x, float y, float vx, float vy) {
    for (int i = 0; i < 40; i++) if (!bl[i].on) {
        bl[i].x = x; bl[i].y = y; bl[i].vx = vx; bl[i].vy = vy; bl[i].on = 1; return;
    }
}

static void dodge_update(void) {
    float sp = (pad.Buttons & PSP_CTRL_CIRCLE) ? 1.0f : 2.0f;
    if (pad.Buttons & PSP_CTRL_LEFT)  hx -= sp;
    if (pad.Buttons & PSP_CTRL_RIGHT) hx += sp;
    if (pad.Buttons & PSP_CTRL_UP)    hy -= sp;
    if (pad.Buttons & PSP_CTRL_DOWN)  hy += sp;
    if (hx < BX + 3) hx = BX + 3;
    if (hx > BX + BW - 10) hx = BX + BW - 10;
    if (hy < BY + 3) hy = BY + 3;
    if (hy > BY + BH - 10) hy = BY + BH - 10;

    int p = defs[eid].pat;
    float bx = BX + rand() % BW;
    float by = BY + rand() % BH;
    int rain = (p == 0 && dodgeT % 10 == 0) || (p == 3 && dodgeT % 16 == 0);
    int side = (p == 1 && dodgeT % 12 == 0) || (p == 3 && dodgeT % 20 == 0);
    int aim  = (p == 2 && dodgeT % 22 == 0) || (p == 3 && dodgeT % 30 == 0);
    if (rain) spawn(bx, BY, 0, 1.5f + eid * 0.3f);
    if (side) { int l = rand() % 2; spawn(l ? BX : BX + BW, by, l ? 2.0f : -2.0f, 0); }
    if (aim) spawn(bx, BY, (hx - bx) / 55.0f, (hy - BY) / 55.0f);

    if (inv > 0) inv--;
    for (int i = 0; i < 40; i++) if (bl[i].on) {
        bl[i].x += bl[i].vx; bl[i].y += bl[i].vy;
        if (bl[i].x < BX - 6 || bl[i].x > BX + BW + 6 || bl[i].y < BY - 6 || bl[i].y > BY + BH + 6) { bl[i].on = 0; continue; }
        if (!inv && bl[i].x > hx - 5 && bl[i].x < hx + 7 && bl[i].y > hy - 5 && bl[i].y < hy + 7) {
            php -= 2 + eid; inv = 40; bl[i].on = 0;
        }
    }
    if (php <= 0) { state = S_OVER; return; }
    if (--dodgeT <= 0) bphase = B_MENU;
}

static void do_hit(int d) {
    char t[100];
    ehp[eid] -= d;
    if (ehp[eid] <= 0) {
        ehp[eid] = 0; kills++; endkind = 1;
        snprintf(t, sizeof(t), "%s was defeated.\nStatic fills the room.", defs[eid].name);
        say(t, B_FIN);
    } else {
        snprintf(t, sizeof(t), "You hit for %d damage.", d);
        say(t, B_DODGE);
    }
}

static void do_mercy(void) {
    char t[100];
    if (eid == 3 && kills >= 3) {
        say("The Manager: 'You already chose.'\nMercy fails.", B_DODGE);
    } else if (mercy >= 3) {
        spares++; endkind = 2;
        snprintf(t, sizeof(t), "You spared %s.\nIt fades, smiling.", defs[eid].name);
        say(t, B_FIN);
    } else {
        say("It isn't ready.\nTry ACT > Talk.", B_DODGE);
    }
}

static void battle_update(void) {
    char t[160];
    switch (bphase) {
    case B_MENU:
        if (pressed & PSP_CTRL_LEFT)  sel = (sel + 2) % 3;
        if (pressed & PSP_CTRL_RIGHT) sel = (sel + 1) % 3;
        if (pressed & PSP_CTRL_CROSS) {
            if (sel == 0) { bphase = B_FIGHT; curs = 0; }
            else if (sel == 1) { bphase = B_ACT; asel = 0; }
            else do_mercy();
        }
        break;
    case B_FIGHT:
        curs += 6;
        if (pressed & PSP_CTRL_CROSS) {
            int d = 25 - abs(curs - 200) / 8; if (d < 4) d = 4;
            do_hit(d);
        } else if (curs > 400) say("You missed.", B_DODGE);
        break;
    case B_ACT:
        if (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) asel ^= 1;
        if (pressed & PSP_CTRL_CIRCLE) bphase = B_MENU;
        if (pressed & PSP_CTRL_CROSS) {
            if (asel == 0) say(defs[eid].check, B_DODGE);
            else {
                int k = mercy < 3 ? mercy : 2;
                if (mercy < 3) mercy++;
                snprintf(t, sizeof(t), "%s%s", defs[eid].talk[k], mercy >= 3 ? "\n(It can be spared now.)" : "");
                say(t, B_DODGE);
            }
        }
        break;
    case B_MSG:
        if (pressed & PSP_CTRL_CROSS) {
            if (after == B_DODGE) start_dodge();
            bphase = after;
        }
        break;
    case B_DODGE:
        dodge_update();
        break;
    case B_FIN:
        if (pressed & PSP_CTRL_CROSS) {
            gone[eid] = 1;
            if (eid == 3) {
                endkind = (spares == 4) ? 0 : (kills == 4) ? 1 : 2;
                state = S_END;
            } else {
                state = S_WORLD; php += 8; if (php > 20) php = 20;
            }
        }
        break;
    }
}

// ---------- DRAWING ----------
static void draw_enemy(int id, int x, int y, int s) {
    switch (id) {
    case 0:
        rect(x, y, 16 * s, 12 * s, BLACK);
        rect(x + 3 * s, y + 3 * s, 3 * s, 3 * s, WHITE);
        rect(x + 10 * s, y + 3 * s, 3 * s, 3 * s, WHITE);
        rect(x + 2 * s, y + 8 * s, 12 * s, 2 * s, WHITE);
        break;
    case 1:
        rect(x + 4 * s, y, 8 * s, 8 * s, GRAY);
        rect(x + 2 * s, y + 8 * s, 12 * s, 14 * s, DGRAY);
        rect(x + 5 * s, y + 3 * s, 2 * s, 2 * s, BLACK);
        rect(x + 9 * s, y + 3 * s, 2 * s, 2 * s, BLACK);
        rect(x + 6 * s, y + 6 * s, 4 * s, 1 * s, BLACK);
        break;
    case 2:
        rect(x, y + 6 * s, 14 * s, 14 * s, PINK);
        rect(x + 4 * s, y, 6 * s, 2 * s, YEL);
        rect(x + 3 * s, y + 2 * s, 8 * s, 4 * s, ORANGE);
        rect(x + 3 * s, y + 9 * s, 2 * s, 2 * s, BLACK);
        rect(x + 9 * s, y + 9 * s, 2 * s, 2 * s, BLACK);
        break;
    default:
        rect(x, y, 20 * s, 24 * s, DRED);
        rect(x + 4 * s, y + 5 * s, 4 * s, 3 * s, WHITE);
        rect(x + 12 * s, y + 5 * s, 4 * s, 3 * s, WHITE);
        rect(x + 9 * s, y + 10 * s, 2 * s, 10 * s, BLACK);
    }
}

static void draw_steve(int x, int y) {
    rect(x + 2, y, 10, 8, RGB(200, 150, 110));
    rect(x + 3, y + 3, 2, 2, BLACK);
    rect(x + 9, y + 3, 2, 2, BLACK);
    rect(x, y + 8, 14, 7, RGB(0, 170, 170));
    rect(x + 2, y + 15, 10, 6, RGB(50, 50, 170));
}

static void draw_heart(int x, int y) {
    rect(x, y, 3, 3, RED); rect(x + 4, y, 3, 3, RED);
    rect(x, y + 2, 7, 3, RED); rect(x + 1, y + 5, 5, 1, RED); rect(x + 2, y + 6, 3, 1, RED);
}

static void draw_hud(void) {
    char t[64];
    txt(20, 244, WHITE, "STEVE  LV 1  HP");
    rect(150, 244, 40, 8, RED);
    rect(150, 244, php > 0 ? php * 2 : 0, 8, YEL);
    snprintf(t, sizeof(t), "%d / 20", php);
    txt(200, 244, WHITE, t);
}

static void draw_battle(void) {
    char t[64];
    rect(0, 0, 480, 272, BLACK);
    draw_enemy(eid, 200, 20, 3);
    rect(190, 98, 100, 5, RED);
    rect(190, 98, 100 * ehp[eid] / defs[eid].maxhp, 5, GRN);

    if (bphase == B_DODGE) {
        frame(BX, BY, BW, BH, WHITE);
        for (int i = 0; i < 40; i++) if (bl[i].on) rect((int)bl[i].x, (int)bl[i].y, 5, 5, WHITE);
        if (!(inv > 0 && (inv / 3) % 2)) draw_heart((int)hx, (int)hy);
    } else {
        frame(20, 110, 440, 90, WHITE);
        if (bphase == B_MENU) txt(34, 122, WHITE, defs[eid].flavor);
        else if (bphase == B_MSG || bphase == B_FIN) txt(34, 122, WHITE, msg);
        else if (bphase == B_ACT) {
            txt(60, 130, asel == 0 ? YEL : WHITE, "* Check");
            txt(200, 130, asel == 1 ? YEL : WHITE, "* Talk");
            txt(34, 175, GRAY, "O: back");
        } else if (bphase == B_FIGHT) {
            rect(40, 140, 400, 30, DGRAY);
            rect(236, 140, 8, 30, GRN);
            rect(40 + curs, 136, 6, 38, WHITE);
            txt(34, 180, GRAY, "Press X at the center!");
        }
    }
    static const char *bn[3] = {"FIGHT", "ACT", "MERCY"};
    for (int i = 0; i < 3; i++) {
        int bx = 50 + i * 140;
        unsigned c = (bphase == B_MENU && sel == i) ? YEL : ORANGE;
        frame(bx, 208, 100, 24, c);
        txt(bx + 28, 216, c, bn[i]);
    }
    draw_hud();
    (void)t;
}

static void draw_world(void) {
    char t[64];
    rect(0, 0, 480, 272, WALL);
    for (int x = 0; x < 480; x += 40) rect(x, 0, 3, 272, WALL2);
    rect(0, 0, 480, 22, RGB(60, 60, 40));
    for (int x = 30; x < 480; x += 140) rect(x, 4, 40, 8, WHITE);
    for (int i = 0; i < 3; i++) if (!gone[i]) draw_enemy(i, ex[i], ey[i], 2);
    rect(452, 100, 28, 70, RGB(110, 70, 30));
    txt(448, 90, BLACK, "EXIT");
    draw_steve(sx, sy);
    snprintf(t, sizeof(t), "Spared:%d  Defeated:%d", spares, kills);
    txt(8, 252, BLACK, t);
    if (notice > 0) { txt(120, 235, DRED, "The door is sealed. Entities remain."); notice--; }
}

static void world_update(void) {
    if (pad.Buttons & PSP_CTRL_LEFT)  sx -= 2;
    if (pad.Buttons & PSP_CTRL_RIGHT) sx += 2;
    if (pad.Buttons & PSP_CTRL_UP)    sy -= 2;
    if (pad.Buttons & PSP_CTRL_DOWN)  sy += 2;
    if (sx < 0) sx = 0;
    if (sy < 24) sy = 24;
    if (sy > 250) sy = 250;
    if (sx > 438) {
        if (gone[0] && gone[1] && gone[2]) {
            if (sx > 446 && sy > 90 && sy < 160) { start_battle(3); return; }
        } else { sx = 438; notice = 90; }
    }
    if (sx > 466) sx = 466;
    for (int i = 0; i < 3; i++) if (!gone[i]) {
        if (abs(sx - ex[i]) < 24 && abs(sy - ey[i]) < 30) { start_battle(i); return; }
    }
}

// ---------- MUSIC (original chiptune, synthesized) ----------
#define A2 110.0f
#define F3 174.6f
#define G3 196.0f
#define EB3 155.6f
#define A3 220.0f
#define B3 246.9f
#define C4 261.6f
#define D4 293.7f
#define E4 329.6f
#define EB4 311.1f
#define F4 349.2f
#define G4 392.0f
#define A4 440.0f
#define B4 493.9f
#define C5 523.3f
#define E5 659.3f

typedef struct { float step; float lead[16]; float bass[16]; float hum; } Track;
static const Track tracks[5] = {
    // 0: title / story - eerie and slow
    {0.35f,
     {A3,0,C4,0,E4,0,C4,0, B3,0,D4,0,F4,0,D4,0},
     {A2,A2,A2,A2,A2,A2,A2,A2, G3,G3,G3,G3,G3,G3,G3,G3}, 0.02f},
    // 1: Level 0 - fluorescent hum
    {0.30f,
     {A3,0,0,0,E4,0,0,0, C4,0,0,0,B3,0,0,0},
     {A2,0,0,0,A2,0,0,0, F3,0,0,0,G3,0,0,0}, 0.06f},
    // 2: battle
    {0.14f,
     {A4,E4,C5,E4,A4,E4,C5,E4, G4,D4,B4,D4,G4,D4,B4,D4},
     {A2,A2,A2,A2,A2,A2,A2,A2, G3,G3,G3,G3,G3,G3,G3,G3}, 0.0f},
    // 3: boss - tense, tritone
    {0.11f,
     {A4,A4,EB4,A4,E5,A4,EB4,A4, B4,B4,F4,B4,E5,B4,F4,B4},
     {A2,A2,EB3,A2,A2,A2,EB3,A2, A2,A2,EB3,A2,F3,F3,EB3,EB3}, 0.03f},
    // 4: ending - soft
    {0.40f,
     {C4,E4,G4,C5,G4,E4,C4,0, D4,F4,A4,C5,A4,F4,D4,0},
     {C4,0,0,0,C4,0,0,0, F3,0,0,0,G3,0,0,0}, 0.0f}
};

static volatile int track = 0;

static int audio_thread(SceSize args, void *argp) {
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 512, PSP_AUDIO_FORMAT_STEREO);
    if (ch < 0) return 0;
    static short buf[512 * 2];
    float lp = 0, bp = 0, hp = 0;
    int pos = 0, stepi = 0, cur = -1;
    while (1) {
        int t = track;
        if (t != cur) { cur = t; stepi = 0; pos = 0; }
        const Track *T = &tracks[cur];
        int stepLen = (int)(T->step * 44100.0f);
        for (int i = 0; i < 512; i++) {
            if (pos >= stepLen) { pos = 0; stepi = (stepi + 1) & 15; }
            float env = 1.0f - (float)pos / (float)stepLen;
            float s = 0;
            float f = T->lead[stepi];
            lp += f / 44100.0f; if (lp >= 1.0f) lp -= 1.0f;
            if (f > 0) s += (lp < 0.5f ? 0.5f : -0.5f) * env * 0.9f * 0.5f;
            float b = T->bass[stepi];
            bp += b / 44100.0f; if (bp >= 1.0f) bp -= 1.0f;
            if (b > 0) s += (bp < 0.5f ? 4 * bp - 1 : 3 - 4 * bp) * 0.25f;
            hp += 120.0f / 44100.0f; if (hp >= 1.0f) hp -= 1.0f;
            s += (hp < 0.5f ? 4 * hp - 1 : 3 - 4 * hp) * T->hum;
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            short v = (short)(s * 14000.0f);
            buf[i * 2] = v; buf[i * 2 + 1] = v;
            pos++;
        }
        sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, buf);
    }
    return 0;
}

int main(void) {
    setup_cb();
    int at = sceKernelCreateThread("audio", audio_thread, 0x12, 0x10000, 0, NULL);
    if (at >= 0) sceKernelStartThread(at, 0, NULL);
    pspDebugScreenInit();
    pspDebugScreenEnableBackColor(0);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    srand(12345);
    state = S_TITLE;

    while (1) {
        sceCtrlReadBufferPositive(&pad, 1);
        pressed = pad.Buttons & ~oldb; oldb = pad.Buttons;
        sceDisplayWaitVblankStart();

        track = (state == S_WORLD) ? 1 : (state == S_BATTLE) ? (eid == 3 ? 3 : 2) : (state == S_END) ? 4 : 0;

        switch (state) {
        case S_TITLE:
            rect(0, 0, 480, 272, BLACK);
            rect(0, 100, 480, 4, WALL);
            txt(160, 80, YEL, "B A C K R O O M S T A L E");
            txt(150, 130, WHITE, "A fan RPG. Steve vs. Level 0.");
            txt(190, 180, GRAY, "Press X to start");
            if (pressed & PSP_CTRL_CROSS) reset_game();
            break;
        case S_STORY:
            rect(0, 0, 480, 272, BLACK);
            txt(40, 90, WHITE, story[page]);
            txt(300, 240, GRAY, "X: next");
            if (pressed & PSP_CTRL_CROSS) { if (++page >= 4) state = S_WORLD; }
            break;
        case S_WORLD:
            world_update();
            if (state == S_WORLD) draw_world();
            break;
        case S_BATTLE:
            battle_update();
            if (state == S_BATTLE) draw_battle();
            break;
        case S_END:
            rect(0, 0, 480, 272, BLACK);
            txt(60, 40, endkind == 1 ? RED : endkind == 0 ? YEL : WHITE, ending[endkind]);
            txt(280, 245, GRAY, "X: back to title");
            if (pressed & PSP_CTRL_CROSS) state = S_TITLE;
            break;
        case S_OVER:
            rect(0, 0, 480, 272, BLACK);
            txt(120, 100, RED, "You woke up on the yellow carpet again.");
            txt(160, 130, WHITE, "Stay determined... Steve.");
            txt(190, 180, GRAY, "X: retry");
            if (pressed & PSP_CTRL_CROSS) { php = 20; start_battle(eid); }
            break;
        }
    }
    return 0;
}
