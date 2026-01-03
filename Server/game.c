#include "game.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

/*
  Pomocná funkcia: je (x,y) vnútri hraníc?
*/
static int in_bounds(int x, int y) {
    return x >= 0 && x < COLS && y >= 0 && y < ROWS;
}

/*
  Vyèistí board na medzery.
  (board je len "canvas", kreslíme doò pred odoslaním klientovi)
*/
static void clear_board(GameState* g) {
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            g->board[y][x] = ' ';
}

/*
  Nakreslí okrajové steny (#).
  V tomto kroku: náraz do steny = koniec hry.
*/
static void draw_walls(GameState* g) {
    for (int x = 0; x < COLS; x++) {
        g->board[0][x] = '#';
        g->board[ROWS - 1][x] = '#';
    }
    for (int y = 0; y < ROWS; y++) {
        g->board[y][0] = '#';
        g->board[y][COLS - 1] = '#';
    }
}

/*
  Zistí, èi had zaberá políèko (x,y).
  Použijeme pre:
  - self-collision
  - aby ovocie nespawnlo na hade
*/
static int snake_occupies(const GameState* g, int x, int y) {
    for (int i = 0; i < g->snake.len; i++) {
        if (g->snake.parts[i].x == x && g->snake.parts[i].y == y) return 1;
    }
    return 0;
}

/*
  Spawn ovocia: náhodná pozícia "vnútri" (nie na stene) a nie na hade.
*/
static void spawn_fruit(GameState* g) {
    int fx, fy;
    do {
        fx = 1 + rand() % (COLS - 2);
        fy = 1 + rand() % (ROWS - 2);
    } while (snake_occupies(g, fx, fy));
    g->fruit.x = fx;
    g->fruit.y = fy;
}

void game_init(GameState* g) {
    // vynulujeme celý stav
    memset(g, 0, sizeof(*g));

    // init mutexu (server bude ma 2 vlákna: game loop + recv loop)
    pthread_mutex_init(&g->mtx, NULL);

    srand((unsigned)time(NULL));
    g->running = 1;

    // inicializácia hada (dåžka 3, smer doprava)
    g->snake.alive = 1;
    g->snake.len = 3;
    g->snake.dir = 'd';

    // had do stredu mapy
    int sx = COLS / 2;
    int sy = ROWS / 2;
    g->snake.parts[0] = (Pos){ sx, sy };       // hlava
    g->snake.parts[1] = (Pos){ sx - 1, sy };   // telo
    g->snake.parts[2] = (Pos){ sx - 2, sy };   // telo

    // ovocie
    spawn_fruit(g);
}

/*
  Nastaví smer pohybu z inputu.
  Pridávame ochranu proti otoèeniu o 180 stupòov (aby sa had nezabil hneï).
*/
void game_set_dir(GameState* g, char dir) {
    char cur = g->snake.dir;

    // zakáž protismer
    if ((cur == 'w' && dir == 's') || (cur == 's' && dir == 'w') ||
        (cur == 'a' && dir == 'd') || (cur == 'd' && dir == 'a')) return;

    // povo¾ len w/a/s/d
    if (dir == 'w' || dir == 'a' || dir == 's' || dir == 'd')
        g->snake.dir = dir;
}

/*
  Posun o jeden tick:
  - vypoèíta novú hlavu
  - skontroluje stenu a self-collision
  - ak zje ovocie -> rast + nový fruit
*/
void game_step(GameState* g) {
    if (!g->running || !g->snake.alive) return;

    Pos head = g->snake.parts[0];
    Pos nh = head;

    switch (g->snake.dir) {
    case 'w': nh.y--; break;
    case 's': nh.y++; break;
    case 'a': nh.x--; break;
    case 'd': nh.x++; break;
    default: break;
    }

    // náraz do steny = koniec
    if (!in_bounds(nh.x, nh.y) || nh.x == 0 || nh.x == COLS - 1 || nh.y == 0 || nh.y == ROWS - 1) {
        g->snake.alive = 0;
        g->running = 0;
        return;
    }

    // náraz do seba = koniec
    if (snake_occupies(g, nh.x, nh.y)) {
        g->snake.alive = 0;
        g->running = 0;
        return;
    }

    // zjedol ovocie?
    int ate = (nh.x == g->fruit.x && nh.y == g->fruit.y);

    // ak zje, zväèšíme dåžku (max MAX_SNAKE)
    if (ate&& g->snake.len < MAX_SNAKE) g->snake.len++;

    // posun segmentov: od konca k hlave
    for (int i = g->snake.len - 1; i > 0; i--) {
        g->snake.parts[i] = g->snake.parts[i - 1];
    }
    g->snake.parts[0] = nh;

    // po zjedení spawnni nové ovocie
    if (ate) spawn_fruit(g);
}

/*
  Vytvorí ASCII mapu do out bufferu.
  Klient to nebude parsova "inteligentne" – len to vypíše.
*/
int game_render_map(GameState* g, char* out, int out_cap) {
    clear_board(g);
    draw_walls(g);

    // ovocie
    g->board[g->fruit.y][g->fruit.x] = 'o';

    // had: hlava '@', telo '*'
    for (int i = 0; i < g->snake.len; i++) {
        int x = g->snake.parts[i].x;
        int y = g->snake.parts[i].y;
        g->board[y][x] = (i == 0) ? '@' : '*';
    }

    // zloženie textu do out
    int n = 0;
    n += snprintf(out + n, out_cap - n, "MAP\n");

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (n >= out_cap - 2) break; // ochrana bufferu
            out[n++] = g->board[y][x];
        }
        if (n < out_cap - 1) out[n++] = '\n';
    }

    n += snprintf(out + n, out_cap - n, "ENDMAP\n");
    return n;
}
