#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "protocolo.h"

#define MAX_CLIENTES 100

typedef struct {
    char username[32];
    char status[16];
    int sockfd;
} Cliente;

Cliente lista[MAX_CLIENTES];
int num_clientes = 0;
pthread_mutex_t mutex_lista = PTHREAD_MUTEX_INITIALIZER;

// buscar cliente por nombre
int buscar_cliente(char *nombre) {
    for (int i = 0; i < num_clientes; i++) {
        if (strcmp(lista[i].username, nombre) == 0) {
            return i;
        }
    }
    return -1;
}

// agregar cliente
void agregar_cliente(int sockfd) {
    pthread_mutex_lock(&mutex_lista);

    if (num_clientes < MAX_CLIENTES) {
        lista[num_clientes].sockfd = sockfd;
        strcpy(lista[num_clientes].username, "");
        strcpy(lista[num_clientes].status, STATUS_ACTIVO);
        num_clientes++;
    }

    pthread_mutex_unlock(&mutex_lista);
}

// eliminar cliente
void eliminar_cliente(int sockfd) {
    pthread_mutex_lock(&mutex_lista);

    for (int i = 0; i < num_clientes; i++) {
        if (lista[i].sockfd == sockfd) {
            lista[i] = lista[num_clientes - 1];
            num_clientes--;
            break;
        }
    }

    pthread_mutex_unlock(&mutex_lista);
}

// enviar a todos
void broadcast(ChatPacket *pkt) {
    pthread_mutex_lock(&mutex_lista);

    for (int i = 0; i < num_clientes; i++) {
        send(lista[i].sockfd, pkt, sizeof(ChatPacket), 0);
    }

    pthread_mutex_unlock(&mutex_lista);
}

// enviar directo
void enviar_directo(ChatPacket *pkt, int sender_fd) {
    pthread_mutex_lock(&mutex_lista);

    int index = buscar_cliente(pkt->target);

    if (index != -1) {

        ChatPacket resp;
        memset(&resp, 0, sizeof(resp));

        // respetar protocolo
        resp.command = CMD_MSG;

        // QUIÉN ENVÍA
        strncpy(resp.sender, pkt->sender, 31);
        resp.sender[31] = '\0';

        // A QUIÉN VA
        strncpy(resp.target, pkt->target, 31);
        resp.target[31] = '\0';

        // MENSAJE
        strncpy(resp.payload, pkt->payload, 956);
        resp.payload[956] = '\0';

        resp.payload_len = strlen(resp.payload);

        // enviar al destinatario
        send(lista[index].sockfd, &resp, sizeof(resp), 0);

    } else {
        // error si no existe usuario
        ChatPacket err;
        memset(&err, 0, sizeof(err));

        err.command = CMD_ERROR;
        strcpy(err.sender, "SERVER");
        strcpy(err.payload, "Usuario no conectado");
        err.payload_len = strlen(err.payload);

        send(sender_fd, &err, sizeof(err), 0);
    }

    pthread_mutex_unlock(&mutex_lista);
}
// lista de usuarios
void enviar_lista(int sockfd) {
    pthread_mutex_lock(&mutex_lista);

    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.command = CMD_USER_LIST;
    strcpy(pkt.sender, "SERVER");

    char buffer[957] = "";

    for (int i = 0; i < num_clientes; i++) {
        char temp[64];
        snprintf(temp, sizeof(temp), "%s,%s;", lista[i].username, lista[i].status);
        strcat(buffer, temp);
    }

    strncpy(pkt.payload, buffer, 956);
    pkt.payload_len = strlen(pkt.payload);

    send(sockfd, &pkt, sizeof(pkt), 0);

    pthread_mutex_unlock(&mutex_lista);
}

// info usuario
void enviar_info(int sockfd, char *target) {
    pthread_mutex_lock(&mutex_lista);

    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.command = CMD_USER_INFO;
    strcpy(pkt.sender, "SERVER");

    int index = buscar_cliente(target);

    if (index != -1) {
        snprintf(pkt.payload, sizeof(pkt.payload),
                 "Usuario: %s, STATUS: %s",
                 lista[index].username,
                 lista[index].status);
    } else {
        strcpy(pkt.payload, "Usuario no conectado");
    }

    pkt.payload_len = strlen(pkt.payload);
    send(sockfd, &pkt, sizeof(pkt), 0);

    pthread_mutex_unlock(&mutex_lista);
}

