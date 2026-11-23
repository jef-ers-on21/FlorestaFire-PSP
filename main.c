/**
 * Floresta Fire - main.c (versão com Menu + Lobby + Mundo)
 * Battle Royale estilo Free Fire para PSP / PPSSPP
 */

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SCREEN_WIDTH     480
#define SCREEN_HEIGHT    272
#define MAX_PLAYERS      4
#define MAX_BUILDINGS    32

PSP_MODULE_INFO("FlorestaFire", 0, 1, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(18432);

typedef struct { float x, y, z; } Vec3;

typedef enum {
    STATE_MENU,
    STATE_LOBBY,
    STATE_PLAYING
} GameState;

typedef struct {
    Vec3 pos;
    float yaw;
    float hp;
    float speed;
    int ammo;
    int materials;
    int alive;
    int is_bot;
    float bot_timer;
} Player;

typedef struct {
    Vec3 pos;
    int active;
} Building;

typedef struct {
    GameState state;
    Player players[MAX_PLAYERS];
    Building buildings[MAX_BUILDINGS];
    float game_time;
    int local_id;
    int menu_sel;
} Game;

static Game g;
static SceCtrlData pad, pad_old;
static unsigned int __attribute__((aligned(16))) list[262144];
static void *fbp0, *fbp1, *zbp;

int exit_callback(int arg1, int arg2, void *common) {
    sceKernelExitGame();
    return 0;
}

int callback_thread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

void setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

void init_gu(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, 512);
    sceGuDispBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, fbp1, 512);
    sceGuDepthBuffer(zbp, 512);
    sceGuOffset(2048 - (SCREEN_WIDTH/2), 2048 - (SCREEN_HEIGHT/2));
    sceGuViewport(2048, 2048, SCREEN_WIDTH, SCREEN_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CULL_FACE);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void init_game(void) {
    memset(&g, 0, sizeof(Game));
    g.state = STATE_MENU;
    g.local_id = 0;
    g.menu_sel = 0;

    g.players[0].pos = (Vec3){0, 0, 0};
    g.players[0].yaw = 0;
    g.players[0].hp = 100;
    g.players[0].speed = 5.0f;
    g.players[0].ammo = 40;
    g.players[0].materials = 60;
    g.players[0].alive = 1;
    g.players[0].is_bot = 0;

    for (int i = 1; i < MAX_PLAYERS; i++) {
        g.players[i].pos.x = (i - 2) * 20.0f;
        g.players[i].pos.z = (i) * 15.0f;
        g.players[i].hp = 100;
        g.players[i].speed = 4.0f;
        g.players[i].alive = 1;
        g.players[i].is_bot = 1;
        g.players[i].bot_timer = 0;
    }
}

typedef struct {
    unsigned int color;
    short x, y, z;
} Vert2D;

void draw_rect(int x, int y, int w, int h, unsigned int color) {
    Vert2D v[4] = {
        {color, (short)x,     (short)y,     0},
        {color, (short)(x+w), (short)y,     0},
        {color, (short)x,     (short)(y+h), 0},
        {color, (short)(x+w), (short)(y+h), 0}
    };
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, 0, v);
}

void render_menu(void) {
    sceGuClearColor(0xFF0A1A0A);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumOrtho(0, 480, 272, 0, -1, 1);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);

    // Título
    draw_rect(90, 35, 300, 55, 0xFF1A5A1A);
    draw_rect(100, 45, 280, 35, 0xFF2A8A2A);

    // Botão Jogar
    unsigned int c1 = (g.menu_sel == 0) ? 0xFF55CC55 : 0xFF226622;
    draw_rect(140, 120, 200, 45, c1);
    draw_rect(150, 130, 180, 25, 0xFF33AA33);

    // Botão Sair
    unsigned int c2 = (g.menu_sel == 1) ? 0xFFCC5555 : 0xFF662222;
    draw_rect(140, 185, 200, 45, c2);
    draw_rect(150, 195, 180, 25, 0xFFAA3333);

    sceGuEnable(GU_DEPTH_TEST);
}

