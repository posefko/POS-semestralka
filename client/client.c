#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <termios.h>

#include "../common/protocol.h"

#define BUFFER_SIZE 4096

static int sock;
static volatile int running = 1;

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
    write(STDOUT_FILENO, "\033[H\033[J", 6);
}

static void* input_thread(void* arg) {
    char c;
    char msg[32];

    enable_raw_mode();

    while (running) {
        if (read(STDIN_FILENO, &c, 1) <= 0)
            continue;

        if (c == 'q') {
            running = 0;
            break;
        }

        if (c == 'w' || c == 'a' || c == 's' || c == 'd') {
            snprintf(msg, sizeof(msg), "MOVE %c\n", c);
            send(sock, msg, strlen(msg), 0);
        }
    }

    disable_raw_mode();
    return NULL;
}

static void* render_thread(void* arg) {
    char buffer[BUFFER_SIZE];

    while (running) {
        int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            running = 0;
            break;
        }

        buffer[n] = '\0';
        clear_screen();
        printf("%s", buffer);
        fflush(stdout);
    }

    return NULL;
}

int main(void) {
    struct sockaddr_in addr;
    pthread_t tin, tr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    pthread_create(&tin, NULL, input_thread, NULL);
    pthread_create(&tr, NULL, render_thread, NULL);

    pthread_join(tin, NULL);
    pthread_join(tr, NULL);

    close(sock);
    clear_screen();
    printf("Client ukončený.\n");
    return 0;
}
