#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSESOCKET closesocket
#else
    #include <arpa/inet.h>
    #include <unistd.h>
    #define CLOSESOCKET close
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

// Estados posibles
#define STATUS_ACTIVE   "ACTIVO"
#define STATUS_BUSY     "OCUPADO"
#define STATUS_INACTIVE "INACTIVO"

// Estructura para almacenar la información de cada cliente
typedef struct Client {
    int socket;
    char name[50];
    char ip[INET_ADDRSTRLEN];   // "xxx.xxx.xxx.xxx"
    char status[10];            // ACTIVO, OCUPADO, INACTIVO
    struct Client *next;
} Client;

// Lista global de clientes
Client *clients = NULL;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// ---------------------------------------------------------
// Funciones Auxiliares
// ---------------------------------------------------------

// Agregar cliente a la lista global
void add_client(Client *new_client) {
    pthread_mutex_lock(&clients_mutex);

    new_client->next = clients;
    clients = new_client;

    pthread_mutex_unlock(&clients_mutex);
}

// Eliminar cliente de la lista global
void remove_client(int socket) {
    pthread_mutex_lock(&clients_mutex);

    Client *prev = NULL;
    Client *curr = clients;

    while (curr != NULL) {
        if (curr->socket == socket) {
            if (prev == NULL) {
                clients = curr->next;
            } else {
                prev->next = curr->next;
            }
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    pthread_mutex_unlock(&clients_mutex);
}

// Buscar cliente por nombre de usuario
Client* find_client_by_name(const char *name) {
    pthread_mutex_lock(&clients_mutex);

    Client *curr = clients;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return curr;
        }
        curr = curr->next;
    }

    pthread_mutex_unlock(&clients_mutex);
    return NULL;
}

// Enviar un mensaje a un socket
void send_msg(int socket, const char *msg) {
    send(socket, msg, strlen(msg), 0);
}

// Broadcasting: enviar msg a todos menos al emisor
void broadcast_message(const char *msg, int sender_socket) {
    pthread_mutex_lock(&clients_mutex);

    Client *curr = clients;
    while (curr != NULL) {
        if (curr->socket != sender_socket) {
            send_msg(curr->socket, msg);
        }
        curr = curr->next;
    }

    pthread_mutex_unlock(&clients_mutex);
}

// Mensaje privado
void send_private_message(const char *target_name, const char *msg, int sender_socket) {
    Client *target = find_client_by_name(target_name);
    if (target) {
        char buffer[BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "[Privado de %d]: %s\n", sender_socket, msg);
        send_msg(target->socket, buffer);
    } else {
        // Enviar error a quien envió el mensaje
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, sizeof(error_msg), "Usuario %s no encontrado.\n", target_name);
        send_msg(sender_socket, error_msg);
    }
}

// Listar usuarios
void list_users(int requester_socket) {
    pthread_mutex_lock(&clients_mutex);

    Client *curr = clients;
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    strcat(buffer, "Usuarios conectados:\n");
    while (curr != NULL) {
        char line[100];
        snprintf(line, sizeof(line), " - %s (%s)\n", curr->name, curr->status);
        strcat(buffer, line);
        curr = curr->next;
    }

    pthread_mutex_unlock(&clients_mutex);

    send_msg(requester_socket, buffer);
}

// Cambiar estado
void set_status(int socket, const char *new_status) {
    pthread_mutex_lock(&clients_mutex);

    Client *curr = clients;
    while (curr != NULL) {
        if (curr->socket == socket) {
            strncpy(curr->status, new_status, sizeof(curr->status));
            curr->status[sizeof(curr->status) - 1] = '\0';
            break;
        }
        curr = curr->next;
    }

    pthread_mutex_unlock(&clients_mutex);
}

// Obtener información de un usuario
void get_user_info(const char *username, int requester_socket) {
    Client *user = find_client_by_name(username);
    if (user) {
        char buffer[BUFFER_SIZE];
        snprintf(buffer, sizeof(buffer), "Info de %s: IP=%s, STATUS=%s\n", 
                 user->name, user->ip, user->status);
        send_msg(requester_socket, buffer);
    } else {
        char error_msg[BUFFER_SIZE];
        snprintf(error_msg, sizeof(error_msg), "Usuario %s no encontrado.\n", username);
        send_msg(requester_socket, error_msg);
    }
}

