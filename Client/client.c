#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../common/protocol.h"


static void clear_screen(void) {
    // ANSI clear (frios2 terminal)
    write(STDOUT_FILENO, "\033[H\033[J", 6);
}

int main() {
    // 1️⃣ Vytvorenie socketu
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    // 2️⃣ Nastavenie adresy servera
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);

    // Server beží na localhoste
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    // 3️⃣ Pripojenie klienta na server
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    char buffer[4096];

    while (1) {
        // vstup od hráča
        printf("Enter command (MOVE w / QUIT): ");
        fgets(buffer, sizeof(buffer), stdin);

        send(sock, buffer, strlen(buffer), 0);

        // príjem mapy zo servera
        int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;

        buffer[n] = '\0';

        clear_screen();
        printf("%s", buffer);
        fflush(stdout);
    }

    // 5️⃣ Zatvorenie spojenia
    close(sock);
    return 0;
}