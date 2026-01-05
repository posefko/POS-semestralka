#include "game.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

/* Globálne premenné pre veľkosť mapy */
int MAP_ROWS = 20;
int MAP_COLS = 40;

/*
  Pomocná funkcia: je (x,y) vnútri hraníc?
*/
static int in_bounds(int x, int y) {
    return x >= 0 && x < MAP_COLS && y >= 0 && y < MAP_ROWS;
}

/*
  Vyčisti board na medzery.
*/
static void clear_board(GameState* g) {
    for (int y = 0; y < MAP_ROWS; y++)
        for (int x = 0; x < MAP_COLS; x++)
            g->board[y][x] = ' ';
}

/*
  Nakresli okrajové steny.
*/
static void draw_walls(GameState* g, char horiz, char vert) {
    for (int x = 0; x < MAP_COLS; x++) {
        g->board[0][x] = horiz;
        g->board[MAP_ROWS - 1][x] = horiz;
    }
    for (int y = 0; y < MAP_ROWS; y++) {
        g->board[y][0] = vert;
        g->board[y][MAP_COLS - 1] = vert;
    }
}

/* BFS pre kontrolu dosiahnuteľnosti */
static int is_reachable(GameState* g, int start_x, int start_y) {
    char visited[MAX_ROWS][MAX_COLS] = {0};
    int queue_x[MAX_ROWS * MAX_COLS], queue_y[MAX_ROWS * MAX_COLS];
    int head = 0, tail = 0;
    
    queue_x[tail] = start_x;
    queue_y[tail] = start_y;
    tail++;
    visited[start_y][start_x] = 1;
    int reachable_count = 1;
    
    while (head < tail) {
        int x = queue_x[head];
        int y = queue_y[head];
        head++;
        
        int dx[] = {0, 0, 1, -1};
        int dy[] = {1, -1, 0, 0};
        
        for (int i = 0; i < 4; i++) {
            int nx = x + dx[i];
            int ny = y + dy[i];
            
            if (nx > 0 && nx < MAP_COLS - 1 && ny > 0 && ny < MAP_ROWS - 1 &&
                !visited[ny][nx] && !g->obstacles[ny][nx]) {
                visited[ny][nx] = 1;
                queue_x[tail] = nx;
                queue_y[tail] = ny;
                tail++;
                reachable_count++;
            }
        }
    }
    
    /* Počet voľných políčok */
    int free_count = 0;
    for (int y = 1; y < MAP_ROWS - 1; y++) {
        for (int x = 1; x < MAP_COLS - 1; x++) {
            if (!g->obstacles[y][x]) free_count++;
        }
    }
    
    return reachable_count == free_count;
}

/*
  Generuj náhodné prekážky s kontrolou dosiahnuteľnosti
*/
static void generate_obstacles(GameState* g) {
    memset(g->obstacles, 0, sizeof(g->obstacles));
    
    /* 3-5% políčok budú prekážky */
    int obstacle_count = ((MAP_ROWS - 2) * (MAP_COLS - 2)) / 25;
    
    for (int i = 0; i < obstacle_count; i++) {
        int x, y;
        do {
            x = 1 + rand() % (MAP_COLS - 2);
            y = 1 + rand() % (MAP_ROWS - 2);
        } while ((x == 1 && y == 1) || g->obstacles[y][x]);
        
        g->obstacles[y][x] = 1;
        
        /* Kontrola dosiahnuteľnosti */
        if (!is_reachable(g, 1, 1)) {
            g->obstacles[y][x] = 0;  /* Zruš túto prekážku */
        }
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
  Spawn ovocia: náhodná pozícia, nie na hade ani prekážkach
*/
static void spawn_fruit(GameState* g) {
    int fx, fy;
    do {
        fx = 1 + rand() % (MAP_COLS - 2);
        fy = 1 + rand() % (MAP_ROWS - 2);
    } while (snake_occupies(g, fx, fy) || g->obstacles[fy][fx]);
    g->fruit.x = fx;
    g->fruit.y = fy;
}

void game_init(GameState* g, WorldType world, GameMode game_mode, int time_limit_sec,
               int rows, int cols, int has_obstacles) {
    memset(g, 0, sizeof(*g));
    
    /* Nastavenie veľkosti mapy */
    MAP_ROWS = rows;
    MAP_COLS = cols;

    pthread_mutex_init(&g->mtx, NULL);

    srand((unsigned)time(NULL));
    g->running = 1;
    g->score = 0;
    g->world = world;
    g->game_mode = game_mode;
    g->paused = 0;
    g->time_limit_sec = time_limit_sec;
    g->start_time = time(NULL);
    g->pause_start = 0;
    g->total_pause_time = 0;
    g->death_time = 0;
    g->has_obstacles = has_obstacles;
    
    /* Generuj prekážky ak sú požadované */
    if (has_obstacles) {
        generate_obstacles(g);
    }

    /* Inicializácia hada */
    g->snake.alive = 1;
    g->snake.len = 3;
    g->snake.dir = 'd';

    int sx = MAP_COLS / 2;
    int sy = MAP_ROWS / 2;
    g->snake.parts[0] = (Pos){ sx, sy };
    g->snake.parts[1] = (Pos){ sx - 1, sy };
    g->snake.parts[2] = (Pos){ sx - 2, sy };

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
        if (nh.x <= 0) nh.x = MAP_COLS - 2;
        else if (nh.x >= MAP_COLS - 1) nh.x = 1;

        if (nh.y <= 0) nh.y = MAP_ROWS - 2;
        else if (nh.y >= MAP_ROWS - 1) nh.y = 1;
    }
    // WORLD_WALLS: náraz do steny = koniec
    else if (g->world == WORLD_WALLS) {
        if (!in_bounds(nh.x, nh.y) || nh.x == 0 || nh.x == MAP_COLS - 1 || nh.y == 0 || nh.y == MAP_ROWS - 1) {
            g->snake.alive = 0;
            g->running = 0;  // Okamžite ukončiť hru
            return;
        }
    }
    
    // náraz do prekážky = koniec
    if (g->has_obstacles && g->obstacles[nh.y][nh.x]) {
        g->snake.alive = 0;
        g->running = 0;
        return;
    }

    // náraz do seba = koniec
    if (snake_occupies(g, nh.x, nh.y)) {
        g->snake.alive = 0;
        g->running = 0;  // Okamžite ukončiť hru
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
    
    // Vykresli prekážky
    if (g->has_obstacles) {
        for (int y = 1; y < MAP_ROWS - 1; y++) {
            for (int x = 1; x < MAP_COLS - 1; x++) {
                if (g->obstacles[y][x]) {
                    g->board[y][x] = '#';
                }
            }
        }
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
