#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include "protocolo.h"

// Estructura Clientes, para guardar clientes
struct Cliente {
    char nombre[32];
    int socket;
    char status[16];
};


// Lista/Agenda de clientes ya conectados
struct Cliente lista[100]; //hasta 100 clientes
int num_clientes = 0;
pthread_mutex_t mutex_lista = PTHREAD_MUTEX_INITIALIZER; //para el race condition


// void de Clientes
void agregar_cliente(int client_fd) {
    pthread_mutex_lock(&mutex_lista);

    if (num_clientes < 100) {
        lista[num_clientes].socket = client_fd;
        strcpy(lista[num_clientes].nombre, "Anon");
        strcpy(lista[num_clientes].status, STATUS_ACTIVO);
        num_clientes++;

        printf("Cliente agregado. Total: %d\n", num_clientes);
    }

    pthread_mutex_unlock(&mutex_lista);
}


// void para Eliminar Clientes
void eliminar_cliente(int client_fd) {
    pthread_mutex_lock(&mutex_lista);

    for (int i = 0; i < num_clientes; i++) {
        if (lista[i].socket == client_fd) {
            lista[i] = lista[num_clientes - 1];
            num_clientes--;
            break;
        }
    }

    printf("Cliente eliminado. Total: %d\n", num_clientes);

    pthread_mutex_unlock(&mutex_lista);
}


//Guardar Cliente
void set_nombre(int client_fd, char *nombre) {
    pthread_mutex_lock(&mutex_lista);

    for (int i = 0; i < num_clientes; i++) {
        if (lista[i].socket == client_fd) {
            strncpy(lista[i].nombre, nombre, 31);
            break;
        }
    }

    pthread_mutex_unlock(&mutex_lista);
}


// void Cambiar Status
void set_status(int client_fd, char *status) {
    pthread_mutex_lock(&mutex_lista);

    for (int i = 0; i < num_clientes; i++) {
        if (lista[i].socket == client_fd) {
            strncpy(lista[i].status, status, 15);
            lista[i].status[15] = '\0';
            break;
        }
    }

    pthread_mutex_unlock(&mutex_lista);
}

// Buscar usuario
int buscar_cliente_por_nombre(char *nombre) {
    int resultado = -1;

    for (int i = 0; i < num_clientes; i++) {
        if (strcmp(lista[i].nombre, nombre) == 0) {
            resultado = lista[i].socket;
            break;
        }
    }

    return resultado;
}

// void Enviar msg Directo
void enviar_directo(ChatPacket *pkt, int sender_fd) {
    pthread_mutex_lock(&mutex_lista);

    int sock = buscar_cliente_por_nombre(pkt->target);

    if (sock != -1) {
        ChatPacket nuevo;
        memset(&nuevo, 0, sizeof(nuevo));

        nuevo.command = CMD_MSG;
        strncpy(nuevo.sender, pkt->sender, 31);
        strncpy(nuevo.target, pkt->target, 31);
        
        nuevo.sender[31] = '\0';
        nuevo.target[31] = '\0';

        snprintf(nuevo.payload, sizeof(nuevo.payload),
         "[%.20s -> %.20s] %.900s",
         pkt->sender, pkt->target, pkt->payload);
         
         nuevo.payload_len = strlen(nuevo.payload);

        send(sock, &nuevo, sizeof(nuevo), 0);
    } else {
        ChatPacket err;
        memset(&err, 0, sizeof(err));

        err.command = CMD_ERROR;
        strcpy(err.payload, "Usuario no encontrado");
        err.payload_len = strlen(err.payload);

        send(sender_fd, &err, sizeof(err), 0);
    }

    pthread_mutex_unlock(&mutex_lista);
}


// Broadcast (mensajes a todos menos el que lo envió)
void broadcast(char *mensaje, int sender_fd, char *sender_nombre) {
    pthread_mutex_lock(&mutex_lista);

    for (int i = 0; i < num_clientes; i++) {
        int sock = lista[i].socket;

        if (sock != sender_fd) { // no se envía a sí mismo
            ChatPacket pkt;
            memset(&pkt, 0, sizeof(pkt));

            pkt.command = CMD_MSG;
            strncpy(pkt.sender, sender_nombre, 31);
            strcpy(pkt.target, "ALL");
            strncpy(pkt.payload, mensaje, 956);
            
            pkt.payload_len = strlen(pkt.payload);

            send(sock, &pkt, sizeof(pkt), 0);
        }
        
    }

    pthread_mutex_unlock(&mutex_lista);
}


