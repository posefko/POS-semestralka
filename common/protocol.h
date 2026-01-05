#pragma once

/* ================= SERVER ================= */

// Port, na ktorom server počúva
#define SERVER_PORT 5555


/* ================= PRÍKAZY OD KLIENTA ================= */

// štart hry s typom sveta (napr. "START WALLS" alebo "START WRAP")
#define CMD_START "START"

// pohyb hráča (napr. "MOVE w")
#define CMD_MOVE "MOVE"

// korektné ukončenie klienta
#define CMD_QUIT "QUIT"

// pozastavenie hry
#define CMD_PAUSE "PAUSE"

// pokračovanie v hre
#define CMD_RESUME "RESUME"


/* ================= ODPOVEDE SERVERA ================= */

// obyčajná textová správa (debug / info)
#define CMD_MSG "MSG"

// herná mapa (ASCII výpis)
#define CMD_MAP "MAP"

// skóre hráča (napr. "SCORE 12")
#define CMD_SCORE "SCORE"

// čas hry v sekundách (napr. "TIME 42")
#define CMD_TIME "TIME"

// koniec hry (kolízia)
#define CMD_GAME_OVER "GAME_OVER"