void render_lobby(void) {
    sceGuClearColor(0xFF0D1F0D);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumOrtho(0, 480, 272, 0, -1, 1);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);

    draw_rect(0, 0, 480, 272, 0xFF0D1F0D);
    draw_rect(50, 25, 380, 220, 0xFF1A3A1A);
    draw_rect(60, 35, 360, 200, 0xFF142814);

    // Personagem simples
    draw_rect(190, 70, 100, 90, 0xFF3366FF);   // corpo
    draw_rect(210, 45, 60, 35, 0xFFFFCC99);    // cabeça
    draw_rect(200, 160, 35, 40, 0xFF2244AA);   // perna
    draw_rect(245, 160, 35, 40, 0xFF2244AA);

    // Botão começar
    draw_rect(150, 215, 180, 35, 0xFF44AA44);
    draw_rect(160, 222, 160, 20, 0xFF66CC66);

    sceGuEnable(GU_DEPTH_TEST);
}

typedef struct {
    unsigned int color;
    float x, y, z;
} Vert3D;

void draw_box(float cx, float cy, float cz, float sx, float sy, float sz, unsigned int color) {
    Vert3D v[4];

    v[0] = (Vert3D){color, cx-sx, cy,    cz+sz};
    v[1] = (Vert3D){color, cx+sx, cy,    cz+sz};
    v[2] = (Vert3D){color, cx-sx, cy+sy, cz+sz};
    v[3] = (Vert3D){color, cx+sx, cy+sy, cz+sz};
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 4, 0, v);

    v[0] = (Vert3D){color, cx+sx, cy,    cz-sz};
    v[1] = (Vert3D){color, cx-sx, cy,    cz-sz};
    v[2] = (Vert3D){color, cx+sx, cy+sy, cz-sz};
    v[3] = (Vert3D){color, cx-sx, cy+sy, cz-sz};
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 4, 0, v);

    v[0] = (Vert3D){color, cx-sx, cy,    cz-sz};
    v[1] = (Vert3D){color, cx-sx, cy,    cz+sz};
    v[2] = (Vert3D){color, cx-sx, cy+sy, cz-sz};
    v[3] = (Vert3D){color, cx-sx, cy+sy, cz+sz};
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 4, 0, v);

    v[0] = (Vert3D){color, cx+sx, cy,    cz+sz};
    v[1] = (Vert3D){color, cx+sx, cy,    cz-sz};
    v[2] = (Vert3D){color, cx+sx, cy+sy, cz+sz};
    v[3] = (Vert3D){color, cx+sx, cy+sy, cz-sz};
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 4, 0, v);

    v[0] = (Vert3D){color, cx-sx, cy+sy, cz+sz};
    v[1] = (Vert3D){color, cx+sx, cy+sy, cz+sz};
    v[2] = (Vert3D){color, cx-sx, cy+sy, cz-sz};
    v[3] = (Vert3D){color, cx+sx, cy+sy, cz-sz};
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 4, 0, v);
}

void render_playing(void) {
    sceGuClearColor(0xFF87AACC); // céu
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    Player *cam = &g.players[0];

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(60.0f, 16.0f/9.0f, 0.5f, 250.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();

    ScePspFVector3 eye = {
        cam->pos.x - sinf(cam->yaw) * 8.5f,
        cam->pos.y + 4.5f,
        cam->pos.z - cosf(cam->yaw) * 8.5f
    };
    ScePspFVector3 center = { cam->pos.x, cam->pos.y + 1.3f, cam->pos.z };
    ScePspFVector3 up = {0, 1, 0};
    sceGumLookAt(&eye, &center, &up);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_DEPTH_TEST);

    // Chão
    Vert3D ground[4] = {
        {0xFF3A8A3A, -90, 0, -90},
        {0xFF3A8A3A,  90, 0, -90},
        {0xFF3A8A3A, -90, 0,  90},
        {0xFF3A8A3A,  90, 0,  90}
    };
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 4, 0, ground);

    // Jogador
    draw_box(cam->pos.x, 0, cam->pos.z, 0.55f, 1.6f, 0.55f, 0xFF3366FF);

    // Bots
    unsigned int cols[3] = {0xFFFF4444, 0xFF44FF44, 0xFFFFFF44};
    for (int i = 1; i < MAX_PLAYERS; i++) {
        if (!g.players[i].alive) continue;
        draw_box(g.players[i].pos.x, 0, g.players[i].pos.z, 0.55f, 1.6f, 0.55f, cols[i-1]);
    }

    // Construções
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!g.buildings[i].active) continue;
        draw_box(g.buildings[i].pos.x, 0, g.buildings[i].pos.z, 1.3f, 2.4f, 0.25f, 0xFF999999);
    }

    // HUD
    sceGuDisable(GU_DEPTH_TEST);
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumOrtho(0, 480, 272, 0, -1, 1);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    draw_rect(10, 10, 130, 18, 0xFF222222);
    int w = (int)(g.players[0].hp * 1.26f);
    if (w < 0) w = 0;
    if (w > 126) w = 126;
    draw_rect(12, 12, w, 14, 0xFF44CC44);
}

