#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <termios.h>
#include <signal.h>

#include "../Common/protocol.h"

#define BUFFER_SIZE 4096

static int sock;
static volatile int running = 1;
static int score = 0;


// TERMINAL
static struct termios old_termios;

static void enable_raw_mode(void) {
    struct termios t;
    tcgetattr(STDIN_FILENO, &old_termios);
    t = old_termios;
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void disable_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
}

static void clear_screen(void) {
    (void)write(STDOUT_FILENO, "\033[H\033[J", 6);
}


// INPUT THREAD
static void* input_thread(void* arg) {
    (void)arg;  // unused
    char c;
    char msg[32];
    static int paused = 0;  // lokalny flag pre toggle pauzy

    enable_raw_mode();

    while (running) {
        if (read(STDIN_FILENO, &c, 1) <= 0)
            continue;

        if (c == 'q') {
            send(sock, CMD_QUIT "\n", strlen(CMD_QUIT) + 1, 0);
            running = 0;
            break;
        }

        // ESC (kod 27) - prepinanie pauzy
        if (c == 27) {
            if (paused) {
                send(sock, CMD_RESUME "\n", strlen(CMD_RESUME) + 1, 0);
                paused = 0;
            } else {
                send(sock, CMD_PAUSE "\n", strlen(CMD_PAUSE) + 1, 0);
                paused = 1;
            }
            continue;
        }

        if (c == 'w' || c == 'a' || c == 's' || c == 'd') {
            snprintf(msg, sizeof(msg), "%s %c\n", CMD_MOVE, c);
            send(sock, msg, strlen(msg), 0);
        }
    }

    disable_raw_mode();
    return NULL;
}

// tato cast bola vytvorena pomocou AI
// RENDER THREAD
static void* render_thread(void* arg) {
    (void)arg;  // unused
    char recvbuf[BUFFER_SIZE];
    char frame[BUFFER_SIZE * 2];
    int frame_len = 0;
    char time_str[32] = "0s";
    char mode_str[32] = "STANDARD";

    while (running) {
        int n = recv(sock, recvbuf, sizeof(recvbuf) - 1, 0);
        if (n <= 0) break;

        memcpy(frame + frame_len, recvbuf, n);
        frame_len += n;
        frame[frame_len] = '\0';

        /* SCORE */
        char* s = strstr(frame, CMD_SCORE);
        if (s) {
            sscanf(s + strlen(CMD_SCORE), "%d", &score);
        }

        /* MODE - parsuj pred TIME */
        char* m = strstr(frame, "MODE ");
        if (m && m < frame + frame_len - 10) {
            sscanf(m + 5, "%31s", mode_str);
        }

        /* TIME - hladaj "\nTIME " aby sme nenasli TIME vo vnutri TIMED */
        char* map_start = strstr(frame, CMD_MAP);
        char* t = strstr(frame, "\nTIME ");
        if (t && (!map_start || t < map_start)) {
            // Parsuje cas (preskocime \n a "TIME")
            if (sscanf(t + 6, "%31s", time_str) != 1) {
                strcpy(time_str, "N/A");
            }
        }

        /* GAME OVER */
        if (strstr(frame, CMD_GAME_OVER)) {
            clear_screen();
            printf("\n");
            printf("╔════════════════════════════════════════════════════════════╗\n");
            printf("║                        GAME OVER                           ║\n");
            printf("╠════════════════════════════════════════════════════════════╣\n");
            printf("║  Rezim: %-20s                            ║\n", mode_str);
            printf("║  Finalne skore: %-5d                                    ║\n", score);
            printf("║  Cas: %-20s                               ║\n", time_str);
            printf("╠════════════════════════════════════════════════════════════╣\n");
            printf("║         Stlac Enter pre navrat do menu...                  ║\n");
            printf("╚════════════════════════════════════════════════════════════╝\n");
            printf("\n");
            running = 0;
            break;
        }

        /* MAP */
        char* m1 = strstr(frame, CMD_MAP "\n");
        char* m2 = strstr(frame, "ENDMAP\n");

        if (m1 && m2) {
            m1 += strlen(CMD_MAP) + 1;
            clear_screen();
            
            // Zobraz header s informaciami o hre
            printf("╔════════════════════════════════════════════════════════════╗\n");
            printf("║  REZIM: %-10s │ SKORE: %-5d │ CAS: %-15s ║\n", 
                   mode_str, score, time_str);
            printf("╚════════════════════════════════════════════════════════════╝\n");
            
            (void)write(STDOUT_FILENO, m1, m2 - m1);
            fflush(stdout);

            int used = (m2 + strlen("ENDMAP\n")) - frame;
            memmove(frame, frame + used, frame_len - used);
            frame_len -= used;
        }
    }

    running = 0;
    return NULL;
}

