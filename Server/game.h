#pragma once
#include <pthread.h>

/*
  Rozmery mapy (ASCII).
  Server bude posiela ROWS riadkov po COLS znakov.
*/
#define ROWS 20
#define COLS 40

/*
  Max dåka hada – v tomto kroku pouívame statické pole,
  aby sme nemuseli rieši malloc/free (jednoduchšie, menej bugov).
*/
#define MAX_SNAKE 256

// Jednoduchá pozícia v mrieke
typedef struct {
    int x, y;
} Pos;

/*
  Snake = "objekt" v C (struct).
  - parts[]: segmenty hada (0 je hlava)
  - len: aktuálna dåka
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
  GameState = kompletnı stav hry na SERVERI.
  Klient nemá logiku hry, iba zobrazuje text.
*/
typedef struct {
    char board[ROWS][COLS];  // vısledná vykres¾ovaná mrieka (server ju skladá)
    Snake snake;             // zatia¾ 1 had
    Pos fruit;               // ovocie
    int running;             // 1 = hra beí, 0 = koniec
    pthread_mutex_t mtx;     // mutex chráni celı stav (dir, posun, ovocie, running)
} GameState;

// Inicializácia hry (nastaví hada, ovocie, running, mutex…)
void game_init(GameState* g);

// Nastavenie smeru pohybu (volá sa zo serverového receive threadu)
void game_set_dir(GameState* g, char dir);

// Posun hry o 1 tick (server game loop)
void game_step(GameState* g);

/*
  Vytvorí textovú mapu do bufferu (out).
  Formát:
    MAP\n
    <ROWS riadkov>\n
    ENDMAP\n
*/
int game_render_map(GameState* g, char* out, int out_cap);
