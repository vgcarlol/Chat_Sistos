/******************************************************************************
 * Cliente de Chat con Interfaz Gráfica (GTK3) + libwebsockets + cJSON
 * ---------------------------------------------------------------------------
 * - Cumple con el "Protocolo con WebSockets" (registro, broadcast, privado,
 *   listar usuarios, cambiar estado, etc.).
 * - Usa un hilo para lws_service, evitando bloquear la interfaz gráfica.
 * - Usa cJSON para parsear las respuestas del servidor y mostrar mensajes
 *   más amigables al usuario.
 *
 * Requerimientos en Ubuntu:
 *   sudo apt-get install libgtk-3-dev libwebsockets-dev libcjson-dev
 *
 * Compilar:
 *   gcc chat_client_gtk.c -o chat_client_gtk \
 *       $(pkg-config --cflags --libs gtk+-3.0) \
 *       -lwebsockets -lcjson -lpthread
 *
 * Ejecutar:
 *   ./chat_client_gtk
 *
 * Nota: Se ha reducido el tiempo de espera en el loop de libwebsockets para
 *       mejorar la reactividad de las acciones.
 ******************************************************************************/

 #include <gtk/gtk.h>
 #include <libwebsockets.h>
 #include <pthread.h>
 #include <string.h>
 #include <time.h>
 #include <stdlib.h>
 #include <cjson/cJSON.h>
 
 // Tamaños de buffers
 #define WS_BUFFER 2048
 
 // Estados posibles (según el protocolo)
 #define STATUS_ACTIVE   "ACTIVO"
 #define STATUS_BUSY     "OCUPADO"
 #define STATUS_INACTIVE "INACTIVO"
 
 // Estructura para guardar datos globales de la app
 typedef struct {
     // Elementos de la interfaz
     GtkWidget *window_main;       // Ventana principal
     GtkWidget *entry_username;    // Campo de texto para nombre de usuario
     GtkWidget *entry_ip;          // Campo de texto para IP
     GtkWidget *entry_port;        // Campo de texto para puerto
     GtkWidget *btn_connect;       // Botón Conectar
 
     GtkWidget *textview_chat;     // Área de texto para mensajes
     GtkWidget *entry_message;     // Campo de texto para escribir mensajes
     GtkWidget *btn_send;          // Botón Enviar
 
     GtkWidget *btn_list_users;    // Botón "Listar usuarios"
     GtkWidget *combo_status;      // ComboBoxText para cambiar estado
     GtkWidget *btn_change_status; // Botón para confirmar cambio de estado
 
     // Variables de conexión
     struct lws_context *context;
     struct lws *wsi;
     char username[128];
     char server_ip[128];
     int  server_port;
 
     // Sincronización y estado
     pthread_mutex_t lock_send_buffer;
     char send_buffer[WS_BUFFER];
     int  send_pending;
     volatile int connected;
     volatile int force_exit;
 } AppData;
 
 // Estructura para pasar mensaje a la función idle (actualizar GUI)
 typedef struct {
     AppData *app;
     char *msg;
 } IdleMsgData;
 
 // ----------------- Funciones de ayuda -----------------
 
 // Obtener timestamp (ISO8601 simplificado)
 static void get_timestamp(char *buf, size_t len) {
     time_t now = time(NULL);
     struct tm *tm_info = localtime(&now);
     strftime(buf, len, "%Y-%m-%dT%H:%M:%S", tm_info);
 }
 
 // Agregar texto al TextView (se llama desde el hilo principal)
 static void append_chat_text(AppData *app, const char *msg) {
     GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->textview_chat));
     GtkTextIter end;
     gtk_text_buffer_get_end_iter(buffer, &end);
     gtk_text_buffer_insert(buffer, &end, msg, -1);
     gtk_text_buffer_insert(buffer, &end, "\n", 1);
 }
 
 // Función idle que se programa con g_idle_add() para actualizar la GUI
 static gboolean update_chat_idle(gpointer data) {
     IdleMsgData *idle_data = (IdleMsgData *)data;
     append_chat_text(idle_data->app, idle_data->msg);
     g_free(idle_data->msg);
     g_free(idle_data);
     return FALSE; // No se repite
 }
 
 // Enviar un mensaje JSON al servidor (ya en formato string)
 static void send_json(AppData *app, const char *json_str) {
     pthread_mutex_lock(&app->lock_send_buffer);
     strncpy(app->send_buffer, json_str, sizeof(app->send_buffer) - 1);
     app->send_pending = 1;
     pthread_mutex_unlock(&app->lock_send_buffer);
 
     if (app->wsi) {
         lws_callback_on_writable(app->wsi);
     }
 }
 
 // Construir y enviar un mensaje JSON a partir de cJSON
 static void send_cjson(AppData *app, cJSON *root) {
     char *msg_str = cJSON_PrintUnformatted(root);
     if (!msg_str) return;
     send_json(app, msg_str);
     free(msg_str);
 }
 
 // Mostrar un mensaje en la GUI usando g_idle_add
 static void show_message(AppData *app, const char *text) {
     IdleMsgData *idle_data = g_new0(IdleMsgData, 1);
     idle_data->app = app;
     idle_data->msg = g_strdup(text);
     g_idle_add(update_chat_idle, idle_data);
 }
 
 // ----------------- Parseo de mensajes recibidos -----------------
 
 // Manejar un mensaje JSON recibido del servidor
 static void handle_server_message(AppData *app, const char *json_str) {
     cJSON *root = cJSON_Parse(json_str);
     if (!root) {
         show_message(app, "Recibido mensaje no-JSON (o inválido)");
         return;
     }
 
     cJSON *type_item   = cJSON_GetObjectItemCaseSensitive(root, "type");
     cJSON *sender_item = cJSON_GetObjectItemCaseSensitive(root, "sender");
     cJSON *content_item= cJSON_GetObjectItemCaseSensitive(root, "content");
     if (!cJSON_IsString(type_item)) {
         cJSON_Delete(root);
         return;
     }
     const char *type = type_item->valuestring;
 
     if (strcmp(type, "register_success") == 0) {
         cJSON *userList_item = cJSON_GetObjectItemCaseSensitive(root, "userList");
         if (cJSON_IsArray(userList_item)) {
             show_message(app, "Registro exitoso. Lista de usuarios:");
             cJSON *user_elem = NULL;
             cJSON_ArrayForEach(user_elem, userList_item) {
                 if (cJSON_IsString(user_elem)) {
                     show_message(app, user_elem->valuestring);
                 }
             }
         } else {
             if (cJSON_IsString(content_item))
                 show_message(app, content_item->valuestring);
         }
     }
     else if (strcmp(type, "broadcast") == 0) {
         if (cJSON_IsString(sender_item) && cJSON_IsString(content_item)) {
             char buff[256];
             snprintf(buff, sizeof(buff), "[%s (broadcast)]: %s",
                      sender_item->valuestring, content_item->valuestring);
             show_message(app, buff);
         }
     }
     else if (strcmp(type, "private") == 0) {
         if (cJSON_IsString(sender_item) && cJSON_IsString(content_item)) {
             char buff[256];
             snprintf(buff, sizeof(buff), "[%s (privado)]: %s",
                      sender_item->valuestring, content_item->valuestring);
             show_message(app, buff);
         }
     }
     else if (strcmp(type, "list_users_response") == 0) {
         if (cJSON_IsArray(content_item)) {
             show_message(app, "Usuarios conectados:");
             cJSON *user_elem = NULL;
             cJSON_ArrayForEach(user_elem, content_item) {
                 if (cJSON_IsString(user_elem)) {
                     show_message(app, user_elem->valuestring);
                 }
             }
         }
     }
     else if (strcmp(type, "status_update") == 0) {
         if (cJSON_IsObject(content_item)) {
             cJSON *user_it = cJSON_GetObjectItemCaseSensitive(content_item, "user");
             cJSON *status_it = cJSON_GetObjectItemCaseSensitive(content_item, "status");
             if (cJSON_IsString(user_it) && cJSON_IsString(status_it)) {
                 char buff[256];
                 snprintf(buff, sizeof(buff), "[server]: %s cambió su estado a %s",
                          user_it->valuestring, status_it->valuestring);
                 show_message(app, buff);
             }
         }
     }
     else if (strcmp(type, "error") == 0) {
         if (cJSON_IsString(content_item)) {
             char buff[256];
             snprintf(buff, sizeof(buff), "[ERROR]: %s", content_item->valuestring);
             show_message(app, buff);
         }
     }
     else if (strcmp(type, "user_info_response") == 0) {
         if (cJSON_IsObject(content_item)) {
             cJSON *ip_item = cJSON_GetObjectItemCaseSensitive(content_item, "ip");
             cJSON *st_item = cJSON_GetObjectItemCaseSensitive(content_item, "status");
             if (cJSON_IsString(ip_item) && cJSON_IsString(st_item)) {
                 char buff[256];
                 snprintf(buff, sizeof(buff), "Info de %s: IP=%s, STATUS=%s",
                          sender_item->valuestring, ip_item->valuestring, st_item->valuestring);
                 show_message(app, buff);
             }
         } else if (cJSON_IsString(content_item)) {
             show_message(app, content_item->valuestring);
         }
     }
     else {
         if (cJSON_IsString(content_item)) {
             char buff[256];
             snprintf(buff, sizeof(buff), "[%s]: %s", type, content_item->valuestring);
             show_message(app, buff);
         }
     }
 
     cJSON_Delete(root);
 }
 
 // ----------------- Callbacks de WebSockets -----------------
 
 static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len)
 {
     AppData *app = (AppData *)lws_context_user(lws_get_context(wsi));
     switch (reason) {
         case LWS_CALLBACK_CLIENT_ESTABLISHED: {
             show_message(app, "Conexión establecida con el servidor WebSocket");
 
             // Enviar mensaje de registro
             cJSON *root = cJSON_CreateObject();
             cJSON_AddStringToObject(root, "type", "register");
             cJSON_AddStringToObject(root, "sender", app->username);
             cJSON_AddItemToObject(root, "content", cJSON_CreateNull());
 
             char timestamp[64];
             get_timestamp(timestamp, sizeof(timestamp));
             cJSON_AddStringToObject(root, "timestamp", timestamp);
 
             send_cjson(app, root);
             cJSON_Delete(root);
 
             app->connected = 1;
             break;
         }
         case LWS_CALLBACK_CLIENT_RECEIVE: {
             if (in && len > 0) {
                 char *msg_in = g_strndup((const char *)in, len);
                 handle_server_message(app, msg_in);
                 g_free(msg_in);
             }
             break;
         }
         case LWS_CALLBACK_CLIENT_WRITEABLE: {
             pthread_mutex_lock(&app->lock_send_buffer);
             if (app->send_pending) {
                 unsigned char buf[LWS_PRE + WS_BUFFER];
                 size_t n = strlen(app->send_buffer);
                 memcpy(&buf[LWS_PRE], app->send_buffer, n);
                 int m = lws_write(wsi, &buf[LWS_PRE], n, LWS_WRITE_TEXT);
                 if (m < (int)n) {
                     show_message(app, "Error al enviar mensaje (parcial)");
                 }
                 app->send_pending = 0;
             }
             pthread_mutex_unlock(&app->lock_send_buffer);
             break;
         }
         case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
             show_message(app, "Error de conexión con el servidor");
             app->force_exit = 1;
             break;
         }
         case LWS_CALLBACK_CLOSED: {
             show_message(app, "Conexión cerrada");
             app->connected = 0;
             app->force_exit = 1;
             break;
         }
         default:
             break;
     }
     return 0;
 }
 
 // ----------------- Hilo para ejecutar lws_service() -----------------
 
 static void *ws_service_thread(void *arg) {
     AppData *app = (AppData *)arg;
     // Reducimos el tiempo de espera a 1 ms para mayor reactividad.
     while (!app->force_exit) {
         if (app->context) {
             lws_service(app->context, 1);
         }
     }
     return NULL;
 }
 
 // ----------------- Funciones para la Interfaz (GTK) -----------------
 
 // Botón "Conectar"
 static void on_button_connect_clicked(GtkButton *button, gpointer user_data) {
     AppData *app = (AppData *)user_data;
 
     const gchar *user = gtk_entry_get_text(GTK_ENTRY(app->entry_username));
     const gchar *ip   = gtk_entry_get_text(GTK_ENTRY(app->entry_ip));
     const gchar *port = gtk_entry_get_text(GTK_ENTRY(app->entry_port));
 
     if (strlen(user) == 0 || strlen(ip) == 0 || strlen(port) == 0) {
         show_message(app, "Por favor, llena todos los campos (usuario, IP, puerto)");
         return;
     }
 
     strncpy(app->username, user, sizeof(app->username)-1);
     strncpy(app->server_ip, ip, sizeof(app->server_ip)-1);
     app->server_port = atoi(port);
 
     struct lws_context_creation_info info;
     memset(&info, 0, sizeof(info));
     info.port = CONTEXT_PORT_NO_LISTEN;
     info.protocols = (struct lws_protocols[]){
         { "chat-protocol", ws_callback, 0, WS_BUFFER, 0, NULL, 0 },
         { NULL, NULL, 0, 0, 0, NULL, 0 }
     };
     info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
     info.user = app;
 
     app->context = lws_create_context(&info);
     if (!app->context) {
         show_message(app, "Error al crear contexto libwebsockets");
         return;
     }
 
     struct lws_client_connect_info ccinfo = {0};
     ccinfo.context = app->context;
     ccinfo.address = app->server_ip;
     ccinfo.port = app->server_port;
     ccinfo.path = "/chat";
     ccinfo.host = lws_canonical_hostname(app->context);
     ccinfo.origin = "origin";
     ccinfo.protocol = "chat-protocol";
     ccinfo.pwsi = &app->wsi;
 
     if (!lws_client_connect_via_info(&ccinfo)) {
         show_message(app, "Error al intentar conectar al servidor");
         lws_context_destroy(app->context);
         app->context = NULL;
         return;
     }
 
     pthread_t tid;
     pthread_create(&tid, NULL, ws_service_thread, (void *)app);
     pthread_detach(tid);
 
     show_message(app, "Intentando conectar...");
 }
 
 // Botón "Enviar"
 static void on_button_send_clicked(GtkButton *button, gpointer user_data) {
     AppData *app = (AppData *)user_data;
     if (!app->connected) {
         show_message(app, "No estás conectado al servidor");
         return;
     }
     const gchar *msg_text = gtk_entry_get_text(GTK_ENTRY(app->entry_message));
     if (!msg_text || strlen(msg_text) == 0)
         return;
  
     if (strncmp(msg_text, "/salir", 6) == 0) {
         cJSON *root = cJSON_CreateObject();
         cJSON_AddStringToObject(root, "type", "disconnect");
         cJSON_AddStringToObject(root, "sender", app->username);
         cJSON_AddStringToObject(root, "content", "Cierre de sesión");
         char timestamp[64];
         get_timestamp(timestamp, sizeof(timestamp));
         cJSON_AddStringToObject(root, "timestamp", timestamp);
  
         send_cjson(app, root);
         cJSON_Delete(root);
         return;
     }
  
     cJSON *root = cJSON_CreateObject();
     if (msg_text[0] == '@') {
         const char *space = strchr(msg_text, ' ');
         if (space) {
             size_t target_len = space - msg_text - 1;
             char target[128] = {0};
             strncpy(target, msg_text + 1, target_len);
             cJSON_AddStringToObject(root, "type", "private");
             cJSON_AddStringToObject(root, "target", target);
  
             const char *content = space + 1;
             cJSON_AddStringToObject(root, "content", content);
         } else {
             cJSON_AddStringToObject(root, "type", "broadcast");
             cJSON_AddStringToObject(root, "content", msg_text);
         }
     } else {
         cJSON_AddStringToObject(root, "type", "broadcast");
         cJSON_AddStringToObject(root, "content", msg_text);
     }
  
     cJSON_AddStringToObject(root, "sender", app->username);
     char timestamp[64];
     get_timestamp(timestamp, sizeof(timestamp));
     cJSON_AddStringToObject(root, "timestamp", timestamp);
  
     send_cjson(app, root);
     cJSON_Delete(root);
  
     gtk_entry_set_text(GTK_ENTRY(app->entry_message), "");
 }
  
 // Botón "Listar usuarios"
 static void on_button_list_users_clicked(GtkButton *button, gpointer user_data) {
     AppData *app = (AppData *)user_data;
     if (!app->connected) {
         show_message(app, "No estás conectado al servidor");
         return;
     }
     cJSON *root = cJSON_CreateObject();
     cJSON_AddStringToObject(root, "type", "list_users");
     cJSON_AddStringToObject(root, "sender", app->username);
     send_cjson(app, root);
     cJSON_Delete(root);
 }
  
 // Botón "Cambiar estado"
 static void on_button_change_status_clicked(GtkButton *button, gpointer user_data) {
     AppData *app = (AppData *)user_data;
     if (!app->connected) {
         show_message(app, "No estás conectado al servidor");
         return;
     }
     gchar *selected_status = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->combo_status));
     if (!selected_status) {
         show_message(app, "Selecciona un estado (ACTIVO, OCUPADO, INACTIVO)");
         return;
     }
  
     cJSON *root = cJSON_CreateObject();
     cJSON_AddStringToObject(root, "type", "change_status");
     cJSON_AddStringToObject(root, "sender", app->username);
     cJSON_AddStringToObject(root, "content", selected_status);
  
     char timestamp[64];
     get_timestamp(timestamp, sizeof(timestamp));
     cJSON_AddStringToObject(root, "timestamp", timestamp);
  
     send_cjson(app, root);
     cJSON_Delete(root);
     g_free(selected_status);
 }
  
 // ----------------- Creación de la Ventana Principal (GTK) -----------------
  
 static void activate(GtkApplication *gtk_app, gpointer user_data) {
     AppData *app = (AppData *)user_data;
  
     app->window_main = gtk_application_window_new(gtk_app);
     gtk_window_set_title(GTK_WINDOW(app->window_main), "Cliente Chat Completo");
     gtk_window_set_default_size(GTK_WINDOW(app->window_main), 700, 500);
  
     GtkWidget *grid = gtk_grid_new();
     gtk_container_add(GTK_CONTAINER(app->window_main), grid);
  
     GtkWidget *label_user = gtk_label_new("Usuario:");
     gtk_grid_attach(GTK_GRID(grid), label_user, 0, 0, 1, 1);
     app->entry_username = gtk_entry_new();
     gtk_grid_attach(GTK_GRID(grid), app->entry_username, 1, 0, 1, 1);
  
     GtkWidget *label_ip = gtk_label_new("IP Servidor:");
     gtk_grid_attach(GTK_GRID(grid), label_ip, 2, 0, 1, 1);
     app->entry_ip = gtk_entry_new();
     gtk_entry_set_text(GTK_ENTRY(app->entry_ip), "127.0.0.1");
     gtk_grid_attach(GTK_GRID(grid), app->entry_ip, 3, 0, 1, 1);
  
     GtkWidget *label_port = gtk_label_new("Puerto:");
     gtk_grid_attach(GTK_GRID(grid), label_port, 4, 0, 1, 1);
     app->entry_port = gtk_entry_new();
     gtk_entry_set_text(GTK_ENTRY(app->entry_port), "8080");
     gtk_grid_attach(GTK_GRID(grid), app->entry_port, 5, 0, 1, 1);
  
     app->btn_connect = gtk_button_new_with_label("Conectar");
     g_signal_connect(app->btn_connect, "clicked", G_CALLBACK(on_button_connect_clicked), app);
     gtk_grid_attach(GTK_GRID(grid), app->btn_connect, 6, 0, 1, 1);
  
     app->textview_chat = gtk_text_view_new();
     gtk_text_view_set_editable(GTK_TEXT_VIEW(app->textview_chat), FALSE);
     gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->textview_chat), GTK_WRAP_WORD_CHAR);
     GtkWidget *scroll_chat = gtk_scrolled_window_new(NULL, NULL);
     gtk_container_add(GTK_CONTAINER(scroll_chat), app->textview_chat);
     gtk_widget_set_size_request(scroll_chat, 680, 300);
     gtk_grid_attach(GTK_GRID(grid), scroll_chat, 0, 1, 7, 1);
  
     app->entry_message = gtk_entry_new();
     gtk_grid_attach(GTK_GRID(grid), app->entry_message, 0, 2, 5, 1);
  
     app->btn_send = gtk_button_new_with_label("Enviar");
     g_signal_connect(app->btn_send, "clicked", G_CALLBACK(on_button_send_clicked), app);
     gtk_grid_attach(GTK_GRID(grid), app->btn_send, 5, 2, 1, 1);
  
     app->btn_list_users = gtk_button_new_with_label("Listar usuarios");
     g_signal_connect(app->btn_list_users, "clicked", G_CALLBACK(on_button_list_users_clicked), app);
     gtk_grid_attach(GTK_GRID(grid), app->btn_list_users, 0, 3, 1, 1);
  
     app->combo_status = gtk_combo_box_text_new();
     gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->combo_status), STATUS_ACTIVE);
     gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->combo_status), STATUS_BUSY);
     gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->combo_status), STATUS_INACTIVE);
     gtk_grid_attach(GTK_GRID(grid), app->combo_status, 1, 3, 2, 1);
  
     app->btn_change_status = gtk_button_new_with_label("Cambiar estado");
     g_signal_connect(app->btn_change_status, "clicked", G_CALLBACK(on_button_change_status_clicked), app);
     gtk_grid_attach(GTK_GRID(grid), app->btn_change_status, 3, 3, 1, 1);
  
     gtk_widget_show_all(app->window_main);
 }
  
 int main(int argc, char **argv) {
     AppData app;
     memset(&app, 0, sizeof(app));
     pthread_mutex_init(&app.lock_send_buffer, NULL);
  
     GtkApplication *gtk_app = gtk_application_new("com.ejemplo.chatclient", G_APPLICATION_DEFAULT_FLAGS);
     g_signal_connect(gtk_app, "activate", G_CALLBACK(activate), &app);
  
     int status = g_application_run(G_APPLICATION(gtk_app), argc, argv);
     g_object_unref(gtk_app);
  
     pthread_mutex_destroy(&app.lock_send_buffer);
     return status;
 }
 