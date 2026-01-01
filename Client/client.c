#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../common/protocol.h"

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

    char buffer[256];

    // 4️⃣ Hlavná slučka klienta
    while (1) {
        // Načítanie vstupu od používateľa
        printf("Enter command (MOVE w / QUIT): ");
        fgets(buffer, sizeof(buffer), stdin);

        // Poslanie správy serveru
        send(sock, buffer, strlen(buffer), 0);

        // Prijatie odpovede od servera
        int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;

        buffer[n] = '\0';
        printf("Server says: %s", buffer);

        // Ak by sme chceli skončiť
        if (strncmp(buffer, "MSG", 3) == 0 && strstr(buffer, "QUIT"))
            break;
    }

    // 5️⃣ Zatvorenie spojenia
    close(sock);
    return 0;
}
