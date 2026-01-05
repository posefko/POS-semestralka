#pragma once
#include <pthread.h>
#include <time.h>

/*
  Rozmery mapy (budú sa meniť podľa výberu)
*/
extern int MAP_ROWS;
extern int MAP_COLS;

#define MAX_ROWS 30
#define MAX_COLS 60
#define ROWS MAP_ROWS
#define COLS MAP_COLS

/*
  Typ sveta: so stenami alebo wrap-around
*/
typedef enum {
    WORLD_WALLS,
    WORLD_WRAP
} WorldType;

/*
  Herný režim
*/
typedef enum {
    MODE_STANDARD,    // štandardný režim (10s timeout po smrti)
    MODE_TIMED        // časový režim (hra končí po uplynulč času)
} GameMode;

/*
  Max d�ka hada � v tomto kroku pou��vame statick� pole,
  aby sme nemuseli rie�i� malloc/free (jednoduch�ie, menej bugov).
*/
#define MAX_SNAKE 256

// Jednoduch� poz�cia v mrie�ke
typedef struct {
    int x, y;
} Pos;

/*
  Snake = "objekt" v C (struct).
  - parts[]: segmenty hada (0 je hlava)
  - len: aktu�lna d�ka
  - dir: smer pohybu ('w','a','s','d')
  - alive/running: stav hry
*/
typedef struct {
    Pos parts[MAX_SNAKE];
    int len;
    char dir;
    int alive;
} Snake;

/*
  GameState = kompletný stav hry na SERVERI.
*/
typedef struct {
    char board[MAX_ROWS][MAX_COLS];
    char obstacles[MAX_ROWS][MAX_COLS];
    Snake snake;
    Pos fruit;
    int running;
    int score;
    WorldType world;
    GameMode game_mode;
    int paused;
    int time_limit_sec;
    time_t start_time;
    time_t pause_start;
    int total_pause_time;
    time_t death_time;
    int has_obstacles;
    pthread_mutex_t mtx;
} GameState;

void game_init(GameState* g, WorldType world, GameMode game_mode, int time_limit_sec,
               int rows, int cols, int has_obstacles);

// Nastavenie smeru pohybu (vol� sa zo serverov�ho receive threadu)
void game_set_dir(GameState* g, char dir);

// Posun hry o 1 tick (server game loop)
void game_step(GameState* g);

/*
  Vytvor� textov� mapu do bufferu (out).
  Form�t:
    MAP\n
    <ROWS riadkov>\n
    ENDMAP\n
*/
int game_render_map(GameState* g, char* out, int out_cap);
