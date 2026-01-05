#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <termios.h>

#include "../Common/protocol.h"

#define BUFFER_SIZE 4096

static int sock;
static volatile int running = 1;
static int score = 0;

/* ================= TERMINAL ================= */

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

/* ================= INPUT THREAD ================= */

static void* input_thread(void* arg) {
    (void)arg;  // unused
    char c;
    char msg[32];
    static int paused = 0;  // lokálny flag pre toggle pauzy

    enable_raw_mode();

    while (running) {
        if (read(STDIN_FILENO, &c, 1) <= 0)
            continue;

        if (c == 'q') {
            send(sock, CMD_QUIT "\n", strlen(CMD_QUIT) + 1, 0);
            running = 0;
            break;
        }

        // ESC (kód 27) - prepínanie pauzy
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

/* ================= RENDER THREAD ================= */

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

        /* TIME */
        char* t = strstr(frame, CMD_TIME);
        if (t) {
            sscanf(t + strlen(CMD_TIME), "%s", time_str);
        }

        /* MODE */
        char* m = strstr(frame, "MODE ");
        if (m) {
            sscanf(m + 5, "%s", mode_str);
        }

        /* GAME OVER */
        if (strstr(frame, CMD_GAME_OVER)) {
            clear_screen();
            printf("\n");
            printf("╔════════════════════════════════════════════════════════════╗\n");
            printf("║                        GAME OVER                           ║\n");
            printf("╠════════════════════════════════════════════════════════════╣\n");
            printf("║  Režim: %-20s                            ║\n", mode_str);
            printf("║  Finálne skóre: %-5d                                    ║\n", score);
            printf("║  Čas: %-20s                               ║\n", time_str);
            printf("╠════════════════════════════════════════════════════════════╣\n");
            printf("║         Stlač Enter pre návrat do menu...                  ║\n");
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
            
            // Zobraz header s informáciami o hre
            printf("╔════════════════════════════════════════════════════════════╗\n");
            printf("║  REŽIM: %-10s │ SKÓRE: %-5d │ ČAS: %-15s ║\n", 
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

/* ================= MAIN ================= */

/*
 * Zobrazí hlavné menu a vráti voľbu užívateľa
 */
static int show_main_menu(void) {
    printf("\n=== SNAKE ===\n");
    printf("1) Nová hra\n");
    printf("2) Koniec\n");
    printf("> ");
    fflush(stdout);
    
    char choice[10];
    if (fgets(choice, sizeof(choice), stdin) == NULL) return 2;
    return atoi(choice);
}

/*
 * Zobrazí menu výberu herného režimu a vráti režim + čas
 * Return: 1 = OK, 0 = späť do hlavného menu
 */
static int show_game_mode_menu(char* mode_str, int* time_limit) {
    printf("\nVyber herný režim:\n");
    printf("1) Štandardný\n");
    printf("2) Časový (hra končí po uplynutí času)\n");
    printf("0) Späť\n");
    printf("> ");
    fflush(stdout);
    
    char choice[10];
    if (fgets(choice, sizeof(choice), stdin) == NULL) {
        return 0;
    }
    
    int c = atoi(choice);
    
    if (c == 0) {
        return 0;  // Späť
    }
    
    if (c == 2) {
        strcpy(mode_str, "TIMED");
        
        while (1) {
            printf("\nVyber čas hry:\n");
            printf("1) 30 sekúnd\n");
            printf("2) 60 sekúnd\n");
            printf("3) 120 sekúnd\n");
            printf("0) Späť\n");
            printf("> ");
            fflush(stdout);
            
            if (fgets(choice, sizeof(choice), stdin) != NULL) {
                int tc = atoi(choice);
                if (tc == 0) {
                    return 0;  // Späť do hlavného menu
                }
                switch (tc) {
                    case 1: *time_limit = 30; return 1;
                    case 2: *time_limit = 60; return 1;
                    case 3: *time_limit = 120; return 1;
                    default: 
                        printf("Neplatná voľba, skús znova.\n");
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
 * Zobrazí menu výberu sveta
 * Return: 1 = OK, 0 = späť
 */
static int show_world_menu(const char** world) {
    printf("\nVyber herný svet:\n");
    printf("1) Svet s prekážkami (walls)\n");
    printf("2) Svet bez prekážok (wrap)\n");
    printf("0) Späť\n");
    printf("> ");
    fflush(stdout);
    
    char choice_buf[10];
    if (fgets(choice_buf, sizeof(choice_buf), stdin) != NULL) {
        int wc = atoi(choice_buf);
        if (wc == 0) return 0;
        *world = (wc == 1) ? "WALLS" : "WRAP";
        return 1;
    }
    return 0;
}

/*
 * Spustí hernú session (thready, raw mode)
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
    clear_screen();
}

int main(void) {
    struct sockaddr_in addr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    /* MENU LOOP */
    while (1) {
        int choice = show_main_menu();
        
        if (choice == 2) {
            printf("Dovidenia!\n");
            break;
        }
        
        if (choice == 1) {
            /* Výber režimu */
            char mode_str[32];
            int time_limit = 0;
            if (!show_game_mode_menu(mode_str, &time_limit)) {
                continue;  // Späť do hlavného menu
            }
            
            /* Výber sveta */
            const char* world = "WRAP";
            if (!show_world_menu(&world)) {
                continue;  // Späť do hlavného menu
            }
            
            /* Pošli START príkaz serveru */
            char start_cmd[128];
            if (strcmp(mode_str, "TIMED") == 0) {
                snprintf(start_cmd, sizeof(start_cmd), "%s %s %s %d\n", 
                         CMD_START, world, mode_str, time_limit);
            } else {
                snprintf(start_cmd, sizeof(start_cmd), "%s %s %s\n", 
                         CMD_START, world, mode_str);
            }
            send(sock, start_cmd, strlen(start_cmd), 0);
            
            /* Spusti hernú session */
            run_game_session();
            
            /* Počkaj na Enter */
            getchar();
        }
    }

    close(sock);
    printf("Client ukončený.\n");
    return 0;
}
