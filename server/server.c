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
  Context pre receive thread:
  - client_fd: socket na klienta
  - g: pointer na zdie¾anı hernı stav
*/
typedef struct {
    int client_fd;
    GameState* g;
} ClientCtx;

/*
  RECEIVE thread:
  - èíta správy od klienta (MOVE/QUIT)
  - pri MOVE nastaví smer v GameState
  - pri QUIT ukonèí hru
*/
static void* recv_loop(void* arg) {
    ClientCtx* ctx = (ClientCtx*)arg;
    char buf[256];

    while (1) {
        int r = (int)recv(ctx->client_fd, buf, sizeof(buf) - 1, 0);
        if (r <= 0) break;     // klient sa odpojil
        buf[r] = '\0';

        // Oèakávame napr. "MOVE w\n"
        if (strncmp(buf, "MOVE ", 5) == 0) {
            char dir = buf[5];

            // zamkneme mutex, lebo game loop môe práve robi tick
            pthread_mutex_lock(&ctx->g->mtx);
            game_set_dir(ctx->g, dir);
            pthread_mutex_unlock(&ctx->g->mtx);

        }
        else if (strncmp(buf, "QUIT", 4) == 0) {
            // korektné ukonèenie hry
            pthread_mutex_lock(&ctx->g->mtx);
            ctx->g->running = 0;
            pthread_mutex_unlock(&ctx->g->mtx);
            break;
        }
    }

    // zavri socket klienta (server potom skonèí v main loop)
    close(ctx->client_fd);
    return NULL;
}

/*
  Vytvorí server socket a zaène poèúva.
*/
static int start_server(void) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); exit(1); }

    // aby port nezostal "busy" pri reštarte
    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); exit(1); }
    if (listen(server_fd, 1) < 0) { perror("listen"); exit(1); }

    return server_fd;
}

int main(void) {
    int server_fd = start_server();
    printf("Server listening on port %d\n", SERVER_PORT);

    // Krok 2: stále len 1 klient (najjednoduchšie)
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) { perror("accept"); return 1; }
    printf("Client connected\n");

    // inicializuj hernı stav
    GameState g;
    game_init(&g);

    // spusti receive thread (input od klienta)
    ClientCtx ctx = { .client_fd = client_fd, .g = &g };
    pthread_t th_recv;
    pthread_create(&th_recv, NULL, recv_loop, &ctx);

    // buffer na ASCII mapu
    char out[8192];

    /*
      GAME LOOP (v main threade):
      - kadıch ~150ms spraví tick
      - vyrenderuje mapu
      - pošle ju klientovi
    */
    while (1) {
        pthread_mutex_lock(&g.mtx);

        if (!g.running) {
            // jednoduché oznámenie konca
            int n = snprintf(out, sizeof(out), "MAP\nGAME OVER\nENDMAP\n");
            pthread_mutex_unlock(&g.mtx);

            send(client_fd, out, (size_t)n, 0);
            break;
        }

        // posuò hru a vyrenderuj mapu
        game_step(&g);
        int n = game_render_map(&g, out, (int)sizeof(out));

        pthread_mutex_unlock(&g.mtx);

        // pošli mapu klientovi
        send(client_fd, out, (size_t)n, 0);

        // malé oneskorenie (plynulos)
        usleep(150 * 1000);
    }

    pthread_join(th_recv, NULL);
    close(server_fd);
    return 0;
}