// manejar cliente
void *handle_client(void *arg) {
    int sockfd = *(int*)arg;
    free(arg);

    ChatPacket pkt;

    // REGISTER
    if (recv(sockfd, &pkt, sizeof(pkt), MSG_WAITALL) <= 0) {
        close(sockfd);
        return NULL;
    }

    if (pkt.command != CMD_REGISTER) {
        close(sockfd);
        return NULL;
    }

    pthread_mutex_lock(&mutex_lista);

    // verificar duplicado
    if (buscar_cliente(pkt.sender) != -1) {
        ChatPacket err;
        memset(&err, 0, sizeof(err));

        err.command = CMD_ERROR;
        strcpy(err.sender, "SERVER");
        strcpy(err.payload, "Usuario ya existe");
        err.payload_len = strlen(err.payload);

        send(sockfd, &err, sizeof(err), 0);

        pthread_mutex_unlock(&mutex_lista);
        close(sockfd);
        return NULL;
    }

    // guardar nombre
    for (int i = 0; i < num_clientes; i++) {
        if (lista[i].sockfd == sockfd) {
            strncpy(lista[i].username, pkt.sender, 31);
        }
    }

    pthread_mutex_unlock(&mutex_lista);

    // responder OK
    ChatPacket ok;
    memset(&ok, 0, sizeof(ok));

    ok.command = CMD_OK;
    strcpy(ok.sender, "SERVER");
    snprintf(ok.payload, sizeof(ok.payload), "Bienvenido %s", pkt.sender);
    ok.payload_len = strlen(ok.payload);

    send(sockfd, &ok, sizeof(ok), 0);

    // LOOP
    while (1) {
        int bytes = recv(sockfd, &pkt, sizeof(pkt), MSG_WAITALL);

        if (bytes <= 0) break;

        if (pkt.command == CMD_BROADCAST) {
            ChatPacket msg;
            memset(&msg, 0, sizeof(msg));

            msg.command = CMD_MSG;
            strncpy(msg.sender, pkt.sender, 31);
            strcpy(msg.target, "ALL");
            strncpy(msg.payload, pkt.payload, 956);
            msg.payload_len = strlen(msg.payload);

            broadcast(&msg);
        }

        else if (pkt.command == CMD_DIRECT) {
            enviar_directo(&pkt, sockfd);
        }

        else if (pkt.command == CMD_LIST) {
            enviar_lista(sockfd);
        }

        else if (pkt.command == CMD_INFO) {
            enviar_info(sockfd, pkt.target);
        }

        else if (pkt.command == CMD_STATUS) {
            pthread_mutex_lock(&mutex_lista);

            for (int i = 0; i < num_clientes; i++) {
                if (lista[i].sockfd == sockfd) {
                    strncpy(lista[i].status, pkt.payload, 15);
                }
            }

            pthread_mutex_unlock(&mutex_lista);

            ChatPacket resp;
            memset(&resp, 0, sizeof(resp));

            resp.command = CMD_OK;
            strcpy(resp.sender, "SERVER");
            strncpy(resp.payload, pkt.payload, 956);
            resp.payload_len = strlen(resp.payload);

            send(sockfd, &resp, sizeof(resp), 0);
        }

        else if (pkt.command == CMD_LOGOUT) {
            break;
        }
    }

    eliminar_cliente(sockfd);
    close(sockfd);
    return NULL;
}

// MAIN
int main(int argc, char *argv[]) {

    if (argc != 2) {
        printf("Uso: %s <puerto>\n", argv[0]);
        return 1;
    }

    int puerto = atoi(argv[1]);

    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(puerto);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    printf("Servidor escuchando en puerto %d\n", puerto);

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);

        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        agregar_cliente(client_fd);

        pthread_t tid;
        int *new_sock = malloc(sizeof(int));
        *new_sock = client_fd;

        pthread_create(&tid, NULL, handle_client, new_sock);
        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}