// MAIN
/*
* Zobrazi hlavne menu a vrati volbu uzivatela
*/
static int show_main_menu(void) {
    clear_screen();
    printf("\n=== SNAKE ===\n");
    printf("1) Nova hra (lokalne)\n");
    printf("2) Pripojit k hre (IP/PORT)\n");
    printf("3) Koniec\n");
    printf("> ");
    fflush(stdout);
    
    char choice[10];
    if (fgets(choice, sizeof(choice), stdin) == NULL) return 3;
    return atoi(choice);
}

/*
* Zobrazi menu vyberu herneho rezimu a vrati rezim + cas
* Return: 1 = OK, 0 = spat do hlavneho menu
*/
static int show_game_mode_menu(char* mode_str, int* time_limit) {
clear_screen();
printf("\nVyber herny rezim:\n");
printf("1) Standardny\n");
printf("2) Casovy (hra konci po uplynutí casu)\n");
printf("0) Spat\n");
printf("> ");
fflush(stdout);
    
    char choice[10];
    if (fgets(choice, sizeof(choice), stdin) == NULL) {
        return 0;
    }
    
    int c = atoi(choice);
    
    if (c == 0) {
        return 0;  // Spat
    }
    
    if (c == 2) {
        strcpy(mode_str, "TIMED");
        
        while (1) {
            clear_screen();
            printf("\nVyber cas hry:\n");
            printf("1) 30 sekund\n");
            printf("2) 60 sekund\n");
            printf("3) 120 sekund\n");
            printf("0) Spat\n");
            printf("> ");
            fflush(stdout);
            
            if (fgets(choice, sizeof(choice), stdin) != NULL) {
                int tc = atoi(choice);
                if (tc == 0) {
                    return 0;  // Spat do hlavneho menu
                }
                switch (tc) {
                    case 1: *time_limit = 30; return 1;
                    case 2: *time_limit = 60; return 1;
                    case 3: *time_limit = 120; return 1;
                    default: 
                        printf("Neplatna volba, skus znova.\n");
                        continue;
                }
            } else {
                return 0;
            }
        }
    } else {
        strcpy(mode_str, "STANDARD");
        *time_limit = 0;
        return 1;
    }
}

/*
* Zobrazi menu vyberu velkosti mapy
*/
static int show_size_menu(int* rows, int* cols) {
    clear_screen();
    printf("\nVelkost mapy:\n");
    printf("1) Mala (20x40)\n");
    printf("2) Stredna (25x50)\n");
    printf("3) Velka (30x60)\n");
    printf("0) Spat\n");
    printf("> ");
    fflush(stdout);
    
    char choice[10];
    if (fgets(choice, sizeof(choice), stdin) == NULL) return 0;
    
    int c = atoi(choice);
    if (c == 0) return 0;
    
    switch (c) {
        case 1: *rows = 20; *cols = 40; break;
        case 2: *rows = 25; *cols = 50; break;
        case 3: *rows = 30; *cols = 60; break;
        default: *rows = 20; *cols = 40;
    }
    return 1;
}

/*
* Zobrazi menu vyberu sveta
*/
static int show_world_menu(const char** world, int* has_obstacles) {
    clear_screen();
    printf("\nTyp okrajov mapy:\n");
    printf("1) Steny (naraz = koniec)\n");
    printf("2) Wrap (prechod cez okraj)\n");
    printf("0) Spat\n");
    printf("> ");
    fflush(stdout);
    
    char choice_buf[10];
    if (fgets(choice_buf, sizeof(choice_buf), stdin) == NULL) return 0;
    int wc = atoi(choice_buf);
    if (wc == 0) return 0;
    *world = (wc == 1) ? "WALLS" : "WRAP";
    
    clear_screen();
    printf("\nPrekazky v mape:\n");
    printf("1) Bez prekazok\n");
    printf("2) S prekazkami (nahodne generovane)\n");
    printf("0) Spat\n");
    printf("> ");
    fflush(stdout);
    
    if (fgets(choice_buf, sizeof(choice_buf), stdin) == NULL) return 0;
    int oc = atoi(choice_buf);
    if (oc == 0) return 0;
    *has_obstacles = (oc == 2) ? 1 : 0;
    
    return 1;
}

/*
* Spusti hernu session (thready, raw mode)
*/
static void run_game_session(void) {
    pthread_t tin, tr;
    
    running = 1;
    score = 0;
    
    pthread_create(&tin, NULL, input_thread, NULL);
    pthread_create(&tr, NULL, render_thread, NULL);

    pthread_join(tin, NULL);
    pthread_join(tr, NULL);
    
    disable_raw_mode();
    
    /* GAME OVER obrazovka je uz zobrazena render threadom */
    printf("\n");
    fflush(stdout);
    
    char dummy[10];
    if (fgets(dummy, sizeof(dummy), stdin) == NULL) {
        //ignore
    }
}

