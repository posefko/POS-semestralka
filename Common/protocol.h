#pragma once

//SERVER
// Port, na ktorom server pocuva
#define SERVER_PORT 5555
//________________________________________________________


//PRIKAZY OD KLIENTA
// start hry (napr. "START WALLS STANDARD" alebo "START WRAP TIMED 60")
#define CMD_START "START"

// pohyb hraca
#define CMD_MOVE "MOVE"

// korektne ukoncenie klienta
#define CMD_QUIT "QUIT"

// pozastavenie hry
#define CMD_PAUSE "PAUSE"

// pokracovanie v hre
#define CMD_RESUME "RESUME"
//________________________________________________________


//ODPOVEDE SERVERA
// obycajna textova sprava
#define CMD_MSG "MSG"

// herna mapa (ASCII vypis)
#define CMD_MAP "MAP"

// skore hraca
#define CMD_SCORE "SCORE"

// cas hry v sekundach
#define CMD_TIME "TIME"

// koniec hry (kolizia)
#define CMD_GAME_OVER "GAME_OVER"
