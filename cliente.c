#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "protocolo.h"

int sock;
char username[32];

// Recibir mensajes
void *recibir(void *arg) {
    ChatPacket pkt;

    while (1) {
        int bytes = recv(sock, &pkt, sizeof(pkt), MSG_WAITALL);

        if (bytes <= 0) {
            printf("Desconectado del servidor\n");
            exit(0);
        }

        if (pkt.command == CMD_MSG) {
            printf("%s\n", pkt.payload);
        }
        
        else if (pkt.command == CMD_USER_LIST) {
            printf("Usuarios: %s\n", pkt.payload);
        }
        
        else if (pkt.command == CMD_USER_INFO) {
            printf("INFO: %s\n", pkt.payload);
        }
        
        else if (pkt.command == CMD_OK) {
            printf("Status actualizado: %s\n", pkt.payload);
        }
    }
}

int main() {
    struct sockaddr_in serv;

    printf("Username: ");
    fgets(username, 31, stdin);
    username[strcspn(username, "\n")] = 0;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    serv.sin_family = AF_INET;
    serv.sin_port = htons(5000);
    inet_pton(AF_INET, "127.0.0.1", &serv.sin_addr);

    connect(sock, (struct sockaddr*)&serv, sizeof(serv));

    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));

    // REGISTER
    pkt.command = CMD_REGISTER;
    strcpy(pkt.sender, username);

    send(sock, &pkt, sizeof(pkt), 0);

    pthread_t t;
    pthread_create(&t, NULL, recibir, NULL);

    while (1) {
        char input[1024];

        fgets(input, 1000, stdin);
        input[strcspn(input, "\n")] = 0;
        
        if (strcmp(input, "/exit") == 0) {
            memset(&pkt, 0, sizeof(pkt));
            pkt.command = CMD_LOGOUT;
            strcpy(pkt.sender, username);

            send(sock, &pkt, sizeof(pkt), 0);
            close(sock);
            exit(0);
        }

        memset(&pkt, 0, sizeof(pkt));

        // /msg usuario mensaje
        if (strncmp(input, "/msg ", 5) == 0) {
            pkt.command = CMD_DIRECT;

            char target[32], mensaje[900];
            if (sscanf(input + 5, "%s %[^\n]", target, mensaje) < 2) {
                printf("Uso: /msg usuario mensaje\n");
                continue;
            }

            strcpy(pkt.target, target);
            strcpy(pkt.payload, mensaje);
        }
        
        else if (strcmp(input, "/list") == 0) {
            pkt.command = CMD_LIST;
            pkt.payload[0] = '\0';
        }
        
        else if (strncmp(input, "/info ", 6) == 0) {
            pkt.command = CMD_INFO;

            char target[32];
            sscanf(input + 6, "%s", target);

            strcpy(pkt.target, target);
        }
        
        else if (strncmp(input, "/status ", 8) == 0) {
            pkt.command = CMD_STATUS;

            char estado[16];
            sscanf(input + 8, "%s", estado);
            
            if (strcmp(estado, "ACTIVE") != 0 &&
                strcmp(estado, "BUSY") != 0 &&
                strcmp(estado, "INACTIVE") != 0) {

                printf("Estado inválido. Usa ACTIVE, BUSY o INACTIVE\n");
                continue;
            }

            strcpy(pkt.payload, estado);
        }

        else {
            pkt.command = CMD_MSG;
            strcpy(pkt.payload, input);
        }

        strcpy(pkt.sender, username);
        pkt.payload_len = strlen(pkt.payload);

        send(sock, &pkt, sizeof(pkt), 0);
    }
}
