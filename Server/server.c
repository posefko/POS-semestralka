#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>

#include "../Common/protocol.h"
#include "game.h"

/* P1: Stavový protokol */
typedef enum {
    STATE_WAITING,
    STATE_RUNNING,
    STATE_PAUSED,
    STATE_GAMEOVER
} ServerState;

/*
 * Context pre receive thread
 */
typedef struct {
    int client_fd;
    GameState* g;
    volatile ServerState state;
    WorldType world;
    GameMode game_mode;
    int time_limit;
    int map_rows;
    int map_cols;
    int has_obstacles;
    volatile int client_disconnected;
} ClientCtx;

/*
 * RECEIVE THREAD
 */
static void* recv_loop(void* arg) {
    ClientCtx* ctx = (ClientCtx*)arg;
    char buf[256];

    while (1) {
        int r = (int)recv(ctx->client_fd, buf, sizeof(buf) - 1, 0);
        
        /* P5: Korektné odpojenie
         * Odpojenie klienta NESMIE okamžite ukončiť hru na serveri.
         * Len si označíme stav a skončí recv thread. Hru ukončí hlavný game loop.
         */
        if (r <= 0) {
            ctx->client_disconnected = 1;
            break;
        }

        buf[r] = '\0';

        /* START - len v stave WAITING */
        /* Format: START <rows> <cols> <WALLS/WRAP> <OBS/NOOBS> <mode> [time] */
        if (strncmp(buf, CMD_START " ", strlen(CMD_START) + 1) == 0) {
            if (ctx->state != STATE_WAITING) continue;
            
            char world_str[32], mode_str[32], obs_str[32];
            int rows = 20, cols = 40;
            int time_limit = 0;
            int parsed = sscanf(buf + strlen(CMD_START) + 1, "%d %d %s %s %s %d", 
                               &rows, &cols, world_str, obs_str, mode_str, &time_limit);
            
            if (parsed >= 5) {
                ctx->map_rows = rows;
                ctx->map_cols = cols;
                ctx->world = (strncmp(world_str, "WALLS", 5) == 0) ? WORLD_WALLS : WORLD_WRAP;
                ctx->has_obstacles = (strncmp(obs_str, "OBS", 3) == 0) ? 1 : 0;
                
                if (strncmp(mode_str, "STANDARD", 8) == 0) {
                    ctx->game_mode = MODE_STANDARD;
                    ctx->time_limit = 0;
                } else {
                    ctx->game_mode = MODE_TIMED;
                    ctx->time_limit = (parsed >= 6) ? time_limit : 60;
                }
                ctx->state = STATE_RUNNING;
            }
        }
        /* MOVE - len v stave RUNNING */
        else if (strncmp(buf, CMD_MOVE " ", strlen(CMD_MOVE) + 1) == 0) {
            if (ctx->state != STATE_RUNNING) continue;
            
            char dir = buf[strlen(CMD_MOVE) + 1];
            pthread_mutex_lock(&ctx->g->mtx);
            game_set_dir(ctx->g, dir);
            pthread_mutex_unlock(&ctx->g->mtx);
        }
        /* PAUSE */
        else if (strncmp(buf, CMD_PAUSE, strlen(CMD_PAUSE)) == 0) {
            if (ctx->state != STATE_RUNNING) continue;
            
            pthread_mutex_lock(&ctx->g->mtx);
            if (!ctx->g->paused) {
                ctx->g->paused = 1;
                ctx->g->pause_start = time(NULL);
                ctx->state = STATE_PAUSED;
            }
            pthread_mutex_unlock(&ctx->g->mtx);
        }
        /* RESUME */
        else if (strncmp(buf, CMD_RESUME, strlen(CMD_RESUME)) == 0) {
            if (ctx->state != STATE_PAUSED) continue;
            
            pthread_mutex_lock(&ctx->g->mtx);
            if (ctx->g->paused) {
                ctx->g->paused = 0;
                ctx->g->total_pause_time += (int)(time(NULL) - ctx->g->pause_start);
                ctx->state = STATE_RUNNING;
            }
            pthread_mutex_unlock(&ctx->g->mtx);
        }
        /* QUIT */
        else if (strncmp(buf, CMD_QUIT, strlen(CMD_QUIT)) == 0) {
            pthread_mutex_lock(&ctx->g->mtx);
            ctx->g->running = 0;
            pthread_mutex_unlock(&ctx->g->mtx);
        }
    }

    return NULL;
}

static void sleep_us(long us)
{
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

/*
 * Vytvorí server socket
 */
static int start_server(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(fd, 1) < 0) {
        perror("listen"); exit(1);
    }

    return fd;
}

