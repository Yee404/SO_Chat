#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#include "protocolo.h"

#define BUFFER 1024

int sock_global;
char username_global[32];

// THREAD: recibir mensajes
void *recibir_mensajes(void *arg) {
    ChatPacket pkt;

    while (1) {
        int bytes = recv(sock_global, &pkt, sizeof(pkt), MSG_WAITALL);

        if (bytes <= 0) {
            printf("\n[SERVER]: Desconectado\n");
            exit(0);
        }

        // FORMATO DE MENSAJES
        if (pkt.command == CMD_MSG) {

            if (strcmp(pkt.sender, "SERVER") == 0) {
                printf("\n[SERVER]: %s\n", pkt.payload);
            }
            else if (strcmp(pkt.target, username_global) == 0) {
                printf("\n[PRIVATE][%s]: %s\n", pkt.sender, pkt.payload);
            }
            else {
                printf("\n[%s]: %s\n", pkt.sender, pkt.payload);
            }
        }

        else if (pkt.command == CMD_OK) {
            printf("\n[SERVER]: %s\n", pkt.payload);
        }

        else if (pkt.command == CMD_ERROR) {
            printf("\n[ERROR]: %s\n", pkt.payload);
        }

        else if (pkt.command == CMD_USER_LIST) {
            printf("\n[USUARIOS]: %s\n", pkt.payload);
        }

        else if (pkt.command == CMD_USER_INFO) {
            printf("\n[INFO]: %s\n", pkt.payload);
        }

        else if (pkt.command == CMD_DISCONNECTED) {
            printf("\n[SERVER]: %s se desconectó\n", pkt.payload);
        }

        printf(">> ");
        fflush(stdout);
    }
}

// Enviar paquete
void enviar_paquete(ChatPacket *pkt) {
    if (send(sock_global, pkt, sizeof(ChatPacket), 0) < 0) {
        perror("Error al enviar");
    }
}

int main(int argc, char *argv[]) {

    if (argc != 4) {
        printf("Uso: %s <username> <IP> <puerto>\n", argv[0]);
        return 1;
    }

    char *username = argv[1];
    char *ip = argv[2];
    int puerto = atoi(argv[3]);

    strncpy(username_global, username, 31);

    // Crear socket
    sock_global = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_global < 0) {
        perror("Error socket");
        return 1;
    }

    // Configurar servidor
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(puerto);

    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
        perror("IP inválida");
        return 1;
    }

    // Conectar
    if (connect(sock_global, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error al conectar");
        return 1;
    }

    printf("Conectado al servidor\n");

    // REGISTER
    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.command = CMD_REGISTER;
    strncpy(pkt.sender, username, 31);
    strncpy(pkt.payload, username, 956);
    pkt.payload_len = strlen(pkt.payload);

    enviar_paquete(&pkt);

    // Crear thread
    pthread_t thread;
    pthread_create(&thread, NULL, recibir_mensajes, NULL);

    char input[BUFFER];

    while (1) {
        printf(">> ");
        fgets(input, BUFFER, stdin);

        // quitar salto de línea
        input[strcspn(input, "\n")] = 0;

        // evitar input vacío
        if (strlen(input) == 0) continue;

        memset(&pkt, 0, sizeof(pkt));
        strncpy(pkt.sender, username, 31);

        // /exit
        if (strcmp(input, "/exit") == 0) {
            pkt.command = CMD_LOGOUT;
            enviar_paquete(&pkt);
            printf("Saliendo...\n");
            break;
        }

        // /list
        else if (strcmp(input, "/list") == 0) {
            pkt.command = CMD_LIST;
            enviar_paquete(&pkt);
        }

        // /info
        else if (strncmp(input, "/info ", 6) == 0) {
           if (strlen(input + 6) == 0) {
                printf("Uso: /info <usuario>\n");
                continue;
            }

            pkt.command = CMD_INFO;
            strncpy(pkt.target, input + 6, 31);
            enviar_paquete(&pkt);
        }

        // /status
        else if (strncmp(input, "/status ", 8) == 0) {
            char *status = input + 8;

            if (strcmp(status, "ACTIVE") != 0 &&
                strcmp(status, "BUSY") != 0 &&
                strcmp(status, "INACTIVE") != 0) {

                printf("Status inválido\n");
                continue;
            }

            pkt.command = CMD_STATUS;
            strncpy(pkt.payload, status, 15);
            pkt.payload_len = strlen(pkt.payload);
            enviar_paquete(&pkt);
        }

        // /msg
        else if (strncmp(input, "/msg ", 5) == 0) {

            char *token = strtok(input + 5, " ");
            if (token == NULL) {
                printf("Uso: /msg <usuario> <mensaje>\n");
                continue;
            }

            strncpy(pkt.target, token, 31);

            char *mensaje = strtok(NULL, "");
            if (mensaje == NULL || strlen(mensaje) == 0) {
                printf("Mensaje vacío\n");
                continue;
            }

            pkt.command = CMD_DIRECT;
            strncpy(pkt.payload, mensaje, 956);
            pkt.payload_len = strlen(pkt.payload);

            enviar_paquete(&pkt);
        }

        // /broadcast
        else if (strncmp(input, "/broadcast ", 11) == 0) {

            if (strlen(input + 11) == 0) {
                printf("Mensaje vacío\n");
                continue;
            }

            pkt.command = CMD_BROADCAST;
            strncpy(pkt.payload, input + 11, 956);
            pkt.payload_len = strlen(pkt.payload);

            enviar_paquete(&pkt);
        }

        else {
            printf("Comando no reconocido\n");
        }
    }

    close(sock_global);
    return 0;
}