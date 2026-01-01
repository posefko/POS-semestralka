#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../common/protocol.h"

int main() {
    // 1?? Vytvorenie TCP socketu
    // AF_INET = IPv4, SOCK_STREAM = TCP
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    // 2?? Nastavenie adresy servera
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;           // IPv4
    addr.sin_port = htons(SERVER_PORT);  // port (v network byte order)
    addr.sin_addr.s_addr = INADDR_ANY;   // poèúvaj na všetkých rozhraniach

    // 3?? Priradenie adresy socketu (bind)
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    // 4?? Server zaène poèúva (max 1 klient zatia¾)
    listen(server_fd, 1);
    printf("Server listening on port %d\n", SERVER_PORT);

    // 5?? Èakanie na pripojenie klienta
    int client_fd = accept(server_fd, NULL, NULL);
    printf("Client connected\n");

    // 6?? Komunikaèný cyklus servera
    char buffer[256];

    while (1) {
        // Prijatie správy od klienta
        int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        // Ak klient zavrel spojenie
        if (n <= 0) break;

        buffer[n] = '\0'; // ukonèenie stringu
        printf("Received from client: %s", buffer);

        // Zatia¾ server len odpovie "OK"
        send(client_fd, "MSG OK\n", 7, 0);
    }

    // 7?? Upratanie
    close(client_fd);
    close(server_fd);
    return 0;
}
