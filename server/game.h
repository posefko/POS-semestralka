#pragma once
#include <pthread.h>
#include <time.h>

/*
  Rozmery mapy (ASCII).
  Server bude posiela� ROWS riadkov po COLS znakov.
*/
#define ROWS 20
#define COLS 40

/*
  Typ sveta: so stenami alebo wrap-around (bez prekážok)
*/
typedef enum {
    WORLD_WALLS,      // svet so stenami (náraz do steny = GAME OVER)
    WORLD_WRAP        // svet bez prekážok (wrap-around)
} WorldType;

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
  GameState = kompletn� stav hry na SERVERI.
  Klient nem� logiku hry, iba zobrazuje text.
*/
typedef struct {
    char board[ROWS][COLS];  // v�sledn� vykres�ovan� mrie�ka (server ju sklad�)
    Snake snake;             // zatia� 1 had
    Pos fruit;               // ovocie
    int running;             // 1 = hra be��, 0 = koniec
    int score;              // aktualne skore
    WorldType world;         // typ sveta (WORLD_WALLS / WORLD_WRAP)
    int paused;              // 1 = hra je pozastaven�, 0 = be�
    int time_limit_sec;      // limit �asu (0 = nekone�n� re�im)
    time_t start_time;       // �as za�iatku hry
    time_t pause_start;      // �as za�iatku pauzy (pre po��tanie �asu bez pauzy)
    int total_pause_time;    // celkov� �as str�ven� v pauze
    pthread_mutex_t mtx;     // mutex chr�ni cel� stav (dir, posun, ovocie, running)
} GameState;

// Inicializ�cia hry (nastav� hada, ovocie, running, mutex�)
// world - typ sveta (WORLD_WALLS alebo WORLD_WRAP)
void game_init(GameState* g, WorldType world);

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
