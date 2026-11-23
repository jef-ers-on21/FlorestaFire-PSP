/**
 * Floresta Fire - main.c
 * Battle Royale simplificado para PSP / PPSSPP
 * Estilo Free Fire (tiro + construção + mapa florestal + 4 jogadores/bots)
 *
 * Compilar com PSPSDK
 * Otimizado para performance baixa do PSP
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

// ============================================================
// Configurações do jogo
// ============================================================
#define SCREEN_WIDTH     480
#define SCREEN_HEIGHT    272
#define MAX_PLAYERS      4
#define MAX_BUILDINGS    64
#define MAX_BULLETS      32
#define TICK_RATE        30.0f          // alvo de 30 FPS

PSP_MODULE_INFO("FlorestaFire", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(20480);                // ~20 MB de heap (seguro)

// ============================================================
// Tipos básicos
// ============================================================
typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    float x, y;
} Vec2;

// ============================================================
// Estados do jogo
// ============================================================
typedef enum {
    GAME_STATE_MENU,
    GAME_STATE_PLAYING,
    GAME_STATE_PAUSED,
    GAME_STATE_GAMEOVER
} GameState;

typedef enum {
    BOT_IDLE,
    BOT_LOOT,
    BOT_ATTACK,
    BOT_BUILD,
    BOT_FLEE,
    BOT_GOTO_ZONE
} BotState;

// ============================================================
// Estruturas principais
// ============================================================
typedef struct {
    Vec3 pos;
    Vec3 rot;               // pitch, yaw
    float hp;
    float speed;
    int weapon_id;          // 0=pistola, 1=smg, 2=rifle, 3=shotgun
    int ammo;
    int materials;          // madeira para construir
    int is_alive;
    int is_bot;
    BotState bot_state;
    float bot_timer;
    int target_id;          // quem o bot está mirando
} Player;

typedef struct {
    Vec3 pos;
    Vec3 dir;
    float speed;
    float life;
    int owner_id;
    int active;
} Bullet;

typedef struct {
    Vec3 pos;
    int type;               // 0=parede, 1=rampa, 2=plataforma
    float hp;
    int active;
} Building;

typedef struct {
    GameState state;
    Player players[MAX_PLAYERS];
    Bullet bullets[MAX_BULLETS];
    Building buildings[MAX_BUILDINGS];
    float zone_radius;
    Vec3 zone_center;
    float game_time;
    int local_player_id;
    int building_count;
} Game;

// ============================================================
// Variáveis globais
// ============================================================
static Game g_game;
static SceCtrlData g_pad;
static SceCtrlData g_pad_old;
static unsigned int __attribute__((aligned(16))) g_list[262144]; // display list

// ============================================================
// Protótipos
// ============================================================
void init_game(void);
void update_game(float dt);
void render_game(void);
void update_bots(float dt);
void update_player_input(float dt);
void update_bullets(float dt);
void update_zone(float dt);
void spawn_bullet(int owner, Vec3 pos, Vec3 dir);
void try_build(int player_id);
int  get_free_bullet(void);
int  get_free_building(void);

// ============================================================
// Callbacks de saída
// ============================================================
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

int setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
    return thid;
}

// ============================================================
// Inicialização do GU (gráficos)
// ============================================================
static void *fbp0 = 0;
static void *fbp1 = 0;
static void *zbp  = 0;

void init_gu(void) {
    sceGuInit();

    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, 512);
    sceGuDispBuffer(SCREEN_WIDTH, SCREEN_HEIGHT, fbp1, 512);
    sceGuDepthBuffer(zbp, 512);

    sceGuOffset(2048 - (SCREEN_WIDTH / 2), 2048 - (SCREEN_HEIGHT / 2));
    sceGuViewport(2048, 2048, SCREEN_WIDTH, SCREEN_HEIGHT);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_LIGHTING);          // luz simples (otimizado)
    sceGuEnable(GU_LIGHT0);

    // Luz direcional básica (barata)
    ScePspFVector3 light_dir = { 0.4f, 1.0f, 0.4f };
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE_AND_SPECULAR, &light_dir);
    sceGuLightColor(0, GU_DIFFUSE, 0xFFCCCCAA);
    sceGuLightColor(0, GU_SPECULAR, 0xFF444444);
    sceGuAmbient(0xFF222222);

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void end_gu(void) {
    sceGuTerm();
}

// ============================================================
// Inicialização do jogo
// ============================================================
void init_game(void) {
    memset(&g_game, 0, sizeof(Game));

    g_game.state = GAME_STATE_PLAYING;
    g_game.local_player_id = 0;
    g_game.zone_radius = 180.0f;
    g_game.zone_center = (Vec3){0.0f, 0.0f, 0.0f};
    g_game.game_time = 0.0f;
    g_game.building_count = 0;

    // Player local (humano)
    g_game.players[0].pos = (Vec3){0.0f, 0.0f, 0.0f};
    g_game.players[0].hp = 100.0f;
    g_game.players[0].speed = 4.5f;
    g_game.players[0].weapon_id = 0;
    g_game.players[0].ammo = 30;
    g_game.players[0].materials = 50;
    g_game.players[0].is_alive = 1;
    g_game.players[0].is_bot = 0;

    // 3 Bots
    for (int i = 1; i < MAX_PLAYERS; i++) {
        g_game.players[i].pos.x = (float)((i * 40) - 60);
        g_game.players[i].pos.z = (float)((i * 30) - 45);
        g_game.players[i].pos.y = 0.0f;
        g_game.players[i].hp = 100.0f;
        g_game.players[i].speed = 4.0f + (i * 0.2f);
        g_game.players[i].weapon_id = i % 4;
        g_game.players[i].ammo = 40;
        g_game.players[i].materials = 40;
        g_game.players[i].is_alive = 1;
        g_game.players[i].is_bot = 1;
        g_game.players[i].bot_state = BOT_IDLE;
        g_game.players[i].bot_timer = 0.0f;
        g_game.players[i].target_id = -1;
    }
}

// ============================================================
// Input do jogador local
// ============================================================
void update_player_input(float dt) {
    Player *p = &g_game.players[g_game.local_player_id];
    if (!p->is_alive) return;

    sceCtrlReadBufferPositive(&g_pad, 1);

    float move_x = 0.0f;
    float move_z = 0.0f;

    // Analógico
    float ax = (g_pad.Lx - 128) / 128.0f;
    float ay = (g_pad.Ly - 128) / 128.0f;

    if (fabsf(ax) > 0.2f || fabsf(ay) > 0.2f) {
        move_x = ax;
        move_z = ay;
    }

    // Botões digitais (fallback)
    if (g_pad.Buttons & PSP_CTRL_UP)    move_z -= 1.0f;
    if (g_pad.Buttons & PSP_CTRL_DOWN)  move_z += 1.0f;
    if (g_pad.Buttons & PSP_CTRL_LEFT)  move_x -= 1.0f;
    if (g_pad.Buttons & PSP_CTRL_RIGHT) move_x += 1.0f;

    // Normalizar movimento
    float len = sqrtf(move_x * move_x + move_z * move_z);
    if (len > 0.01f) {
        move_x /= len;
        move_z /= len;
        p->pos.x += move_x * p->speed * dt;
        p->pos.z += move_z * p->speed * dt;
    }

    // Mirar (R + analógico direito simulado com L/R ou só yaw simples)
    if (g_pad.Buttons & PSP_CTRL_LTRIGGER) {
        p->rot.y -= 1.8f * dt;          // yaw
    }
    if (g_pad.Buttons & PSP_CTRL_RTRIGGER) {
        p->rot.y += 1.8f * dt;
    }

    // Atirar (X)
    if ((g_pad.Buttons & PSP_CTRL_CROSS) && !(g_pad_old.Buttons & PSP_CTRL_CROSS)) {
        if (p->ammo > 0) {
            Vec3 dir = {
                sinf(p->rot.y),
                0.0f,
                cosf(p->rot.y)
            };
            spawn_bullet(g_game.local_player_id, p->pos, dir);
            p->ammo--;
        }
    }

    // Construir (O / Circle)
    if ((g_pad.Buttons & PSP_CTRL_CIRCLE) && !(g_pad_old.Buttons & PSP_CTRL_CIRCLE)) {
        try_build(g_game.local_player_id);
    }

    // Trocar arma (Square)
    if ((g_pad.Buttons & PSP_CTRL_SQUARE) && !(g_pad_old.Buttons & PSP_CTRL_SQUARE)) {
        p->weapon_id = (p->weapon_id + 1) % 4;
    }

    g_pad_old = g_pad;
}

// ============================================================
// IA dos Bots (FSM leve)
// ============================================================
void update_bots(float dt) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        Player *bot = &g_game.players[i];
        if (!bot->is_alive || !bot->is_bot) continue;

        bot->bot_timer -= dt;
        if (bot->bot_timer > 0.0f) continue;     // só decide a cada X segundos

        bot->bot_timer = 0.35f + (i * 0.05f);    // desincronizar bots (otimização)

        // 1. HP baixo → fugir
        if (bot->hp < 30.0f) {
            bot->bot_state = BOT_FLEE;
        }
        // 2. Procurar alvo próximo
        else {
            int closest = -1;
            float closest_dist = 45.0f;          // alcance de visão limitado

            for (int j = 0; j < MAX_PLAYERS; j++) {
                if (j == i || !g_game.players[j].is_alive) continue;
                float dx = g_game.players[j].pos.x - bot->pos.x;
                float dz = g_game.players[j].pos.z - bot->pos.z;
                float dist = sqrtf(dx*dx + dz*dz);
                if (dist < closest_dist) {
                    closest_dist = dist;
                    closest = j;
                }
            }

            if (closest >= 0) {
                bot->target_id = closest;
                bot->bot_state = BOT_ATTACK;
            } else if (bot->materials < 15) {
                bot->bot_state = BOT_LOOT;
            } else {
                // Verificar se está fora da zona
                float dx = bot->pos.x - g_game.zone_center.x;
                float dz = bot->pos.z - g_game.zone_center.z;
                float dist_zone = sqrtf(dx*dx + dz*dz);
                if (dist_zone > g_game.zone_radius - 15.0f) {
                    bot->bot_state = BOT_GOTO_ZONE;
                } else {
                    bot->bot_state = BOT_IDLE;
                }
            }
        }

        // Executar estado
        switch (bot->bot_state) {
            case BOT_ATTACK: {
                if (bot->target_id < 0) break;
                Player *target = &g_game.players[bot->target_id];
                float dx = target->pos.x - bot->pos.x;
                float dz = target->pos.z - bot->pos.z;
                float dist = sqrtf(dx*dx + dz*dz);

                // Olhar para o alvo
                bot->rot.y = atan2f(dx, dz);

                if (dist > 12.0f) {
                    // Aproximar
                    bot->pos.x += (dx / dist) * bot->speed * dt * 0.9f;
                    bot->pos.z += (dz / dist) * bot->speed * dt * 0.9f;
                } else if (bot->ammo > 0 && (rand() % 100) < 40) {
                    // Atirar (com imprecisão)
                    Vec3 dir = { sinf(bot->rot.y + ((rand()%100)-50)*0.01f),
                                 0.0f,
                                 cosf(bot->rot.y + ((rand()%100)-50)*0.01f) };
                    spawn_bullet(i, bot->pos, dir);
                    bot->ammo--;
                }

                // Às vezes constrói cobertura
                if (dist < 20.0f && bot->materials >= 10 && (rand() % 100) < 8) {
                    try_build(i);
                }
                break;
            }

            case BOT_FLEE: {
                // Corre na direção oposta ao centro da zona ou aleatória
                float angle = bot->rot.y + 3.14f;
                bot->pos.x += sinf(angle) * bot->speed * 1.2f * dt;
                bot->pos.z += cosf(angle) * bot->speed * 1.2f * dt;
                break;
            }

            case BOT_GOTO_ZONE: {
                float dx = g_game.zone_center.x - bot->pos.x;
                float dz = g_game.zone_center.z - bot->pos.z;
                float dist = sqrtf(dx*dx + dz*dz);
                if (dist > 1.0f) {
                    bot->pos.x += (dx / dist) * bot->speed * dt;
                    bot->pos.z += (dz / dist) * bot->speed * dt;
                }
                break;
            }

            case BOT_LOOT:
            case BOT_IDLE:
            default: {
                // Movimento aleatório leve
                if ((rand() % 100) < 30) {
                    bot->rot.y += ((rand() % 100) - 50) * 0.02f;
                }
                bot->pos.x += sinf(bot->rot.y) * bot->speed * 0.5f * dt;
                bot->pos.z += cosf(bot->rot.y) * bot->speed * 0.5f * dt;
                break;
            }
        }
    }
}

// ============================================================
// Sistema de tiros
// ============================================================
int get_free_bullet(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!g_game.bullets[i].active) return i;
    }
    return -1;
}

void spawn_bullet(int owner, Vec3 pos, Vec3 dir) {
    int id = get_free_bullet();
    if (id < 0) return;

    Bullet *b = &g_game.bullets[id];
    b->pos = pos;
    b->pos.y += 1.0f;               // altura do peito
    b->dir = dir;
    b->speed = 45.0f;
    b->life = 1.5f;
    b->owner_id = owner;
    b->active = 1;
}

void update_bullets(float dt) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &g_game.bullets[i];
        if (!b->active) continue;

        b->pos.x += b->dir.x * b->speed * dt;
        b->pos.z += b->dir.z * b->speed * dt;
        b->life -= dt;

        if (b->life <= 0.0f) {
            b->active = 0;
            continue;
        }

        // Colisão simples com jogadores
        for (int j = 0; j < MAX_PLAYERS; j++) {
            if (j == b->owner_id || !g_game.players[j].is_alive) continue;

            float dx = g_game.players[j].pos.x - b->pos.x;
            float dz = g_game.players[j].pos.z - b->pos.z;
            float dist = sqrtf(dx*dx + dz*dz);

            if (dist < 1.4f) {
                // Acerto
                float damage = 18.0f + (g_game.players[b->owner_id].weapon_id * 6.0f);
                g_game.players[j].hp -= damage;
                b->active = 0;

                if (g_game.players[j].hp <= 0.0f) {
                    g_game.players[j].hp = 0.0f;
                    g_game.players[j].is_alive = 0;
                }
                break;
            }
        }
    }
}

// ============================================================
// Construção
// ============================================================
int get_free_building(void) {
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!g_game.buildings[i].active) return i;
    }
    return -1;
}

void try_build(int player_id) {
    Player *p = &g_game.players[player_id];
    if (p->materials < 10) return;

    int id = get_free_building();
    if (id < 0) return;

    Building *b = &g_game.buildings[id];
    b->pos = p->pos;
    // Coloca a construção um pouco à frente do jogador
    b->pos.x += sinf(p->rot.y) * 3.0f;
    b->pos.z += cosf(p->rot.y) * 3.0f;
    b->type = 0;                // parede por enquanto
    b->hp = 80.0f;
    b->active = 1;

    p->materials -= 10;
    g_game.building_count++;
}

// ============================================================
// Zona segura
// ============================================================
void update_zone(float dt) {
    g_game.game_time += dt;

    // Fecha a zona lentamente
    if (g_game.game_time > 30.0f) {
        g_game.zone_radius -= 2.5f * dt;
        if (g_game.zone_radius < 25.0f) g_game.zone_radius = 25.0f;
    }

    // Dano fora da zona
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!g_game.players[i].is_alive) continue;
        float dx = g_game.players[i].pos.x - g_game.zone_center.x;
        float dz = g_game.players[i].pos.z - g_game.zone_center.z;
        float dist = sqrtf(dx*dx + dz*dz);
        if (dist > g_game.zone_radius) {
            g_game.players[i].hp -= 8.0f * dt;
            if (g_game.players[i].hp <= 0.0f) {
                g_game.players[i].hp = 0.0f;
                g_game.players[i].is_alive = 0;
            }
        }
    }
}

// ============================================================
// Update principal
// ============================================================
void update_game(float dt) {
    if (g_game.state != GAME_STATE_PLAYING) return;

    update_player_input(dt);
    update_bots(dt);
    update_bullets(dt);
    update_zone(dt);

    // Verificar fim de jogo
    int alive = 0;
    int last_alive = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_game.players[i].is_alive) {
            alive++;
            last_alive = i;
        }
    }
    if (alive <= 1) {
        g_game.state = GAME_STATE_GAMEOVER;
        // last_alive é o vencedor (se >= 0)
    }
}

// ============================================================
// Render (esqueleto otimizado)
// ============================================================
void render_game(void) {
    sceGuStart(GU_DIRECT, g_list);

    // Clear
    sceGuClearColor(0xFF1A2F1A);        // verde floresta escuro
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    // Câmera simples (terceira pessoa atrás do jogador)
    Player *cam = &g_game.players[g_game.local_player_id];

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(60.0f, 16.0f/9.0f, 0.8f, 300.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();

    // Câmera atrás e um pouco acima
    ScePspFVector3 eye = {
        cam->pos.x - sinf(cam->rot.y) * 8.0f,
        cam->pos.y + 4.5f,
        cam->pos.z - cosf(cam->rot.y) * 8.0f
    };
    ScePspFVector3 center = {
        cam->pos.x,
        cam->pos.y + 1.5f,
        cam->pos.z
    };
    ScePspFVector3 up = { 0.0f, 1.0f, 0.0f };
    sceGumLookAt(&eye, &center, &up);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    // TODO: aqui você desenha o terreno, árvores (billboards),
    // jogadores (modelos low-poly), construções e balas.
    // Por enquanto só limpa a tela com cor de floresta.

    // Exemplo mínimo de debug: desenhar um quad no chão (placeholder)
    // (em produção você usa vertex arrays + display lists)

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

// ============================================================
// main
// ============================================================
int main(int argc, char *argv[]) {
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);   // máximo estável

    // Buffers de frame (alinhados)
    fbp0 = (void*)(0x0);                       // VRAM offset 0
    fbp1 = (void*)(0x88000);                   // segundo buffer
    zbp  = (void*)(0x110000);                  // depth

    init_gu();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    init_game();

    u64 last_tick;
    sceRtcGetCurrentTick(&last_tick);
    float tick_frequency = (float)sceRtcGetTickResolution();

    while (1) {
        u64 now;
        sceRtcGetCurrentTick(&now);
        float dt = (now - last_tick) / tick_frequency;
        last_tick = now;

        // Limitar dt para evitar saltos grandes
        if (dt > 0.1f) dt = 0.1f;

        update_game(dt);
        render_game();

        // Saída
        if (g_pad.Buttons & PSP_CTRL_START) {
            // Em produção: pausar ou voltar ao menu
            // Por enquanto deixa rodando
        }
    }

    end_gu();
    sceKernelExitGame();
    return 0;
}
