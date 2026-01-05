#include "game.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

/*
  Pomocn� funkcia: je (x,y) vn�tri hran�c?
*/
static int in_bounds(int x, int y) {
    return x >= 0 && x < COLS && y >= 0 && y < ROWS;
}

/*
  Vy�ist� board na medzery.
  (board je len "canvas", kresl�me do� pred odoslan�m klientovi)
*/
static void clear_board(GameState* g) {
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            g->board[y][x] = ' ';
}

/*
  Nakresl� okrajov� steny s požadovanými znakmi.
*/
static void draw_walls(GameState* g, char horiz, char vert) {
    for (int x = 0; x < COLS; x++) {
        g->board[0][x] = horiz;
        g->board[ROWS - 1][x] = horiz;
    }
    for (int y = 0; y < ROWS; y++) {
        g->board[y][0] = vert;
        g->board[y][COLS - 1] = vert;
    }
}

/*
  Zist�, �i had zaber� pol��ko (x,y).
  Pou�ijeme pre:
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
  Spawn ovocia: n�hodn� poz�cia "vn�tri" (nie na stene) a nie na hade.
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

void game_init(GameState* g, WorldType world) {
    // vynulujeme cel� stav
    memset(g, 0, sizeof(*g));

    // init mutexu (server bude ma� 2 vl�kna: game loop + recv loop)
    pthread_mutex_init(&g->mtx, NULL);

    srand((unsigned)time(NULL));
    g->running = 1;
    g->score = 0;
    g->world = world;  // nastav typ sveta pod�a po�iadavky klienta
    g->paused = 0;
    g->time_limit_sec = 0;  // 0 = nekone�n� re�im (mo�no neskôr parameterizova�)
    g->start_time = time(NULL);
    g->pause_start = 0;
    g->total_pause_time = 0;

    // inicializ�cia hada (d�ka 3, smer doprava)
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
  Nastav� smer pohybu z inputu.
  Prid�vame ochranu proti oto�eniu o 180 stup�ov (aby sa had nezabil hne�).
*/
void game_set_dir(GameState* g, char dir) {
    char cur = g->snake.dir;

    // zak� protismer
    if ((cur == 'w' && dir == 's') || (cur == 's' && dir == 'w') ||
        (cur == 'a' && dir == 'd') || (cur == 'd' && dir == 'a')) return;

    // povo� len w/a/s/d
    if (dir == 'w' || dir == 'a' || dir == 's' || dir == 'd')
        g->snake.dir = dir;
}

/*
  Posun o jeden tick:
  - vypo��ta nov� hlavu
  - skontroluje stenu a self-collision
  - ak zje ovocie -> rast + nov� fruit
*/
void game_step(GameState* g) {
    if (!g->running || !g->snake.alive || g->paused) return;  // nepohybujeme hadom ak je pauza

    Pos head = g->snake.parts[0];
    Pos nh = head;

    switch (g->snake.dir) {
    case 'w': nh.y--; break;
    case 's': nh.y++; break;
    case 'a': nh.x--; break;
    case 'd': nh.x++; break;
    default: break;
    }

    // WORLD_WRAP: wrap-around na opačný okraj
    if (g->world == WORLD_WRAP) {
        if (nh.x <= 0) nh.x = COLS - 2;
        else if (nh.x >= COLS - 1) nh.x = 1;

        if (nh.y <= 0) nh.y = ROWS - 2;
        else if (nh.y >= ROWS - 1) nh.y = 1;
    }
    // WORLD_WALLS: n�raz do steny = koniec
    else if (g->world == WORLD_WALLS) {
        if (!in_bounds(nh.x, nh.y) || nh.x == 0 || nh.x == COLS - 1 || nh.y == 0 || nh.y == ROWS - 1) {
            g->snake.alive = 0;
            g->running = 0;
            return;
        }
    }

    // n�raz do seba = koniec
    if (snake_occupies(g, nh.x, nh.y)) {
        g->snake.alive = 0;
        g->running = 0;
        return;
    }

    // zjedol ovocie?
    int ate = (nh.x == g->fruit.x && nh.y == g->fruit.y);
    

    // ak zje, zv���me d�ku (max MAX_SNAKE)
    if (ate) {
        g->score += 10;
        if (g->snake.len < MAX_SNAKE){
            g->snake.len++;
        }
    }

    // posun segmentov: od konca k hlave
    for (int i = g->snake.len - 1; i > 0; i--) {
        g->snake.parts[i] = g->snake.parts[i - 1];
    }
    g->snake.parts[0] = nh;

    // po zjeden� spawnni nov� ovocie
    if (ate) spawn_fruit(g);
}

/*
  Vytvor� ASCII mapu do out bufferu.
  Klient to nebude parsova� "inteligentne" � len to vyp�e.
*/
int game_render_map(GameState* g, char* out, int out_cap) {
    clear_board(g);
    
    // Vykresli okraje podľa typu sveta
    if (g->world == WORLD_WALLS) {
        draw_walls(g, '_', '|');  // WORLD_WALLS: _ a |
    } else {
        draw_walls(g, '#', '#');  // WORLD_WRAP: # (len vizuálne)
    }

    // ovocie
    g->board[g->fruit.y][g->fruit.x] = 'o';

    // had: hlava '@', telo '*'
    for (int i = 0; i < g->snake.len; i++) {
        int x = g->snake.parts[i].x;
        int y = g->snake.parts[i].y;
        g->board[y][x] = (i == 0) ? '@' : '*';
    }

    // zlo�enie textu do out
    int n = 0;
    n += snprintf(out + n, out_cap - n, "MAP\n");

    // Ak je pauza, pridať PAUSED indikátor
    if (g->paused) {
        n += snprintf(out + n, out_cap - n, "=== PAUSED (ESC to resume) ===\n");
    }

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