// ---------------------------------------------------------
// Función para manejar la comunicación con cada cliente
// ---------------------------------------------------------
void *handle_client(void *arg) {
    int sockfd = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    int bytes_received;
    char welcome_msg[BUFFER_SIZE];

    // 1. Pedir nombre de usuario
    send_msg(sockfd, "Bienvenido. Ingresa tu nombre de usuario:\n");
    bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received <= 0) {
        CLOSESOCKET(sockfd);
        return NULL;
    }
    buffer[bytes_received] = '\0';
    // Quitar salto de línea si lo hay
    buffer[strcspn(buffer, "\r\n")] = 0;
    char username[50];
    strncpy(username, buffer, sizeof(username));
    username[sizeof(username) - 1] = '\0';

    // Verificar si el nombre ya está en uso
    if (find_client_by_name(username)) {
        send_msg(sockfd, "ERROR: Nombre de usuario en uso. Conexión cerrada.\n");
        CLOSESOCKET(sockfd);
        return NULL;
    }

    // 2. Crear estructura Client y agregarla a la lista global
    Client *new_client = (Client *)malloc(sizeof(Client));
    new_client->socket = sockfd;
    strncpy(new_client->name, username, sizeof(new_client->name));
    new_client->name[sizeof(new_client->name) - 1] = '\0';
    strcpy(new_client->ip, "127.0.0.1");
    strcpy(new_client->status, STATUS_ACTIVE);
    new_client->next = NULL;
    add_client(new_client);

    snprintf(welcome_msg, sizeof(welcome_msg), "¡Hola %s! Estás conectado.\n", username);
    send_msg(sockfd, welcome_msg);

    // 3. Bucle principal de recepción de mensajes
    while ((bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        // Quitar salto de línea
        buffer[strcspn(buffer, "\r\n")] = 0;

        // Comandos posibles (simples ejemplos):
        // /broadcast <mensaje>
        // /msg <usuario> <mensaje>
        // /list
        // /status <ACTIVO|OCUPADO|INACTIVO>
        // /info <usuario>
        // /exit

        if (strncmp(buffer, "/broadcast ", 11) == 0) {
            char *msg = buffer + 11;
            char out[BUFFER_SIZE];
            snprintf(out, sizeof(out), "[%s (BCAST)]: %s\n", username, msg);
            broadcast_message(out, sockfd);

        } else if (strncmp(buffer, "/msg ", 5) == 0) {
            // Formato: /msg <usuario> <mensaje>
            // Ejemplo: /msg Juan Hola, Juan
            char *space = strchr(buffer + 5, ' ');
            if (!space) {
                send_msg(sockfd, "Uso: /msg <usuario> <mensaje>\n");
                continue;
            }
            *space = '\0';
            char *target_name = buffer + 5;
            char *msg = space + 1;

            char out[BUFFER_SIZE];
            snprintf(out, sizeof(out), "%s (privado): %s\n", username, msg);
            send_private_message(target_name, out, sockfd);

        } else if (strncmp(buffer, "/list", 5) == 0) {
            list_users(sockfd);

        } else if (strncmp(buffer, "/status ", 8) == 0) {
            char *new_status = buffer + 8;
            if (strcmp(new_status, STATUS_ACTIVE) == 0 ||
                strcmp(new_status, STATUS_BUSY) == 0 ||
                strcmp(new_status, STATUS_INACTIVE) == 0) {
                set_status(sockfd, new_status);
                send_msg(sockfd, "Estado actualizado.\n");
            } else {
                send_msg(sockfd, "Estado inválido. Use ACTIVO, OCUPADO o INACTIVO.\n");
            }

        } else if (strncmp(buffer, "/info ", 6) == 0) {
            char *target_name = buffer + 6;
            get_user_info(target_name, sockfd);

        } else if (strcmp(buffer, "/exit") == 0) {
            send_msg(sockfd, "Adiós.\n");
            break;
        } else {
            // Mensaje no reconocido o chat libre
            char out[BUFFER_SIZE];
            snprintf(out, sizeof(out), "[%s]: %s\n", username, buffer);
            broadcast_message(out, sockfd);
        }
    }

    // 4. El usuario se desconectó o hubo error
    printf("El usuario %s se ha desconectado.\n", username);
    remove_client(sockfd);
    CLOSESOCKET(sockfd);
    return NULL;
}

// ---------------------------------------------------------
// Función Principal (main)
// ---------------------------------------------------------
int main() {
#ifdef _WIN32
    // Inicializar Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup falló.\n");
        return 1;
    }
#endif

    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Crear el socket
#ifdef _WIN32
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        fprintf(stderr, "Error al crear el socket.\n");
        WSACleanup();
        exit(EXIT_FAILURE);
    }
#else
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Error al crear el socket");
        exit(EXIT_FAILURE);
    }
#endif

    // Configurar la dirección del servidor
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Vincular el socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Error en bind");
#ifdef _WIN32
        closesocket(server_fd);
        WSACleanup();
#else
        CLOSESOCKET(server_fd);
#endif
        exit(EXIT_FAILURE);
    }
    printf("Servidor iniciado en el puerto %d\n", PORT);

    // Escuchar conexiones entrantes
    if (listen(server_fd, 3) < 0) {
        perror("Error en listen");
#ifdef _WIN32
        closesocket(server_fd);
        WSACleanup();
#else
        CLOSESOCKET(server_fd);
#endif
        exit(EXIT_FAILURE);
    }

    // Bucle para aceptar conexiones
    while (1) {
        int new_socket;
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Error en accept");
            break;
        }

        // Crear hilo para manejar al cliente
        pthread_t tid;
        int *pclient = malloc(sizeof(int));
        *pclient = new_socket;
        if (pthread_create(&tid, NULL, &handle_client, pclient) != 0) {
            perror("Error al crear thread");
            free(pclient);
        }
        pthread_detach(tid);
    }

#ifdef _WIN32
    closesocket(server_fd);
    WSACleanup();
#else
    CLOSESOCKET(server_fd);
#endif

    return 0;
}