int main(void) {
    /* aby sa printf zobrazovali hneď aj pri spúšťaní cez iný proces */
    setvbuf(stdout, NULL, _IONBF, 0);

    int server_fd = start_server();
    printf("Server listening on port %d\n", SERVER_PORT);

    /* P2: Non-blocking accept */
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    int exit_after_game = 0;

    while (1) {
        /* P4: Odmietnutie ďalších klientov */
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            sleep_us(100000);
            continue;
        }

        printf("Client connected\n");

        char out[8192];
        GameState g;
        memset(&g, 0, sizeof(g));  // Inicializuj na 0

        ClientCtx ctx = {
            .client_fd = client_fd,
            .g = &g,
            .state = STATE_WAITING,
            .world = WORLD_WRAP,
            .client_disconnected = 0
        };

        pthread_t th_recv;
        pthread_create(&th_recv, NULL, recv_loop, &ctx);

        /* HLAVNÝ LOOP (čakáme na START alebo disconnect pred START) */
        while (!ctx.client_disconnected) {
            /* Čakáme na START */
            while (ctx.state == STATE_WAITING && !ctx.client_disconnected) {
                sleep_us(10000);
            }

            if (ctx.client_disconnected) {
                break; /* klient odišiel ešte pred START */
            }

            printf("Starting game - World: %s, Mode: %s, Size: %dx%d, Obstacles: %s\n",
                ctx.world == WORLD_WALLS ? "WALLS" : "WRAP",
                ctx.game_mode == MODE_STANDARD ? "STANDARD" : "TIMED",
                ctx.map_rows, ctx.map_cols,
                ctx.has_obstacles ? "YES" : "NO");

            game_init(&g, ctx.world, ctx.game_mode, ctx.time_limit,
                ctx.map_rows, ctx.map_cols, ctx.has_obstacles);

            /*
             * P5: ak klient odíde, server pokračuje.
             * Aby server nebežal donekonečna bez klienta, ukončí hru po timeout-e.
             */
            time_t disconnected_at = 0;
            const int DISCONNECT_TIMEOUT_SEC = 10;

            /* GAME LOOP */
            while (ctx.state == STATE_RUNNING || ctx.state == STATE_PAUSED) {

                /* P5 timeout bez klienta: nespôsobí okamžité ukončenie, ale korektne dobehne */
                if (ctx.client_disconnected) {
                    if (disconnected_at == 0) disconnected_at = time(NULL);
                    if ((int)(time(NULL) - disconnected_at) >= DISCONNECT_TIMEOUT_SEC) {
                        pthread_mutex_lock(&g.mtx);
                        g.running = 0;
                        pthread_mutex_unlock(&g.mtx);
                    }
                }
                else {
                    disconnected_at = 0;
                }

                pthread_mutex_lock(&g.mtx);

                /* Kontrola časového limitu */
                if (g.game_mode == MODE_TIMED && g.time_limit_sec > 0 && ctx.state == STATE_RUNNING) {
                    time_t now = time(NULL);
                    int elapsed = (int)(now - g.start_time) - g.total_pause_time;
                    if (elapsed >= g.time_limit_sec) {
                        g.running = 0;
                        ctx.state = STATE_GAMEOVER;

                        int n = snprintf(out, sizeof(out),
                            "%s\n%s %d\nMODE TIMED\n%s 0s\n%s\n*** CAS VYPRSAL ***\nENDMAP\n",
                            CMD_GAME_OVER, CMD_SCORE, g.score, CMD_TIME, CMD_MAP);

                        pthread_mutex_unlock(&g.mtx);

                        if (!ctx.client_disconnected) {
                            send(client_fd, out, (size_t)n, 0);
                        }
                        break;
                    }
                }

                /* game_step len v RUNNING */
                if (ctx.state == STATE_RUNNING && !g.paused) {
                    game_step(&g);
                }

                /* GAME OVER */
                if (!g.running) {
                    ctx.state = STATE_GAMEOVER;

                    time_t now = time(NULL);
                    int elapsed = (int)(now - g.start_time) - g.total_pause_time;

                    int n = snprintf(out, sizeof(out),
                        "%s\n%s %d\nMODE %s\n%s %ds\n%s\n*** KONIEC HRY ***\nENDMAP\n",
                        CMD_GAME_OVER, CMD_SCORE, g.score,
                        g.game_mode == MODE_STANDARD ? "STANDARD" : "TIMED",
                        CMD_TIME, elapsed, CMD_MAP);

                    pthread_mutex_unlock(&g.mtx);

                    if (!ctx.client_disconnected) {
                        send(client_fd, out, (size_t)n, 0);
                    }
                    break;
                }

                /* SCORE, MODE, TIME */
                int n = snprintf(out, sizeof(out), "%s %d\n", CMD_SCORE, g.score);
                n += snprintf(out + n, sizeof(out) - n, "MODE %s\n",
                    g.game_mode == MODE_STANDARD ? "STANDARD" : "TIMED");

                time_t now = time(NULL);
                int elapsed = g.paused ?
                    (int)(g.pause_start - g.start_time) - g.total_pause_time :
                    (int)(now - g.start_time) - g.total_pause_time;

                if (g.game_mode == MODE_TIMED) {
                    int remaining = g.time_limit_sec - elapsed;
                    if (remaining < 0) remaining = 0;
                    n += snprintf(out + n, sizeof(out) - n, "%s %ds LEFT\n", CMD_TIME, remaining);
                }
                else {
                    n += snprintf(out + n, sizeof(out) - n, "%s %ds\n", CMD_TIME, elapsed);
                }

                n += game_render_map(&g, out + n, (int)sizeof(out) - n);
                pthread_mutex_unlock(&g.mtx);

                if (!ctx.client_disconnected) {
                    send(client_fd, out, (size_t)n, 0);
                }

                sleep_us(150 * 1000);
            }

            /* Po skončení hry: ukonči spojenie, aby recv thread bezpečne skončil */
            shutdown(client_fd, SHUT_RDWR);

            /* Chceme 1 server = 1 hra (P5: server zanikne po skončení hry) */
            exit_after_game = 1;

            /* Pozor: NEROB destroy mutexu, kým beží recv thread.
               Najprv počkaj na recv thread. */
            break; /* skonči HLAVNÝ LOOP pre tohto klienta */
        }

        /* Bezpečné ukončenie vlákna a zdrojov */
        pthread_join(th_recv, NULL);

        /* Mutex destroy až po join */
        pthread_mutex_destroy(&g.mtx);

        close(client_fd);
        printf("Client disconnected\n");

        if (exit_after_game) break;
    }

    close(server_fd);
    return 0;
}
