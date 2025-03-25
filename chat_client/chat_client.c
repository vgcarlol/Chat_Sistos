/*
 * chat_client.c
 *
 * Cliente de Chat en C usando WebSockets, libwebsockets y ncurses.
 * Compilar con:
 *   gcc chat_client.c -o chat_client -lwebsockets -lncurses -lpthread
 *
 * Uso:
 *   ./chat_client <nombre_usuario> <IP_del_servidor> <puerto>
 *
 * Este código implementa una interfaz visual con ncurses, en la que se muestran
 * los mensajes recibidos y se permite escribir mensajes. Se utiliza libwebsockets
 * para el establecimiento de la conexión WebSocket y se formatean los mensajes en JSON
 * siguiendo el protocolo definido.
 *
 * Basado en la "Definición de proyecto Chat 2025" :contentReference[oaicite:0]{index=0}&#8203;:contentReference[oaicite:1]{index=1} y el "Protocolo con WebSockets" :contentReference[oaicite:2]{index=2}&#8203;:contentReference[oaicite:3]{index=3}.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <time.h>
 #include <pthread.h>
 #include <ncurses.h>
 #include <libwebsockets.h>
 
 #define MAX_MESSAGE_SIZE 1024
 #define SEND_BUFFER_SIZE 2048
 
 // Estructuras globales y variables de sincronización
 static pthread_mutex_t send_mutex = PTHREAD_MUTEX_INITIALIZER;
 static char send_buffer[SEND_BUFFER_SIZE] = {0};
 static int send_pending = 0;   // Bandera que indica que hay un mensaje para enviar
 
 // Variables para la interfaz ncurses
 WINDOW *chat_win, *input_win;
 pthread_mutex_t ncurses_mutex = PTHREAD_MUTEX_INITIALIZER;
 
 // Variables globales de libwebsockets
 static struct lws *client_wsi = NULL;
 static struct lws_context *context = NULL;
 static volatile int force_exit = 0;
 
 // Datos de usuario
 static char username[128] = {0};
 
 // Función para obtener la hora actual en formato ISO8601 (simplificado)
 void get_timestamp(char *buffer, size_t len) {
     time_t now = time(NULL);
     struct tm *tm_info = localtime(&now);
     strftime(buffer, len, "%Y-%m-%dT%H:%M:%S", tm_info);
 }
 
 // Función para refrescar la ventana del chat (mostrando el mensaje recibido)
 void display_message(const char *msg) {
     pthread_mutex_lock(&ncurses_mutex);
     // Imprime el mensaje en chat_win y desplaza si es necesario
     wprintw(chat_win, "%s\n", msg);
     wrefresh(chat_win);
     pthread_mutex_unlock(&ncurses_mutex);
 }
 
 // Callback de libwebsockets
 static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                          void *user, void *in, size_t len)
 {
     switch (reason) {
         case LWS_CALLBACK_CLIENT_ESTABLISHED:
             display_message("Conexión establecida con el servidor WebSocket");
             client_wsi = wsi;
             // Tras conectar, enviar el mensaje de registro
             {
                 char msg[SEND_BUFFER_SIZE];
                 char timestamp[64];
                 get_timestamp(timestamp, sizeof(timestamp));
                 snprintf(msg, sizeof(msg),
                          "{\"type\": \"register\", \"sender\": \"%s\", \"content\": null, \"timestamp\": \"%s\"}",
                          username, timestamp);
                 pthread_mutex_lock(&send_mutex);
                 strncpy(send_buffer, msg, sizeof(send_buffer)-1);
                 send_pending = 1;
                 pthread_mutex_unlock(&send_mutex);
                 lws_callback_on_writable(wsi);
             }
             break;
 
         case LWS_CALLBACK_CLIENT_RECEIVE:
             {
                 // Mostrar mensaje recibido en la ventana del chat
                 char *msg = (char *)malloc(len + 1);
                 if(msg) {
                     memcpy(msg, in, len);
                     msg[len] = '\0';
                     display_message(msg);
                     free(msg);
                 }
             }
             break;
 
         case LWS_CALLBACK_CLIENT_WRITEABLE:
             {
                 pthread_mutex_lock(&send_mutex);
                 if(send_pending) {
                     unsigned char buf[LWS_PRE + SEND_BUFFER_SIZE];
                     size_t n = strlen(send_buffer);
                     memcpy(&buf[LWS_PRE], send_buffer, n);
                     // Escribir mensaje y resetear la bandera
                     int m = lws_write(wsi, &buf[LWS_PRE], n, LWS_WRITE_TEXT);
                     if(m < (int)n) {
                         display_message("Error al enviar mensaje");
                     }
                     send_pending = 0;
                 }
                 pthread_mutex_unlock(&send_mutex);
             }
             break;
 
         case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
             display_message("Error en la conexión con el servidor");
             force_exit = 1;
             break;
 
         case LWS_CALLBACK_CLOSED:
             display_message("Conexión cerrada");
             force_exit = 1;
             break;
 
         default:
             break;
     }
     return 0;
 }
 
 // Lista de protocolos
 static struct lws_protocols protocols[] = {
     {
         "chat-protocol",
         callback_chat,
         0, // Tamaño del user data (no usado aquí)
         SEND_BUFFER_SIZE,
     },
     { NULL, NULL, 0, 0 } // Terminador
 };
 
 // Hilo para leer la entrada del usuario usando ncurses
 void *input_thread(void *arg) {
     char input[MAX_MESSAGE_SIZE] = {0};
     while(!force_exit) {
         // Limpiar línea de entrada
         pthread_mutex_lock(&ncurses_mutex);
         wclear(input_win);
         mvwprintw(input_win, 0, 0, "Escribe mensaje: ");
         wrefresh(input_win);
         pthread_mutex_unlock(&ncurses_mutex);
 
         // Leer línea desde la ventana de input
         wgetnstr(input_win, input, MAX_MESSAGE_SIZE - 1);
 
         // Si se escribe "/salir", se cierra la conexión
         if (strncmp(input, "/salir", 6) == 0) {
             pthread_mutex_lock(&send_mutex);
             char msg[SEND_BUFFER_SIZE];
             char timestamp[64];
             get_timestamp(timestamp, sizeof(timestamp));
             snprintf(msg, sizeof(msg),
                      "{\"type\": \"disconnect\", \"sender\": \"%s\", \"content\": \"Cierre de sesión\", \"timestamp\": \"%s\"}",
                      username, timestamp);
             strncpy(send_buffer, msg, sizeof(send_buffer)-1);
             send_pending = 1;
             pthread_mutex_unlock(&send_mutex);
             if(client_wsi)
                 lws_callback_on_writable(client_wsi);
             force_exit = 1;
             break;
         }
 
         // Preparar el mensaje en JSON. Si comienza con '@' se envía como privado.
         {
             char msg[SEND_BUFFER_SIZE];
             char timestamp[64];
             char target[128] = {0};
             char content[1024] = {0};
             get_timestamp(timestamp, sizeof(timestamp));
 
             if(input[0] == '@') {
                 // Se espera el formato: @destino mensaje...
                 char *space = strchr(input, ' ');
                 if(space) {
                     size_t target_len = space - input - 1; // omitir '@'
                     if(target_len > 0 && target_len < sizeof(target)) {
                         strncpy(target, input+1, target_len);
                         target[target_len] = '\0';
                     }
                     strncpy(content, space+1, sizeof(content)-1);
                     snprintf(msg, sizeof(msg),
                              "{\"type\": \"private\", \"sender\": \"%s\", \"target\": \"%s\", \"content\": \"%s\", \"timestamp\": \"%s\"}",
                              username, target, content, timestamp);
                 } else {
                     // Si no se encuentra espacio, tratar todo como mensaje
                     snprintf(msg, sizeof(msg),
                              "{\"type\": \"broadcast\", \"sender\": \"%s\", \"content\": \"%s\", \"timestamp\": \"%s\"}",
                              username, input, timestamp);
                 }
             } else {
                 // Mensaje de chat general (broadcast)
                 snprintf(msg, sizeof(msg),
                          "{\"type\": \"broadcast\", \"sender\": \"%s\", \"content\": \"%s\", \"timestamp\": \"%s\"}",
                          username, input, timestamp);
             }
             // Copiar el mensaje en el buffer global
             pthread_mutex_lock(&send_mutex);
             strncpy(send_buffer, msg, sizeof(send_buffer)-1);
             send_pending = 1;
             pthread_mutex_unlock(&send_mutex);
             // Solicitar a libwebsockets que haga writable el wsi para enviar
             if(client_wsi)
                 lws_callback_on_writable(client_wsi);
         }
     }
     return NULL;
 }
 
 int main(int argc, char **argv) {
     if(argc != 4) {
         fprintf(stderr, "Uso: %s <nombre_usuario> <IP_del_servidor> <puerto>\n", argv[0]);
         exit(EXIT_FAILURE);
     }
 
     strncpy(username, argv[1], sizeof(username)-1);
     const char *server_address = argv[2];
     int port = atoi(argv[3]);
 
     // Inicializar ncurses: dos ventanas, una para chat y otra para input
     initscr();
     cbreak();
     noecho();
     int height, width;
     getmaxyx(stdscr, height, width);
     chat_win = newwin(height - 3, width, 0, 0);
     input_win = newwin(3, width, height - 3, 0);
     scrollok(chat_win, TRUE);
     box(input_win, 0, 0);
     wrefresh(chat_win);
     wrefresh(input_win);
 
     // Configuración de libwebsockets
     struct lws_context_creation_info info;
     memset(&info, 0, sizeof(info));
     info.port = CONTEXT_PORT_NO_LISTEN;
     info.protocols = protocols;
     info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
     context = lws_create_context(&info);
     if(context == NULL) {
         fprintf(stderr, "Error al crear el contexto libwebsockets\n");
         endwin();
         exit(EXIT_FAILURE);
     }
 
     // Configuración de conexión
     struct lws_client_connect_info ccinfo = {0};
     ccinfo.context = context;
     ccinfo.address = server_address;
     ccinfo.port = port;
     ccinfo.path = "/chat";
     ccinfo.host = lws_canonical_hostname(context);
     ccinfo.origin = "origin";
     ccinfo.protocol = protocols[0].name;
     ccinfo.pwsi = &client_wsi;
 
     if(lws_client_connect_via_info(&ccinfo) == NULL) {
         fprintf(stderr, "Error en la conexión con el servidor\n");
         lws_context_destroy(context);
         endwin();
         exit(EXIT_FAILURE);
     }
 
     // Crear hilo para leer entrada del usuario
     pthread_t tid;
     if(pthread_create(&tid, NULL, input_thread, NULL) != 0) {
         fprintf(stderr, "Error al crear el hilo de entrada\n");
         lws_context_destroy(context);
         endwin();
         exit(EXIT_FAILURE);
     }
 
     // Loop principal de libwebsockets
     while(!force_exit) {
         lws_service(context, 50);
     }
 
     // Esperar a que finalice el hilo de entrada
     pthread_join(tid, NULL);
 
     // Limpiar recursos
     lws_context_destroy(context);
     delwin(chat_win);
     delwin(input_win);
     endwin();
     return 0;
 }
 