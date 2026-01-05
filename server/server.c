#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "../common/protocol.h"
#include "game.h"

/*
 * Context pre receive thread
 */
typedef struct {
    int client_fd;
    GameState* g;
    volatile int game_started;  // signalizuje že START príkaz prišiel
    WorldType world;            // typ sveta z START príkazu
} ClientCtx;

/*
 * RECEIVE THREAD
 * - číta príkazy od klienta
 * - START -> nastaví typ sveta a signalizuje štart
 * - PAUSE -> pozastaví hru (game_step sa nevolá)
 * - RESUME -> obnoví hru
 * - MOVE -> zmena smeru
 * - QUIT -> ukončenie hry
 */
static void* recv_loop(void* arg) {
    ClientCtx* ctx = (ClientCtx*)arg;
    char buf[256];

    while (1) {
        int r = (int)recv(ctx->client_fd, buf, sizeof(buf) - 1, 0);
        if (r <= 0) break;

        buf[r] = '\0';

        /* START <world> */
        if (strncmp(buf, CMD_START " ", strlen(CMD_START) + 1) == 0) {
            char* world_type = buf + strlen(CMD_START) + 1;
            if (strncmp(world_type, "WALLS", 5) == 0) {
                ctx->world = WORLD_WALLS;
            } else {
                ctx->world = WORLD_WRAP;
            }
            ctx->game_started = 1;  // signalizuj že môžeme štartovať
        }
        /* MOVE <dir> */
        else if (strncmp(buf, CMD_MOVE " ", strlen(CMD_MOVE) + 1) == 0) {
            char dir = buf[strlen(CMD_MOVE) + 1];

            pthread_mutex_lock(&ctx->g->mtx);
            game_set_dir(ctx->g, dir);
            pthread_mutex_unlock(&ctx->g->mtx);
        }
        /* PAUSE */
        else if (strncmp(buf, CMD_PAUSE, strlen(CMD_PAUSE)) == 0) {
            pthread_mutex_lock(&ctx->g->mtx);
            if (!ctx->g->paused) {
                ctx->g->paused = 1;
                ctx->g->pause_start = time(NULL);
            }
            pthread_mutex_unlock(&ctx->g->mtx);
        }
        /* RESUME */
        else if (strncmp(buf, CMD_RESUME, strlen(CMD_RESUME)) == 0) {
            pthread_mutex_lock(&ctx->g->mtx);
            if (ctx->g->paused) {
                ctx->g->paused = 0;
                ctx->g->total_pause_time += (int)(time(NULL) - ctx->g->pause_start);
            }
            pthread_mutex_unlock(&ctx->g->mtx);
        }
        /* QUIT */
        else if (strncmp(buf, CMD_QUIT, strlen(CMD_QUIT)) == 0) {
            pthread_mutex_lock(&ctx->g->mtx);
            ctx->g->running = 0;
            pthread_mutex_unlock(&ctx->g->mtx);
            // Neprerušujeme thread, pokračujeme v čítaní (čakáme na ďalší START)
        }
    }

    return NULL;
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
    int server_fd = start_server();
    printf("Server listening on port %d\n", SERVER_PORT);

    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) { perror("accept"); return 1; }
    printf("Client connected\n");

    char out[8192];
    GameState g;
    ClientCtx ctx = { .client_fd = client_fd, .g = &g, .game_started = 0, .world = WORLD_WRAP };

    /* Spustenie recv_loop threadu */
    pthread_t th_recv;
    pthread_create(&th_recv, NULL, recv_loop, &ctx);

    /* HLAVNÝ LOOP - podporuje viacero hier */
    while (1) {
        /* Čakáme na START príkaz (signalizácia z recv_loop) */
        while (!ctx.game_started) {
            usleep(10000); // 10ms
        }

        printf("Starting game with %s\n", ctx.world == WORLD_WALLS ? "WALLS" : "WRAP");

        /* Inicializácia hry s vybraným typom sveta */
        game_init(&g, ctx.world);

        /* GAME LOOP */
        while (1) {
            pthread_mutex_lock(&g.mtx);

            /* Kontrola časového limitu */
            if (g.time_limit_sec > 0) {
                time_t now = time(NULL);
                int elapsed = (int)(now - g.start_time) - g.total_pause_time;
                if (elapsed >= g.time_limit_sec) {
                    g.running = 0;
                    int n = snprintf(out, sizeof(out),
                        "%s\n%s\nTIME UP\nENDMAP\n",
                        CMD_GAME_OVER,
                        CMD_MAP
                    );
                    pthread_mutex_unlock(&g.mtx);
                    send(client_fd, out, (size_t)n, 0);
                    break;
                }
            }

            game_step(&g);

            /* GAME OVER */
            if (!g.running) {
                int n = snprintf(out, sizeof(out),
                    "%s\n%s\nGAME OVER\nENDMAP\n",
                    CMD_GAME_OVER,
                    CMD_MAP
                );
                pthread_mutex_unlock(&g.mtx);
                send(client_fd, out, (size_t)n, 0);
                break;
            }

            /* SCORE */
            int n = snprintf(out, sizeof(out),
                "%s %d\n",
                CMD_SCORE,
                g.score
            );

            /* TIME - uplynulý čas bez pauzy */
            time_t now = time(NULL);
            int elapsed;
            if (g.paused) {
                // Počas pauzy zobraz čas v momente pozastavenia
                elapsed = (int)(g.pause_start - g.start_time) - g.total_pause_time;
            } else {
                // Bežiaci čas
                elapsed = (int)(now - g.start_time) - g.total_pause_time;
            }
            n += snprintf(out + n, sizeof(out) - n,
                "%s %ds\n",
                CMD_TIME,
                elapsed
            );

            /* MAP */
            n += game_render_map(&g, out + n, (int)sizeof(out) - n);

            pthread_mutex_unlock(&g.mtx);

            send(client_fd, out, (size_t)n, 0);
            usleep(150 * 1000);
        }

        pthread_mutex_destroy(&g.mtx);
        ctx.game_started = 0;  // reset pre ďalšiu hru
        printf("Game ended, waiting for next START...\n");
    }

    pthread_join(th_recv, NULL);
    close(client_fd);
    close(server_fd);
    return 0;
}
