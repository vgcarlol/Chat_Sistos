/*
 * chat_server.c
 *
 * Servidor de Chat en C usando WebSockets, libwebsockets y cJSON.
 * Basado en la definición del proyecto de chat y el protocolo WebSockets.
 *
 * Compilar en Ubuntu con:
 *   gcc chat_server.c -o chat_server -lwebsockets -lcjson -lpthread
 *
 * Uso:
 *   ./chat_server <puerto>
 * Si no se especifica puerto, se usará 8080.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <time.h>
 #include <pthread.h>
 #include <signal.h>
 #include <libwebsockets.h>
 #include <arpa/inet.h>
 #include <unistd.h>
 #include <errno.h>
 #include <cjson/cJSON.h>
 
 #define BUFFER_SIZE 2048
 #define MAX_STATUS_LEN 10
 
 // Estados posibles
 #define STATUS_ACTIVE   "ACTIVO"
 #define STATUS_BUSY     "OCUPADO"
 #define STATUS_INACTIVE "INACTIVO"
 
 typedef struct Client {
     struct lws *wsi;                  // Conexión WebSocket del cliente
     char name[50];                    // Nombre de usuario
     char ip[INET_ADDRSTRLEN];         // IP del cliente (en formato xxx.xxx.xxx.xxx)
     char status[MAX_STATUS_LEN];      // Estado: ACTIVO, OCUPADO, INACTIVO
     struct Client *next;
 } Client;
 
 static Client *clients = NULL;
 pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
 
 // Función para obtener la hora actual en formato ISO8601
 void get_timestamp(char *buffer, size_t len) {
     time_t now = time(NULL);
     struct tm *tm_info = localtime(&now);
     strftime(buffer, len, "%Y-%m-%dT%H:%M:%S", tm_info);
 }
 
 // Agregar un nuevo cliente a la lista global
 void add_client(Client *new_client) {
     pthread_mutex_lock(&clients_mutex);
     new_client->next = clients;
     clients = new_client;
     pthread_mutex_unlock(&clients_mutex);
 }
 
 // Eliminar un cliente de la lista, identificándolo por su conexión (wsi)
 void remove_client(struct lws *wsi) {
     pthread_mutex_lock(&clients_mutex);
     Client *prev = NULL;
     Client *curr = clients;
     while (curr) {
         if (curr->wsi == wsi) {
             if (prev) {
                 prev->next = curr->next;
             } else {
                 clients = curr->next;
             }
             free(curr);
             break;
         }
         prev = curr;
         curr = curr->next;
     }
     pthread_mutex_unlock(&clients_mutex);
 }
 
 // Buscar un cliente por su nombre
 Client *find_client_by_name(const char *name) {
     pthread_mutex_lock(&clients_mutex);
     Client *curr = clients;
     while (curr) {
         if (strcmp(curr->name, name) == 0) {
             pthread_mutex_unlock(&clients_mutex);
             return curr;
         }
         curr = curr->next;
     }
     pthread_mutex_unlock(&clients_mutex);
     return NULL;
 }
 
 // Función para enviar un mensaje a través de una conexión WebSocket
 void send_ws_message(struct lws *wsi, const char *msg) {
     unsigned char buf[LWS_PRE + BUFFER_SIZE];
     size_t msg_len = strlen(msg);
     if (msg_len > BUFFER_SIZE)
         msg_len = BUFFER_SIZE;
     memcpy(&buf[LWS_PRE], msg, msg_len);
     lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
 }
 
 // Enviar mensaje JSON construido a partir de parámetros
 void send_json_message(struct lws *wsi, const char *type, const char *sender, const char *target, const char *content) {
     cJSON *json = cJSON_CreateObject();
     cJSON_AddStringToObject(json, "type", type);
     cJSON_AddStringToObject(json, "sender", sender);
     if (target && strlen(target) > 0)
         cJSON_AddStringToObject(json, "target", target);
     if (content)
         cJSON_AddStringToObject(json, "content", content);
     char timestamp[64];
     get_timestamp(timestamp, sizeof(timestamp));
     cJSON_AddStringToObject(json, "timestamp", timestamp);
 
     char *json_str = cJSON_PrintUnformatted(json);
     send_ws_message(wsi, json_str);
     free(json_str);
     cJSON_Delete(json);
 }
 
 // Enviar lista de usuarios conectados en formato JSON
 void send_user_list(struct lws *wsi) {
     cJSON *json = cJSON_CreateObject();
     cJSON_AddStringToObject(json, "type", "list_users_response");
     cJSON_AddStringToObject(json, "sender", "server");
     cJSON *userList = cJSON_CreateArray();
 
     pthread_mutex_lock(&clients_mutex);
     Client *curr = clients;
     while (curr) {
         cJSON_AddItemToArray(userList, cJSON_CreateString(curr->name));
         curr = curr->next;
     }
     pthread_mutex_unlock(&clients_mutex);
 
     cJSON_AddItemToObject(json, "content", userList);
     char timestamp[64];
     get_timestamp(timestamp, sizeof(timestamp));
     cJSON_AddStringToObject(json, "timestamp", timestamp);
 
     char *json_str = cJSON_PrintUnformatted(json);
     send_ws_message(wsi, json_str);
     free(json_str);
     cJSON_Delete(json);
 }
 
 // Enviar respuesta con información de un usuario
 void send_user_info(struct lws *wsi, const char *target_name) {
     Client *user = find_client_by_name(target_name);
     cJSON *json = cJSON_CreateObject();
     cJSON_AddStringToObject(json, "type", "user_info_response");
     cJSON_AddStringToObject(json, "sender", "server");
     cJSON_AddStringToObject(json, "target", target_name);
     char timestamp[64];
     get_timestamp(timestamp, sizeof(timestamp));
     cJSON_AddStringToObject(json, "timestamp", timestamp);
     if (user) {
         cJSON *content = cJSON_CreateObject();
         cJSON_AddStringToObject(content, "ip", user->ip);
         cJSON_AddStringToObject(content, "status", user->status);
         cJSON_AddItemToObject(json, "content", content);
     } else {
         cJSON_AddStringToObject(json, "content", "Usuario no encontrado");
     }
     char *json_str = cJSON_PrintUnformatted(json);
     send_ws_message(wsi, json_str);
     free(json_str);
     cJSON_Delete(json);
 }
 
 // Enviar un mensaje a todos los clientes (broadcast)
 void broadcast_message(const char *msg, struct lws *sender) {
     pthread_mutex_lock(&clients_mutex);
     Client *curr = clients;
     while (curr) {
         if (curr->wsi != sender)
             send_ws_message(curr->wsi, msg);
         curr = curr->next;
     }
     pthread_mutex_unlock(&clients_mutex);
 }
 
 // Callback principal para el protocolo WebSocket
 static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
                          void *user, void *in, size_t len) {
     switch (reason) {
         case LWS_CALLBACK_ESTABLISHED: {
             printf("Nueva conexión establecida.\n");
             break;
         }
         case LWS_CALLBACK_RECEIVE: {
             char *msg = (char *)malloc(len + 1);
             if (!msg)
                 break;
             memcpy(msg, in, len);
             msg[len] = '\0';
 
             // Parsear el mensaje JSON recibido
             cJSON *json = cJSON_Parse(msg);
             free(msg);
             if (!json)
                 break;
 
             cJSON *type_item = cJSON_GetObjectItemCaseSensitive(json, "type");
             cJSON *sender_item = cJSON_GetObjectItemCaseSensitive(json, "sender");
             if (!cJSON_IsString(type_item) || !cJSON_IsString(sender_item)) {
                 cJSON_Delete(json);
                 break;
             }
             const char *type = type_item->valuestring;
             const char *sender = sender_item->valuestring;
 
             if (strcmp(type, "register") == 0) {
                 // Registro de usuario: se rechaza si el nombre ya existe
                 if (find_client_by_name(sender)) {
                     send_json_message(wsi, "error", "server", NULL, "Nombre de usuario en uso");
                     lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, (unsigned char *)"Usuario duplicado", 16);
                     cJSON_Delete(json);
                     return -1;
                 }
                 // Crear registro del cliente
                 Client *new_client = (Client *)malloc(sizeof(Client));
                 new_client->wsi = wsi;
                 strncpy(new_client->name, sender, sizeof(new_client->name)-1);
                 new_client->name[sizeof(new_client->name)-1] = '\0';
                 char client_address[128], client_ip[INET_ADDRSTRLEN];
                 lws_get_peer_addresses(wsi, lws_get_socket_fd(wsi),
                                        client_address, sizeof(client_address),
                                        client_ip, sizeof(client_ip));
                 strncpy(new_client->ip, client_ip, sizeof(new_client->ip)-1);
                 new_client->ip[sizeof(new_client->ip)-1] = '\0';
                 strncpy(new_client->status, STATUS_ACTIVE, sizeof(new_client->status)-1);
                 new_client->status[sizeof(new_client->status)-1] = '\0';
                 new_client->next = NULL;
                 add_client(new_client);
 
                 // Responder con "register_success" y la lista de usuarios
                 cJSON *resp = cJSON_CreateObject();
                 cJSON_AddStringToObject(resp, "type", "register_success");
                 cJSON_AddStringToObject(resp, "sender", "server");
                 cJSON_AddStringToObject(resp, "content", "Registro exitoso");
                 cJSON *userList = cJSON_CreateArray();
                 pthread_mutex_lock(&clients_mutex);
                 Client *curr = clients;
                 while (curr) {
                     cJSON_AddItemToArray(userList, cJSON_CreateString(curr->name));
                     curr = curr->next;
                 }
                 pthread_mutex_unlock(&clients_mutex);
                 cJSON_AddItemToObject(resp, "userList", userList);
                 char timestamp[64];
                 get_timestamp(timestamp, sizeof(timestamp));
                 cJSON_AddStringToObject(resp, "timestamp", timestamp);
                 char *resp_str = cJSON_PrintUnformatted(resp);
                 send_ws_message(wsi, resp_str);
                 free(resp_str);
                 cJSON_Delete(resp);
             } else if (strcmp(type, "broadcast") == 0) {
                 cJSON *content_item = cJSON_GetObjectItemCaseSensitive(json, "content");
                 if (cJSON_IsString(content_item)) {
                     char out[BUFFER_SIZE];
                     snprintf(out, sizeof(out), "[%s]: %s", sender, content_item->valuestring);
                     broadcast_message(out, wsi);
                 }
             } else if (strcmp(type, "private") == 0) {
                 cJSON *target_item = cJSON_GetObjectItemCaseSensitive(json, "target");
                 cJSON *content_item = cJSON_GetObjectItemCaseSensitive(json, "content");
                 if (cJSON_IsString(target_item) && cJSON_IsString(content_item)) {
                     Client *target = find_client_by_name(target_item->valuestring);
                     if (target) {
                         char out[BUFFER_SIZE];
                         snprintf(out, sizeof(out), "[%s (privado)]: %s", sender, content_item->valuestring);
                         send_ws_message(target->wsi, out);
                     } else {
                         send_json_message(wsi, "error", "server", NULL, "Usuario no encontrado");
                     }
                 }
             } else if (strcmp(type, "list_users") == 0) {
                 send_user_list(wsi);
             } else if (strcmp(type, "user_info") == 0) {
                 cJSON *target_item = cJSON_GetObjectItemCaseSensitive(json, "target");
                 if (cJSON_IsString(target_item))
                     send_user_info(wsi, target_item->valuestring);
             } else if (strcmp(type, "change_status") == 0) {
                 cJSON *content_item = cJSON_GetObjectItemCaseSensitive(json, "content");
                 if (cJSON_IsString(content_item)) {
                     pthread_mutex_lock(&clients_mutex);
                     Client *curr = clients;
                     while (curr) {
                         if (strcmp(curr->name, sender) == 0) {
                             strncpy(curr->status, content_item->valuestring, sizeof(curr->status)-1);
                             curr->status[sizeof(curr->status)-1] = '\0';
                             break;
                         }
                         curr = curr->next;
                     }
                     pthread_mutex_unlock(&clients_mutex);
                     char out[BUFFER_SIZE];
                     snprintf(out, sizeof(out), "[server]: %s cambió su estado a %s", sender, content_item->valuestring);
                     broadcast_message(out, NULL);
                 }
             } else if (strcmp(type, "disconnect") == 0) {
                 char out[BUFFER_SIZE];
                 snprintf(out, sizeof(out), "[server]: %s se ha desconectado", sender);
                 broadcast_message(out, wsi);
                 remove_client(wsi);
                 lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
                 cJSON_Delete(json);
                 return -1;
             }
             cJSON_Delete(json);
             break;
         }
         case LWS_CALLBACK_CLOSED: {
             remove_client(wsi);
             printf("Conexión cerrada.\n");
             break;
         }
         default:
             break;
     }
     return 0;
 }
 
 static struct lws_protocols protocols[] = {
     {
         "chat-protocol",
         callback_chat,
         0,
         BUFFER_SIZE,
     },
     { NULL, NULL, 0, 0 }
 };
 
 static int force_exit = 0;
 
 static void sigint_handler(int sig) {
     force_exit = 1;
 }
 
 int main(int argc, char **argv) {
     int port = 8080;
     if (argc > 1)
         port = atoi(argv[1]);
     signal(SIGINT, sigint_handler);
 
     struct lws_context_creation_info info;
     memset(&info, 0, sizeof(info));
     info.port = port;
     info.protocols = protocols;
     info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
     struct lws_context *context = lws_create_context(&info);
     if (context == NULL) {
         fprintf(stderr, "Error al crear el contexto libwebsockets\n");
         return -1;
     }
     printf("Servidor WebSocket iniciado en el puerto %d\n", port);
 
     while (!force_exit) {
         lws_service(context, 50);
     }
     lws_context_destroy(context);
     return 0;
 }
 