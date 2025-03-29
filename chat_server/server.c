/* chat_server_threads_fixed.c
 * Versión mejorada: Corrige crasheos y cumple el protocolo.
 */

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

 #include <stdarg.h> // Necesario para va_list

 
void get_timestamp(char *buffer, size_t len);

 void log_action(const char *format, ...) {
    char timestamp[64];

    // Preparar el mensaje
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Imprimir en consola
    printf("[%s] %s\n", timestamp, message);
    fflush(stdout);

    // Guardar en archivo
    FILE *log_file = fopen("servidor.log", "a");
    if (log_file) {
        fprintf(log_file, "[%s] %s\n", timestamp, message);
        fclose(log_file);
    }
}
 
 #define BUFFER_SIZE 2048
 #define MAX_STATUS_LEN 10
 #define INACTIVITY_TIMEOUT 60
 
 #define STATUS_ACTIVE   "ACTIVO"
 #define STATUS_BUSY     "OCUPADO"
 #define STATUS_INACTIVE "INACTIVO"

 void get_timestamp(char *buffer, size_t len);  // ← Esta línea soluciona el warning

 
 typedef struct Client {
     struct lws *wsi;
     char name[50];
     char ip[INET_ADDRSTRLEN];
     char status[MAX_STATUS_LEN];
     time_t last_activity;
     struct Client *next;
 } Client;
 
 static Client *clients = NULL;
 pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
 static volatile int force_exit = 0;
 
 void get_timestamp(char *buffer, size_t len) {
     time_t now = time(NULL);
     struct tm *tm_info = localtime(&now);
     strftime(buffer, len, "%Y-%m-%dT%H:%M:%S", tm_info);
 }
 
 void add_client(Client *new_client) {
     pthread_mutex_lock(&clients_mutex);
     new_client->next = clients;
     clients = new_client;
     log_action("Cliente registrado: %s (%s)", new_client->name, new_client->ip);
     pthread_mutex_unlock(&clients_mutex);
 }
 
 void remove_client(struct lws *wsi) {
     pthread_mutex_lock(&clients_mutex);
     Client *prev = NULL, *curr = clients;
     while (curr) {
         if (curr->wsi == wsi) {
             if (prev) prev->next = curr->next;
             else clients = curr->next;
             log_action("Cliente eliminado: %s (%s)", curr->name, curr->ip);
             free(curr);
             break;
         }
         prev = curr;
         curr = curr->next;
     }
     pthread_mutex_unlock(&clients_mutex);
 }
 
 Client* find_client_by_name(const char *name) {
     Client *curr = clients;
     while (curr) {
         if (strcmp(curr->name, name) == 0)
             return curr;
         curr = curr->next;
     }
     return NULL;
 }
 
 void send_ws_text(struct lws *wsi, const char *msg) {
     unsigned char buf[LWS_PRE + BUFFER_SIZE];
     size_t msg_len = strlen(msg);
     if (msg_len > BUFFER_SIZE) msg_len = BUFFER_SIZE;
     memcpy(&buf[LWS_PRE], msg, msg_len);
     lws_write(wsi, &buf[LWS_PRE], msg_len, LWS_WRITE_TEXT);
     lws_callback_on_writable(wsi);
 }
 
 void send_json(struct lws *wsi, const char *type, const char *sender, const char *target, const char *content) {
     cJSON *root = cJSON_CreateObject();
     cJSON_AddStringToObject(root, "type", type);
     if (sender) cJSON_AddStringToObject(root, "sender", sender);
     if (target) cJSON_AddStringToObject(root, "target", target);
     if (content) cJSON_AddStringToObject(root, "content", content);
     char ts[64];
     get_timestamp(ts, sizeof(ts));
     cJSON_AddStringToObject(root, "timestamp", ts);
     char *json_str = cJSON_PrintUnformatted(root);
     send_ws_text(wsi, json_str);
     free(json_str);
     cJSON_Delete(root);
 }
 
 void broadcast_json(const char *type, const char *sender, const char *content, struct lws *exclude) {
     cJSON *root = cJSON_CreateObject();
     cJSON_AddStringToObject(root, "type", type);
     cJSON_AddStringToObject(root, "sender", sender);
     cJSON_AddStringToObject(root, "content", content);
     char ts[64];
     get_timestamp(ts, sizeof(ts));
     cJSON_AddStringToObject(root, "timestamp", ts);
     char *json_str = cJSON_PrintUnformatted(root);
     pthread_mutex_lock(&clients_mutex);
     Client *c = clients;
     while (c) {
         if (c->wsi != exclude) send_ws_text(c->wsi, json_str);
         c = c->next;
     }
     pthread_mutex_unlock(&clients_mutex);
     free(json_str);
     cJSON_Delete(root);
 }
 
 void send_user_list(struct lws *wsi) {
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
 
 void send_user_info(struct lws *wsi, const char *target_name) {
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
 
 void *inactivity_monitor(void *arg) {
     while (!force_exit) {
         sleep(5);
         time_t now = time(NULL);
         pthread_mutex_lock(&clients_mutex);
         Client *c = clients;
         while (c) {
             if (strcmp(c->status, STATUS_ACTIVE) == 0 && difftime(now, c->last_activity) > INACTIVITY_TIMEOUT) {
                 strncpy(c->status, STATUS_INACTIVE, sizeof(c->status)-1);
                 cJSON *notif = cJSON_CreateObject();
                 cJSON_AddStringToObject(notif, "type", "status_update");
                 cJSON_AddStringToObject(notif, "sender", "server");
                 cJSON *content = cJSON_CreateObject();
                 cJSON_AddStringToObject(content, "user", c->name);
                 cJSON_AddStringToObject(content, "status", STATUS_INACTIVE);
                 cJSON_AddItemToObject(notif, "content", content);
                 char ts[64]; get_timestamp(ts, sizeof(ts));
                 cJSON_AddStringToObject(notif, "timestamp", ts);
                 char *notif_str = cJSON_PrintUnformatted(notif);
                 Client *tmp = clients;
                 while (tmp) { send_ws_text(tmp->wsi, notif_str); tmp = tmp->next; }
                 free(notif_str);
                 cJSON_Delete(notif);
             }
             c = c->next;
         }
         pthread_mutex_unlock(&clients_mutex);
     }
     return NULL;
 }
 
 void* cliente_session(void* arg) {
    Client* client = (Client*)arg;
    
    char logmsg[128];
    snprintf(logmsg, sizeof(logmsg), "Hilo creado para el cliente: %s (%s)", client->name, client->ip);
    log_action("%s", logmsg);

    // Aquí se puede simular alguna actividad del cliente
    while (!force_exit) {
        pthread_mutex_lock(&clients_mutex);
        time_t now = time(NULL);
        double idle = difftime(now, client->last_activity);
        pthread_mutex_unlock(&clients_mutex);

        if (idle > 120) {
            // Por ejemplo, simular que este hilo detecta inactividad o ejecuta acciones personalizadas
            log_action("Cliente %s está inactivo por más de 120s (monitoreado por su hilo)", client->name);
        }

        sleep(10); // Intervalo de simulación
    }

    return NULL;
}



 int callback_chat(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
     switch (reason) {
         case LWS_CALLBACK_RECEIVE: {
             char *msg = strndup((char *)in, len);
             if (!msg) break;
             cJSON *root = cJSON_Parse(msg);
             free(msg);
             if (!root) break;
             cJSON *type_obj = cJSON_GetObjectItem(root, "type");
             cJSON *sender_obj = cJSON_GetObjectItem(root, "sender");
             cJSON *content_obj = cJSON_GetObjectItem(root, "content");
             if (!cJSON_IsString(type_obj) || !cJSON_IsString(sender_obj)) {
                 cJSON_Delete(root);
                 break;
             }
             const char *type = type_obj->valuestring;
             const char *sender = sender_obj->valuestring;
             const char *content = cJSON_IsString(content_obj) ? content_obj->valuestring : NULL;
             Client *client = find_client_by_name(sender);
             if (client) client->last_activity = time(NULL);
 
             if (strcmp(type, "register") == 0) {
                if (find_client_by_name(sender)) {
                    send_json(wsi, "error", "server", NULL, "Nombre de usuario en uso");
                    cJSON_Delete(root);
                    return -1;
                }
            
                Client *new_client = calloc(1, sizeof(Client));
                new_client->wsi = wsi;
                strncpy(new_client->name, sender, sizeof(new_client->name)-1);
                const char *peer = lws_get_peer_simple(wsi, new_client->ip, sizeof(new_client->ip));
                if (!peer) strncpy(new_client->ip, "desconocido", sizeof(new_client->ip)-1);
                strncpy(new_client->status, STATUS_ACTIVE, sizeof(new_client->status)-1);
                new_client->last_activity = time(NULL);
            
                add_client(new_client);
            
                pthread_t client_thread;
                pthread_create(&client_thread, NULL, cliente_session, new_client);
                pthread_detach(client_thread);
            
                send_json(wsi, "register_success", "server", NULL, "Registro exitoso");
                broadcast_json("broadcast", "server", "Nuevo usuario conectado", wsi);
                send_user_list(wsi);
            }
             else if (strcmp(type, "broadcast") == 0) {
                 broadcast_json("broadcast", sender, content, NULL);
             } else if (strcmp(type, "private") == 0) {
                 cJSON *target_obj = cJSON_GetObjectItem(root, "target");
                 if (cJSON_IsString(target_obj)) {
                     Client *receiver = find_client_by_name(target_obj->valuestring);
                     if (receiver) {
                         send_json(receiver->wsi, "private", sender, receiver->name, content);
                     } else {
                         send_json(wsi, "error", "server", NULL, "Usuario no encontrado");
                     }
                 }
             } else if (strcmp(type, "list_users") == 0) {
                 send_user_list(wsi);
             } else if (strcmp(type, "user_info") == 0) {
                 cJSON *target_obj = cJSON_GetObjectItem(root, "target");
                 if (cJSON_IsString(target_obj)) {
                     send_user_info(wsi, target_obj->valuestring);
                 }
             } else if (strcmp(type, "change_status") == 0 && client && content) {
                 strncpy(client->status, content, sizeof(client->status)-1);
                 cJSON *msg = cJSON_CreateObject();
                 cJSON_AddStringToObject(msg, "type", "status_update");
                 cJSON_AddStringToObject(msg, "sender", "server");
                 cJSON *st = cJSON_CreateObject();
                 cJSON_AddStringToObject(st, "user", sender);
                 cJSON_AddStringToObject(st, "status", content);
                 cJSON_AddItemToObject(msg, "content", st);
                 char ts[64]; get_timestamp(ts, sizeof(ts));
                 cJSON_AddStringToObject(msg, "timestamp", ts);
                 char *msg_str = cJSON_PrintUnformatted(msg);
                 Client *tmp = clients;
                 while (tmp) { send_ws_text(tmp->wsi, msg_str); tmp = tmp->next; }
                 free(msg_str);
                 cJSON_Delete(msg);
             } else if (strcmp(type, "disconnect") == 0) {
                 char goodbye[100];
                 snprintf(goodbye, sizeof(goodbye), "%s ha salido", sender);
                 broadcast_json("user_disconnected", "server", goodbye, wsi);
                 remove_client(wsi);
                 lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
                 cJSON_Delete(root);
                 return -1;
             }
             cJSON_Delete(root);
             break;
         }
         case LWS_CALLBACK_CLOSED:
             remove_client(wsi);
             break;
         default: break;
     }
     return 0;
 }
 
 static struct lws_protocols protocols[] = {
     { "chat-protocol", callback_chat, 0, BUFFER_SIZE },
     { NULL, NULL, 0, 0 }
 };
 
 void sigint_handler(int sig) {
     force_exit = 1;
 }
 
 int main(int argc, char **argv) {
     signal(SIGINT, sigint_handler);
     int port = 8080;
     if (argc > 1) port = atoi(argv[1]);
     struct lws_context_creation_info info;
     memset(&info, 0, sizeof(info));
     info.port = port;
     info.protocols = protocols;
     info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
     struct lws_context *context = lws_create_context(&info);
     if (!context) {
         fprintf(stderr, "Error al crear el contexto\n");
         return -1;
     }
     pthread_t monitor_thread;
     pthread_create(&monitor_thread, NULL, inactivity_monitor, NULL);
     printf("Servidor WebSocket iniciado en el puerto %d\n", port);
     while (!force_exit) lws_service(context, 5);
     lws_context_destroy(context);
     return 0;
 }
 