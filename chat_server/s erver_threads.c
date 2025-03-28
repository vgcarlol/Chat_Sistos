#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define MAX_CLIENTES 100
#define NOMBRE_LEN 50
#define MENSAJE_LEN 512
#define TIMEOUT_INACTIVIDAD 60 // segundos

typedef struct {
    int socket_fd;
    char nombre[NOMBRE_LEN];
    char ip[INET_ADDRSTRLEN];
    char estado[10]; // ACTIVO, OCUPADO, INACTIVO
    time_t ultima_actividad;
} Usuario;

Usuario* usuarios[MAX_CLIENTES];
pthread_mutex_t mutex_usuarios = PTHREAD_MUTEX_INITIALIZER;

void actualizar_estado_por_inactividad() {
    time_t ahora = time(NULL);
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (usuarios[i] && strcmp(usuarios[i]->estado, "INACTIVO") != 0) {
            double inactivo = difftime(ahora, usuarios[i]->ultima_actividad);
            if (inactivo > TIMEOUT_INACTIVIDAD) {
                strcpy(usuarios[i]->estado, "INACTIVO");
            }
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
}

void* monitor_inactividad(void* arg) {
    while (1) {
        sleep(5);
        actualizar_estado_por_inactividad();
    }
    return NULL;
}

void enviar(int fd, const char* msg) {
    send(fd, msg, strlen(msg), 0);
}

void broadcast(char* mensaje, int emisor_fd) {
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (usuarios[i] && usuarios[i]->socket_fd != emisor_fd) {
            enviar(usuarios[i]->socket_fd, mensaje);
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
}

void mensaje_directo(char* destino, char* mensaje, char* origen) {
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (usuarios[i] && strcmp(usuarios[i]->nombre, destino) == 0) {
            char buffer[NOMBRE_LEN + MENSAJE_LEN + 16];
            snprintf(buffer, sizeof(buffer), "[Privado de %s]: %s\n", origen, mensaje);
            enviar(usuarios[i]->socket_fd, buffer);
            pthread_mutex_unlock(&mutex_usuarios);
            return;
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
}

void eliminar_usuario(int fd) {
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (usuarios[i] && usuarios[i]->socket_fd == fd) {
            close(fd);
            free(usuarios[i]);
            usuarios[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
}

int nombre_duplicado(const char* nombre) {
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (usuarios[i] && strcmp(usuarios[i]->nombre, nombre) == 0) {
            return 1;
        }
    }
    return 0;
}

void listar_usuarios(int fd) {
    pthread_mutex_lock(&mutex_usuarios);
    enviar(fd, "Usuarios conectados:\n");
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (usuarios[i]) {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "- %s [%s]\n", usuarios[i]->nombre, usuarios[i]->estado);
            enviar(fd, buffer);
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
}

void info_usuario(int fd, char* nombre) {
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (usuarios[i] && strcmp(usuarios[i]->nombre, nombre) == 0) {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "%s está en estado %s con IP %s\n", nombre, usuarios[i]->estado, usuarios[i]->ip);
            enviar(fd, buffer);
            pthread_mutex_unlock(&mutex_usuarios);
            return;
        }
    }
    enviar(fd, "Usuario no encontrado.\n");
    pthread_mutex_unlock(&mutex_usuarios);
}

void cambiar_estado(int fd, char* estado) {
    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (usuarios[i] && usuarios[i]->socket_fd == fd) {
            strcpy(usuarios[i]->estado, estado);
            usuarios[i]->ultima_actividad = time(NULL);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);
}

void* manejar_cliente(void* arg) {
    int cliente_fd = *((int*)arg);
    free(arg);

    char buffer[MENSAJE_LEN];
    char nombre[NOMBRE_LEN];

    int len = recv(cliente_fd, nombre, NOMBRE_LEN, 0);
    if (len <= 0 || nombre_duplicado(nombre)) {
        enviar(cliente_fd, "Nombre de usuario inválido o duplicado.\n");
        close(cliente_fd);
        return NULL;
    }

    Usuario* nuevo = (Usuario*)malloc(sizeof(Usuario));
    nuevo->socket_fd = cliente_fd;
    strncpy(nuevo->nombre, nombre, NOMBRE_LEN);
    strcpy(nuevo->estado, "ACTIVO");
    nuevo->ultima_actividad = time(NULL);

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(cliente_fd, (struct sockaddr*)&addr, &addr_len);
    inet_ntop(AF_INET, &addr.sin_addr, nuevo->ip, sizeof(nuevo->ip));

    pthread_mutex_lock(&mutex_usuarios);
    for (int i = 0; i < MAX_CLIENTES; i++) {
        if (usuarios[i] == NULL) {
            usuarios[i] = nuevo;
            break;
        }
    }
    pthread_mutex_unlock(&mutex_usuarios);

    char msg[MENSAJE_LEN];
    snprintf(msg, sizeof(msg), "%s se ha unido al chat.\n", nombre);
    broadcast(msg, cliente_fd);

    while ((len = recv(cliente_fd, buffer, MENSAJE_LEN, 0)) > 0) {
        buffer[len] = '\0';
        nuevo->ultima_actividad = time(NULL);

        if (strncmp(buffer, "/usuarios", 9) == 0) {
            listar_usuarios(cliente_fd);
        } else if (strncmp(buffer, "/info ", 6) == 0) {
            info_usuario(cliente_fd, buffer + 6);
        } else if (strncmp(buffer, "/estado ", 8) == 0) {
            cambiar_estado(cliente_fd, buffer + 8);
        } else if (strncmp(buffer, "/salir", 6) == 0) {
            break;
        } else if (buffer[0] == '@') {
            char* espacio = strchr(buffer, ' ');
            if (espacio) {
                *espacio = '\0';
                char* destino = buffer + 1;
                char* mensaje = espacio + 1;
                mensaje_directo(destino, mensaje, nombre);
            }
        } else {
            char mensaje_final[NOMBRE_LEN + MENSAJE_LEN + 16];
            snprintf(mensaje_final, sizeof(mensaje_final), "%s: %s", nombre, buffer);
            broadcast(mensaje_final, cliente_fd);
        }
    }

    snprintf(msg, sizeof(msg), "%s ha salido del chat.\n", nombre);
    broadcast(msg, cliente_fd);
    eliminar_usuario(cliente_fd);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        exit(1);
    }

    int puerto = atoi(argv[1]);
    int servidor_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (servidor_fd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(servidor_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in servidor_addr;
    servidor_addr.sin_family = AF_INET;
    servidor_addr.sin_addr.s_addr = INADDR_ANY;
    servidor_addr.sin_port = htons(puerto);

    if (bind(servidor_fd, (struct sockaddr*)&servidor_addr, sizeof(servidor_addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(servidor_fd, 10) < 0) {
        perror("listen");
        exit(1);
    }

    pthread_t monitor;
    pthread_create(&monitor, NULL, monitor_inactividad, NULL);

    printf("Servidor escuchando en el puerto %d...\n", puerto);

    while (1) {
        struct sockaddr_in cliente_addr;
        socklen_t cliente_len = sizeof(cliente_addr);
        int* cliente_fd = malloc(sizeof(int));
        *cliente_fd = accept(servidor_fd, (struct sockaddr*)&cliente_addr, &cliente_len);
        if (*cliente_fd < 0) {
            perror("accept");
            free(cliente_fd);
            continue;
        }

        pthread_t tid;
        pthread_create(&tid, NULL, manejar_cliente, cliente_fd);
        pthread_detach(tid);
    }

    close(servidor_fd);
    return 0;
}