// void List
void enviar_lista(int client_fd) {
    pthread_mutex_lock(&mutex_lista);

    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.command = CMD_USER_LIST;

    char buffer[957] = "";

    for (int i = 0; i < num_clientes; i++) {
        char temp[64];
        snprintf(temp, sizeof(temp), "%s,%s;", lista[i].nombre, lista[i].status);

        if (strlen(buffer) + strlen(temp) < sizeof(buffer) - 1) {
            strcat(buffer, temp);
        }
    }

    strncpy(pkt.payload, buffer, 956);
    pkt.payload_len = strlen(pkt.payload);

    send(client_fd, &pkt, sizeof(pkt), 0);

    pthread_mutex_unlock(&mutex_lista);
}


// void Info
void enviar_info(int client_fd, char *target) {
    pthread_mutex_lock(&mutex_lista);

    ChatPacket pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.command = CMD_USER_INFO;

    for (int i = 0; i < num_clientes; i++) {
        if (strcmp(lista[i].nombre, target) == 0) {
            snprintf(pkt.payload, sizeof(pkt.payload),
                     "IP: socket %d, STATUS: %s",
                     lista[i].socket,
                     lista[i].status);
            pkt.payload_len = strlen(pkt.payload);
            send(client_fd, &pkt, sizeof(pkt), 0);
            pthread_mutex_unlock(&mutex_lista);
            return;
        }
    }

    strcpy(pkt.payload, "Usuario no encontrado");
    pkt.payload_len = strlen(pkt.payload);
    send(client_fd, &pkt, sizeof(pkt), 0);

    pthread_mutex_unlock(&mutex_lista);
}



// void de Threads
void *handle_client(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);

    printf("Thread creado para cliente %d\n", client_fd);

    ChatPacket pkt;

    // 1. Recibir Register
    int bytes = recv(client_fd, &pkt, sizeof(pkt), MSG_WAITALL);

    if (bytes <= 0) {
        close(client_fd);
        return NULL;
    }

    if (pkt.command != CMD_REGISTER) {
        close(client_fd);
        return NULL;
    }

    // 2. Guardar Nombre
    char nombre[32];
    strncpy(nombre, pkt.sender, 31);
    nombre[31] = '\0';

    set_nombre(client_fd, nombre);

    printf("\n[+] %s\n\n", nombre);

    // 3. Loop Principal
    while (1) {
        bytes = recv(client_fd, &pkt, sizeof(pkt), MSG_WAITALL);

        if (bytes <= 0) {
            printf("\n[-] %s salió\n\n", nombre);
            eliminar_cliente(client_fd);
            break;
        }

        // 4. Solo Procesar Mensajes
        if (pkt.command == CMD_MSG) {
            char mensaje[1100];
            sprintf(mensaje, "[%s] %s", pkt.sender, pkt.payload);

            broadcast(mensaje, client_fd, pkt.sender);
        }

        else if (pkt.command == CMD_DIRECT) {
            enviar_directo(&pkt, client_fd);
        }
        
        else if (pkt.command == CMD_LIST) {
            enviar_lista(client_fd);
        }
        
        else if (pkt.command == CMD_INFO) {
            enviar_info(client_fd, pkt.target);
        }
        
        else if (pkt.command == CMD_STATUS) {
            set_status(client_fd, pkt.payload);

            ChatPacket resp;
            memset(&resp, 0, sizeof(resp));

            resp.command = CMD_OK;
            strncpy(resp.payload, pkt.payload, 956);
            resp.payload_len = strlen(resp.payload);

            send(client_fd, &resp, sizeof(resp), 0);
        }
        
        else if (pkt.command == CMD_LOGOUT) {
            printf("\n[-] %s salió\n\n", nombre);
            eliminar_cliente(client_fd);
            break;
        }
    }

    close(client_fd);
    return NULL;
}


// Main
int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // 1. Crear socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    printf("Socket creado\n");

    // 2. Configurar dirección
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // cualquier IP
    address.sin_port = htons(5000);       // puerto 5000

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. Bind
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    printf("Bind hecho (puerto 5000)\n");

    // 4. Listen
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Esperando conexiones...\n");

    // 5. Accept
    while (1) {
    client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    
    if (client_fd < 0) {
        perror("accept");
        continue;
    }

    printf("Cliente conectado!\n");
    
    // Agregar Cliente
    agregar_cliente(client_fd);

    // Threads del cliente
    pthread_t tid;
    int *new_sock = malloc(sizeof(int));
    *new_sock = client_fd;

    pthread_create(&tid, NULL, handle_client, new_sock);
}

    close(client_fd);
    close(server_fd);

    return 0;
}