int main(void) {
    struct sockaddr_in addr;
    pid_t server_pid = -1;

    int local_server_started = 0; // ci sme tento server forkli my
    int game_started = 0;         // ci sme uz poslali START

    char server_ip[128] = "127.0.0.1";
    int server_port = SERVER_PORT;

    // MENU LOOP
    while (1) {
        int choice = show_main_menu();
        
        if (choice == 3) {
            break;
        }
        
        // Spustenie lokalneho servera
        if (choice == 1) {
            // ak uz lokalny server bezi, nechceme spustat dalsi (bind na port by padol)
            if (server_pid > 0 && kill(server_pid, 0) == 0) {
                local_server_started = 1;
                game_started = 0;
            }
            else {
                server_pid = fork();
                if (server_pid == 0) {
                    execl("./bin/server", "server", NULL);
                    perror("execl failed");
                    exit(1);
                }
                local_server_started = 1;
                game_started = 0;
                sleep(1);
            }

            strcpy(server_ip, "127.0.0.1");
            server_port = SERVER_PORT;
        }

        
        
        // Pripojenie na vzdialeny server
        if (choice == 2) {
            clear_screen();
            printf("\n=== Pripojenie na server ===\n");
            printf("IP adresa: ");
            fflush(stdout);
            if (fgets(server_ip, sizeof(server_ip), stdin) != NULL) {
                server_ip[strcspn(server_ip, "\n")] = 0;
            }
            printf("Port: ");
            fflush(stdout);
            char port_buf[10];
            if (fgets(port_buf, sizeof(port_buf), stdin) != NULL) {
                server_port = atoi(port_buf);
            }
        }
        
        if (choice == 1 || choice == 2) {
            sock = socket(AF_INET, SOCK_STREAM, 0);
            addr.sin_family = AF_INET;
            addr.sin_port = htons(server_port);
            inet_pton(AF_INET, server_ip, &addr.sin_addr);
            
            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                perror("connect");
                close(sock);
                if (server_pid > 0 && local_server_started && !game_started) {
                    kill(server_pid, SIGTERM);
                    server_pid = -1;
                    local_server_started = 0;
                }
                continue;
            }
            
            printf("Pripojeny na %s:%d\n", server_ip, server_port);
            sleep(1);
            
            // Vyber velkosti
            int map_rows = 20, map_cols = 40;
            if (!show_size_menu(&map_rows, &map_cols)) {
                close(sock);
                if (server_pid > 0 && local_server_started && !game_started) {
                    kill(server_pid, SIGTERM);
                    server_pid = -1;
                    local_server_started = 0;
                }
                continue;
            }
            
            // Vyber rezimu
            char mode_str[32];
            int time_limit = 0;
            if (!show_game_mode_menu(mode_str, &time_limit)) {
                close(sock);
                if (server_pid > 0 && local_server_started && !game_started) {
                    kill(server_pid, SIGTERM);
                    server_pid = -1;
                    local_server_started = 0;
                }
                continue;
            }
            
            // Vyber sveta a prekazok
            const char* world = "WRAP";
            int has_obstacles = 0;
            if (!show_world_menu(&world, &has_obstacles)) {
                close(sock);
                if (server_pid > 0 && local_server_started && !game_started) {
                    kill(server_pid, SIGTERM);
                    server_pid = -1;
                    local_server_started = 0;
                }
                continue;
            }
            
            // Posle START prikaz: START <rows> <cols> <walls/wrap> <obstacles> <mode> [time]
            char start_cmd[128];
            if (strcmp(mode_str, "TIMED") == 0) {
                snprintf(start_cmd, sizeof(start_cmd), "%s %d %d %s %s %s %d\n", 
                         CMD_START, map_rows, map_cols, world, 
                         has_obstacles ? "OBS" : "NOOBS", mode_str, time_limit);
            } else {
                snprintf(start_cmd, sizeof(start_cmd), "%s %d %d %s %s %s\n", 
                         CMD_START, map_rows, map_cols, world,
                         has_obstacles ? "OBS" : "NOOBS", mode_str);
            }
            send(sock, start_cmd, strlen(start_cmd), 0);
            game_started = 1;

            
            run_game_session();
            
            close(sock);
            /* Server zabijame iba ak sme ho spustili my a hra este nezacala
             * Po START uz server nebude zavisli od klienta
             */
            if (server_pid > 0 && local_server_started && !game_started) {
                kill(server_pid, SIGTERM);
                server_pid = -1;
                local_server_started = 0;
            }
        }
    }

    return 0;
}
