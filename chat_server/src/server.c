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
 
 static int callback_chat(struct lws *wsi, enum lws_callback_reasons reason,
    void *user, void *in, size_t len)
{
switch (reason) {
case LWS_CALLBACK_ESTABLISHED: {
printf("Nueva conexión establecida.\n");
break;
}
case LWS_CALLBACK_RECEIVE: {
// Mensaje entrante
char *msg = (char *)malloc(len + 1);
if (!msg) break;
memcpy(msg, in, len);
msg[len] = '\0';

cJSON *root = cJSON_Parse(msg);
free(msg);
if (!root) {
// No es JSON válido
printf("Recibido texto no JSON\n");
break;
}
cJSON *type_item    = cJSON_GetObjectItemCaseSensitive(root, "type");
cJSON *sender_item  = cJSON_GetObjectItemCaseSensitive(root, "sender");
cJSON *content_item = cJSON_GetObjectItemCaseSensitive(root, "content");

if (!cJSON_IsString(type_item) || !cJSON_IsString(sender_item)) {
cJSON_Delete(root);
break;
}

const char *type = type_item->valuestring;
const char *sender_name = sender_item->valuestring;

if (strcmp(type, "register") == 0) {
// Verificar si ya existe
if (find_client_by_name(sender_name)) {
// Usuario duplicado
send_json(wsi, "error", "server", NULL, "Nombre de usuario en uso");
lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL,
                (unsigned char *)"Usuario duplicado", 16);
cJSON_Delete(root);
return -1;
}
// Crear nuevo cliente
Client *new_client = (Client *)malloc(sizeof(Client));
if (!new_client) {
cJSON_Delete(root);
break;
}
new_client->wsi = wsi;
strncpy(new_client->name, sender_name, sizeof(new_client->name)-1);
new_client->name[sizeof(new_client->name)-1] = '\0';

// Obtener IP
char client_address[128], client_ip[INET_ADDRSTRLEN];
lws_get_peer_addresses(wsi, lws_get_socket_fd(wsi),
                  client_address, sizeof(client_address),
                  client_ip, sizeof(client_ip));
strncpy(new_client->ip, client_ip, sizeof(new_client->ip)-1);
new_client->ip[sizeof(new_client->ip)-1] = '\0';

// Estado inicial
strncpy(new_client->status, STATUS_ACTIVE, sizeof(new_client->status)-1);
new_client->status[sizeof(new_client->status)-1] = '\0';

add_client(new_client);

// Responder "register_success" con userList
cJSON *resp = cJSON_CreateObject();
cJSON_AddStringToObject(resp, "type", "register_success");
cJSON_AddStringToObject(resp, "sender", "server");
cJSON_AddStringToObject(resp, "content", "Registro exitoso");

cJSON *userList = cJSON_CreateArray();
pthread_mutex_lock(&clients_mutex);
Client *cc = clients;
while (cc) {
cJSON_AddItemToArray(userList, cJSON_CreateString(cc->name));
cc = cc->next;
}
pthread_mutex_unlock(&clients_mutex);
cJSON_AddItemToObject(resp, "userList", userList);

char ts[64];
get_timestamp(ts, sizeof(ts));
cJSON_AddStringToObject(resp, "timestamp", ts);

char *resp_str = cJSON_PrintUnformatted(resp);
send_ws_text(wsi, resp_str);
free(resp_str);
cJSON_Delete(resp);
}
else if (strcmp(type, "broadcast") == 0) {
// Enviar broadcast a TODOS, incluyendo al emisor
broadcast_json("broadcast", sender_name, content_item->valuestring, NULL);
}

else if (strcmp(type, "private") == 0) {
// Mensaje privado
cJSON *target_item = cJSON_GetObjectItemCaseSensitive(root, "target");
if (cJSON_IsString(target_item) && cJSON_IsString(content_item)) {
Client *target = find_client_by_name(target_item->valuestring);
if (target) {
   // Enviar JSON con type=private, sender=..., content=...
   cJSON *pvt = cJSON_CreateObject();
   cJSON_AddStringToObject(pvt, "type", "private");
   cJSON_AddStringToObject(pvt, "sender", sender_name);
   cJSON_AddStringToObject(pvt, "content", content_item->valuestring);

   char ts[64];
   get_timestamp(ts, sizeof(ts));
   cJSON_AddStringToObject(pvt, "timestamp", ts);

   char *pvt_str = cJSON_PrintUnformatted(pvt);
   send_ws_text(target->wsi, pvt_str);
   free(pvt_str);
   cJSON_Delete(pvt);
} else {
   send_json(wsi, "error", "server", NULL, "Usuario no encontrado");
}
}
}
else if (strcmp(type, "list_users") == 0) {
send_user_list(wsi);
}
else if (strcmp(type, "user_info") == 0) {
cJSON *target_item = cJSON_GetObjectItemCaseSensitive(root, "target");
if (cJSON_IsString(target_item)) {
send_user_info(wsi, target_item->valuestring);
}
}
else if (strcmp(type, "change_status") == 0) {
if (cJSON_IsString(content_item)) {
pthread_mutex_lock(&clients_mutex);
Client *c = clients;
while (c) {
   if (strcmp(c->name, sender_name) == 0) {
       strncpy(c->status, content_item->valuestring, sizeof(c->status)-1);
       c->status[sizeof(c->status)-1] = '\0';
       break;
   }
   c = c->next;
}
pthread_mutex_unlock(&clients_mutex);
// Notificar a todos
// type="status_update", content={"user":..., "status":...}
cJSON *st_update = cJSON_CreateObject();
cJSON_AddStringToObject(st_update, "type", "status_update");
cJSON_AddStringToObject(st_update, "sender", "server");

cJSON *st_content = cJSON_CreateObject();
cJSON_AddStringToObject(st_content, "user", sender_name);
cJSON_AddStringToObject(st_content, "status", content_item->valuestring);
cJSON_AddItemToObject(st_update, "content", st_content);

char ts2[64];
get_timestamp(ts2, sizeof(ts2));
cJSON_AddStringToObject(st_update, "timestamp", ts2);

char *st_str = cJSON_PrintUnformatted(st_update);
// broadcast
pthread_mutex_lock(&clients_mutex);
Client *cli = clients;
while (cli) {
   send_ws_text(cli->wsi, st_str);
   cli = cli->next;
}
pthread_mutex_unlock(&clients_mutex);

free(st_str);
cJSON_Delete(st_update);
}
}
else if (strcmp(type, "disconnect") == 0) {
// Notificar a todos que se desconectó
char out[128];
snprintf(out, sizeof(out), "%s se ha desconectado", sender_name);

// type="user_disconnected", content=...
send_json(wsi, "user_disconnected", "server", NULL, out);

// broadcast un "broadcast" o algo, si quieres
broadcast_json("broadcast", "server", out, wsi);

remove_client(wsi);
lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
cJSON_Delete(root);
return -1;
}

cJSON_Delete(root);
break;
}
case LWS_CALLBACK_CLOSED: {
printf("Conexión cerrada.\n");
remove_client(wsi);
break;
}
default:
break;
}
return 0;
}

// Lista de protocolos
static struct lws_protocols protocols[] = {
{ "chat-protocol", callback_chat, 0, BUFFER_SIZE },
{ NULL, NULL, 0, 0 }
};

static volatile int force_exit = 0;
static void sigint_handler(int sig) {
force_exit = 1;
}

int main(int argc, char **argv) {
int port = 8080;
if (argc > 1)
port = atoi(argv[1]);

signal(SIGINT, sigint_handler);

// (Opcional) Activar logs de debug
// lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_DEBUG, NULL);

struct lws_context_creation_info info;
memset(&info, 0, sizeof(info));
info.port = port;
info.protocols = protocols;
info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

struct lws_context *context = lws_create_context(&info);
if(!context) {
fprintf(stderr, "Error al crear el contexto libwebsockets\n");
return -1;
}

printf("Servidor WebSocket iniciado en el puerto %d\n", port);

// Se reduce el tiempo de espera en lws_service para responder más rápido
while (!force_exit) {
lws_service(context, 5);  // <--- Valor pequeño (5ms). Puedes usar 1 o 0
}

lws_context_destroy(context);
return 0;
}
