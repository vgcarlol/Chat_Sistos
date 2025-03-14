#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")  // Para que el compilador sepa enlazar la librería de Winsock
#else
    #include <unistd.h>
    #include <arpa/inet.h>
#endif
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

// Función para manejar la comunicación con cada cliente
void *handle_client(void *arg) {
    int sockfd = *(int *)arg;
    char buffer[BUFFER_SIZE];
    int bytes_received;

    printf("Cliente conectado: socket %d\n", sockfd);

    while ((bytes_received = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("Mensaje recibido del socket %d: %s\n", sockfd, buffer);
        // Aquí se implementará la lógica para procesar mensajes
    }

    if (bytes_received == 0) {
        printf("Cliente desconectado: socket %d\n", sockfd);
    } else {
        perror("recv");
    }
#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif
    free(arg);
    return NULL;
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    // Inicializar Winsock
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup falló.\n");
        return 1;
    }
#endif

    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    pthread_t tid;

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
        close(server_fd);
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
        close(server_fd);
#endif
        exit(EXIT_FAILURE);
    }

    // Bucle para aceptar conexiones
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Error en accept");
            break;
        }

        // Reservar memoria para el descriptor del socket y crear un thread
        int *pclient = malloc(sizeof(int));
        *pclient = new_socket;
        if (pthread_create(&tid, NULL, &handle_client, pclient) != 0) {
            perror("Error al crear thread");
            free(pclient);
        }
        pthread_detach(tid);  // Libera recursos del thread al finalizar
    }

#ifdef _WIN32
    closesocket(server_fd);
    WSACleanup();
#else
    close(server_fd);
#endif
    return 0;
}
