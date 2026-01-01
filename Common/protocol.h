#pragma once

// Port, na ktorom server poèúva
#define SERVER_PORT 5555

// Príkazy od klienta na server
#define CMD_MOVE "MOVE"   // pohyb (napr. MOVE w)
#define CMD_QUIT "QUIT"   // ukonèenie

// Odpoveï servera klientovi
#define CMD_MSG  "MSG"    // obyèajná textová správa