void render(void) {
    sceGuStart(GU_DIRECT, list);
    if (g.state == STATE_MENU) render_menu();
    else if (g.state == STATE_LOBBY) render_lobby();
    else render_playing();
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

void update(float dt) {
    sceCtrlReadBufferPositive(&pad, 1);

    if (g.state == STATE_MENU) {
        if ((pad.Buttons & PSP_CTRL_UP) && !(pad_old.Buttons & PSP_CTRL_UP)) g.menu_sel = 0;
        if ((pad.Buttons & PSP_CTRL_DOWN) && !(pad_old.Buttons & PSP_CTRL_DOWN)) g.menu_sel = 1;
        if ((pad.Buttons & PSP_CTRL_CROSS) && !(pad_old.Buttons & PSP_CTRL_CROSS)) {
            if (g.menu_sel == 0) g.state = STATE_LOBBY;
            else sceKernelExitGame();
        }
    }
    else if (g.state == STATE_LOBBY) {
        if ((pad.Buttons & PSP_CTRL_CROSS) && !(pad_old.Buttons & PSP_CTRL_CROSS)) {
            g.state = STATE_PLAYING;
        }
        if ((pad.Buttons & PSP_CTRL_CIRCLE) && !(pad_old.Buttons & PSP_CTRL_CIRCLE)) {
            g.state = STATE_MENU;
        }
    }
    else if (g.state == STATE_PLAYING) {
        Player *p = &g.players[0];
        float mx = 0, mz = 0;
        float ax = (pad.Lx - 128) / 128.0f;
        float ay = (pad.Ly - 128) / 128.0f;
        if (fabsf(ax) > 0.25f) mx = ax;
        if (fabsf(ay) > 0.25f) mz = ay;
        if (pad.Buttons & PSP_CTRL_LEFT)  mx -= 1;
        if (pad.Buttons & PSP_CTRL_RIGHT) mx += 1;
        if (pad.Buttons & PSP_CTRL_UP)    mz -= 1;
        if (pad.Buttons & PSP_CTRL_DOWN)  mz += 1;

        float len = sqrtf(mx*mx + mz*mz);
        if (len > 0.1f) {
            p->pos.x += (mx/len) * p->speed * dt;
            p->pos.z += (mz/len) * p->speed * dt;
        }

        if (pad.Buttons & PSP_CTRL_LTRIGGER) p->yaw -= 2.5f * dt;
        if (pad.Buttons & PSP_CTRL_RTRIGGER) p->yaw += 2.5f * dt;

        if ((pad.Buttons & PSP_CTRL_CIRCLE) && !(pad_old.Buttons & PSP_CTRL_CIRCLE)) {
            if (p->materials >= 10) {
                for (int i = 0; i < MAX_BUILDINGS; i++) {
                    if (!g.buildings[i].active) {
                        g.buildings[i].pos.x = p->pos.x + sinf(p->yaw) * 3.5f;
                        g.buildings[i].pos.z = p->pos.z + cosf(p->yaw) * 3.5f;
                        g.buildings[i].active = 1;
                        p->materials -= 10;
                        break;
                    }
                }
            }
        }

        // Bots andam
        for (int i = 1; i < MAX_PLAYERS; i++) {
            Player *b = &g.players[i];
            if (!b->alive) continue;
            b->bot_timer -= dt;
            if (b->bot_timer <= 0) {
                b->bot_timer = 0.5f + (i * 0.1f);
                float dx = p->pos.x - b->pos.x;
                float dz = p->pos.z - b->pos.z;
                float d = sqrtf(dx*dx + dz*dz);
                if (d > 2.0f && d < 40.0f) {
                    b->pos.x += (dx/d) * b->speed * 0.6f * dt * 8.0f;
                    b->pos.z += (dz/d) * b->speed * 0.6f * dt * 8.0f;
                }
            }
        }
    }

    pad_old = pad;
}

int main(int argc, char *argv[]) {
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);

    fbp0 = (void*)0x0;
    fbp1 = (void*)0x88000;
    zbp  = (void*)0x110000;

    init_gu();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    init_game();

    u64 last;
    sceRtcGetCurrentTick(&last);
    float freq = (float)sceRtcGetTickResolution();

    while (1) {
        u64 now;
        sceRtcGetCurrentTick(&now);
        float dt = (now - last) / freq;
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        update(dt);
        render();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
