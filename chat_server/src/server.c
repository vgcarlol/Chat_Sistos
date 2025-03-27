/******************************************************************************
 * chat_server.c (versión con ajustes de velocidad)
 *
 * Servidor de Chat en C usando WebSockets, libwebsockets y cJSON.
 * Envía TODOS los mensajes en JSON (tipo, sender, content, etc.),
 * compatible con el cliente que parsea JSON.
 *
 * Cambios para aumentar la velocidad:
 *  - lws_service(context, 5) en vez de 50 (reduce latencia en el loop).
 *  - Se llama a lws_callback_on_writable(wsi) después de cada envío de texto.
 *  - (Opcional) Se puede activar logs de debug con lws_set_log_level(...).
 ******************************************************************************/

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <time.h>
 #include <pthread.h>
 #include <signal.h>
 #include <arpa/inet.h>
 #include <unistd.h>
 #include <errno.h>
 #include <libwebsockets.h>
 #include <cjson/cJSON.h>
 
 #define BUFFER_SIZE 2048
 #define MAX_STATUS_LEN 10
 
 // Estados permitidos
 #define STATUS_ACTIVE   "ACTIVO"
 #define STATUS_BUSY     "OCUPADO"
 #define STATUS_INACTIVE "INACTIVO"
 
 // Estructura para cada cliente conectado
 typedef struct Client {
     struct lws *wsi;                  // Conexión WebSocket del cliente
     char name[50];                    // Nombre de usuario
     char ip[INET_ADDRSTRLEN];         // IP del cliente
     char status[MAX_STATUS_LEN];      // Estado actual
     struct Client *next;
 } Client;
 
 static Client *clients = NULL;
 pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
 
 // Obtener timestamp en formato ISO8601
 void get_timestamp(char *buffer, size_t len) {
     time_t now = time(NULL);
     struct tm *tm_info = localtime(&now);
     strftime(buffer, len, "%Y-%m-%dT%H:%M:%S", tm_info);
 }
 
 // Agregar un cliente a la lista
 void add_client(Client *new_client) {
     pthread_mutex_lock(&clients_mutex);
     new_client->next = clients;
     clients = new_client;
     pthread_mutex_unlock(&clients_mutex);
 }
 
 // Remover un cliente de la lista (por su wsi)
 void remove_client(struct lws *wsi) {
     pthread_mutex_lock(&clients_mutex);
     Client *prev = NULL;
     Client *curr = clients;
     while (curr) {
         if (curr->wsi == wsi) {
             if (prev)
                 prev->next = curr->next;
             else
                 clients = curr->next;
             free(curr);
             break;
         }
         prev = curr;
         curr = curr->next;
     }
     pthread_mutex_unlock(&clients_mutex);
 }
 
 // Buscar cliente por nombre
 Client* find_client_by_name(const char *name) {
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
 
 // Enviar mensaje (texto) por WebSocket
 static void send_ws_text(struct lws *wsi, const char *msg) {
     unsigned char buf[LWS_PRE + BUFFER_SIZE];
     size_t msg_len = strlen(msg);
     if (msg_len > BUFFER_SIZE)
         msg_len = BUFFER_SIZE;
 
     memcpy(&buf[LWS_PRE], msg, msg_len);
     lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
 
     // Forzar a libwebsockets a marcar wsi como listo para escribir otra vez
     lws_callback_on_writable(wsi);
 }
 
 // Construir y enviar un JSON con (type, sender, content, target, timestamp)
 static void send_json(struct lws *wsi, const char *type,
                       const char *sender, const char *target,
                       const char *content)
 {
     cJSON *root = cJSON_CreateObject();
     cJSON_AddStringToObject(root, "type", type);
     if (sender)
         cJSON_AddStringToObject(root, "sender", sender);
     if (target && strlen(target) > 0)
         cJSON_AddStringToObject(root, "target", target);
     if (content)
         cJSON_AddStringToObject(root, "content", content);
 
     char ts[64];
     get_timestamp(ts, sizeof(ts));
     cJSON_AddStringToObject(root, "timestamp", ts);
 
     char *json_str = cJSON_PrintUnformatted(root);
     send_ws_text(wsi, json_str);
 
     free(json_str);
     cJSON_Delete(root);
 }
 
 // Enviar lista de usuarios en JSON
 static void send_user_list(struct lws *wsi) {
     cJSON *root = cJSON_CreateObject();
     cJSON_AddStringToObject(root, "type", "list_users_response");
     cJSON_AddStringToObject(root, "sender", "server");
 
     cJSON *array = cJSON_CreateArray();
     pthread_mutex_lock(&clients_mutex);
     Client *c = clients;
     while (c) {
         cJSON_AddItemToArray(array, cJSON_CreateString(c->name));
         c = c->next;
     }
     pthread_mutex_unlock(&clients_mutex);
     cJSON_AddItemToObject(root, "content", array);
 
     char ts[64];
     get_timestamp(ts, sizeof(ts));
     cJSON_AddStringToObject(root, "timestamp", ts);
 
     char *json_str = cJSON_PrintUnformatted(root);
     send_ws_text(wsi, json_str);
     free(json_str);
     cJSON_Delete(root);
 }
 
 // Enviar información de un usuario específico
 static void send_user_info(struct lws *wsi, const char *target_name) {
     Client *user = find_client_by_name(target_name);
     cJSON *root = cJSON_CreateObject();
     cJSON_AddStringToObject(root, "type", "user_info_response");
     cJSON_AddStringToObject(root, "sender", "server");
     cJSON_AddStringToObject(root, "target", target_name);
 
     char ts[64];
     get_timestamp(ts, sizeof(ts));
     cJSON_AddStringToObject(root, "timestamp", ts);
 
     if (user) {
         cJSON *info = cJSON_CreateObject();
         cJSON_AddStringToObject(info, "ip", user->ip);
         cJSON_AddStringToObject(info, "status", user->status);
         cJSON_AddItemToObject(root, "content", info);
     } else {
         cJSON_AddStringToObject(root, "content", "Usuario no encontrado");
     }
     char *json_str = cJSON_PrintUnformatted(root);
     send_ws_text(wsi, json_str);
     free(json_str);
     cJSON_Delete(root);
 }
 
 // Broadcast en JSON (type = "broadcast", "sender", "content", etc.)
 static void broadcast_json(const char *type,
                            const char *sender,
                            const char *content,
                            struct lws *exclude)
 {
     // Construir el JSON a enviar
     cJSON *root = cJSON_CreateObject();
     cJSON_AddStringToObject(root, "type", type);
     cJSON_AddStringToObject(root, "sender", sender);
     cJSON_AddStringToObject(root, "content", content);
 
     char ts[64];
     get_timestamp(ts, sizeof(ts));
     cJSON_AddStringToObject(root, "timestamp", ts);
 
     char *json_str = cJSON_PrintUnformatted(root);
 
     // Enviar a todos menos "exclude"
     pthread_mutex_lock(&clients_mutex);
     Client *c = clients;
     while (c) {
        if (c->wsi != exclude) {
            send_ws_text(c->wsi, json_str);
        }
        c = c->next;
    }
     pthread_mutex_unlock(&clients_mutex);
 
     free(json_str);
     cJSON_Delete(root);
 }
 
 // ----------------- Callback principal de libwebsockets -----------------
 