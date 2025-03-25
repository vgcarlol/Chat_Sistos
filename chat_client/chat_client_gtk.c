/******************************************************************************
 * Cliente de Chat con Interfaz Gráfica (GTK3) + libwebsockets
 *
 * - La aplicación pregunta al usuario (en la GUI) su nombre, IP y puerto.
 * - Muestra un área de texto donde se despliegan los mensajes y un campo para
 *   escribir mensajes.
 * - Se usa un hilo separado para el loop de libwebsockets (lws_service),
 *   evitando bloquear la interfaz.
 *
 * Compilar:
 *   gcc chat_client_gtk.c -o chat_client_gtk \
 *       $(pkg-config --cflags --libs gtk+-3.0) \
 *       -lwebsockets -lpthread
 *
 * Ejecutar:
 *   ./chat_client_gtk
 *
 * Nota: Este código implementa todas las funcionalidades básicas:
 * registro, envío de mensajes (broadcast y privados), y recepción de mensajes.
 ******************************************************************************/

 #include <gtk/gtk.h>
 #include <libwebsockets.h>
 #include <pthread.h>
 #include <string.h>
 #include <time.h>
 #include <stdlib.h>
 
 // Tamaños de buffers
 #define WS_BUFFER 2048
 
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
 
 // Estructura para pasar mensaje a la función idle
 typedef struct {
     AppData *app;
     char *msg;
 } IdleMsgData;
 
 // Prototipos
 static void on_button_connect_clicked(GtkButton *button, gpointer user_data);
 static void on_button_send_clicked(GtkButton *button, gpointer user_data);
 static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len);
 static void *ws_service_thread(void *arg);
 static void append_chat_text(AppData *app, const char *msg);
 static gboolean update_chat_idle(gpointer data);
 
 // ----------------- Funciones de ayuda -----------------
 
 // Obtener timestamp (ISO8601 simplificado)
 static void get_timestamp(char *buf, size_t len) {
     time_t now = time(NULL);
     struct tm *tm_info = localtime(&now);
     strftime(buf, len, "%Y-%m-%dT%H:%M:%S", tm_info);
 }
 
 // Función que agrega texto al TextView (se llama desde el hilo principal)
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
 
 // ----------------- Callbacks de WebSockets -----------------
 
 static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len)
 {
     // Recuperar AppData asignado en info.user
     AppData *app = (AppData *)lws_context_user(lws_get_context(wsi));
     switch (reason) {
         case LWS_CALLBACK_CLIENT_ESTABLISHED: {
             // Actualizar GUI indicando conexión establecida
             IdleMsgData *idle_data = g_new0(IdleMsgData, 1);
             idle_data->app = app;
             idle_data->msg = g_strdup("Conexión establecida con el servidor WebSocket");
             g_idle_add(update_chat_idle, idle_data);
             // Enviar mensaje de registro
             char timestamp[64];
             get_timestamp(timestamp, sizeof(timestamp));
             char msg[WS_BUFFER];
             snprintf(msg, sizeof(msg),
                      "{\"type\":\"register\",\"sender\":\"%s\",\"content\":null,\"timestamp\":\"%s\"}",
                      app->username, timestamp);
             pthread_mutex_lock(&app->lock_send_buffer);
             strncpy(app->send_buffer, msg, sizeof(app->send_buffer)-1);
             app->send_pending = 1;
             pthread_mutex_unlock(&app->lock_send_buffer);
             lws_callback_on_writable(wsi);
             app->connected = 1;
             break;
         }
         case LWS_CALLBACK_CLIENT_RECEIVE: {
             if (in && len > 0) {
                 char *msg_in = g_strndup((const char *)in, len);
                 IdleMsgData *idle_data = g_new0(IdleMsgData, 1);
                 idle_data->app = app;
                 idle_data->msg = msg_in; // ya duplicado
                 g_idle_add(update_chat_idle, idle_data);
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
                     IdleMsgData *idle_data = g_new0(IdleMsgData, 1);
                     idle_data->app = app;
                     idle_data->msg = g_strdup("Error al enviar mensaje (parcial)");
                     g_idle_add(update_chat_idle, idle_data);
                 }
                 app->send_pending = 0;
             }
             pthread_mutex_unlock(&app->lock_send_buffer);
             break;
         }
         case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
             IdleMsgData *idle_data = g_new0(IdleMsgData, 1);
             idle_data->app = app;
             idle_data->msg = g_strdup("Error de conexión con el servidor");
             g_idle_add(update_chat_idle, idle_data);
             app->force_exit = 1;
             break;
         }
         case LWS_CALLBACK_CLOSED: {
             IdleMsgData *idle_data = g_new0(IdleMsgData, 1);
             idle_data->app = app;
             idle_data->msg = g_strdup("Conexión cerrada");
             g_idle_add(update_chat_idle, idle_data);
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
     while (!app->force_exit) {
         if (app->context) {
             lws_service(app->context, 50);
         }
     }
     return NULL;
 }
 
 // ----------------- Callbacks de la Interfaz (GTK) -----------------
 
 // Al presionar "Conectar"
 static void on_button_connect_clicked(GtkButton *button, gpointer user_data) {
     AppData *app = (AppData *)user_data;
     // Leer valores de los campos
     const gchar *user = gtk_entry_get_text(GTK_ENTRY(app->entry_username));
     const gchar *ip   = gtk_entry_get_text(GTK_ENTRY(app->entry_ip));
     const gchar *port = gtk_entry_get_text(GTK_ENTRY(app->entry_port));
     if (strlen(user) == 0 || strlen(ip) == 0 || strlen(port) == 0) {
         append_chat_text(app, "Por favor, llena todos los campos (usuario, IP, puerto)");
         return;
     }
     strncpy(app->username, user, sizeof(app->username)-1);
     strncpy(app->server_ip, ip, sizeof(app->server_ip)-1);
     app->server_port = atoi(port);
     // Crear contexto libwebsockets
     struct lws_context_creation_info info;
     memset(&info, 0, sizeof(info));
     info.port = CONTEXT_PORT_NO_LISTEN;
     info.protocols = (struct lws_protocols[]){
         { "chat-protocol", ws_callback, 0, WS_BUFFER, 0, NULL, 0 },
         { NULL, NULL, 0, 0, 0, NULL, 0 }
     };
     info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
     info.user = app;  // Guardar AppData en el contexto
     app->context = lws_create_context(&info);
     if (!app->context) {
         append_chat_text(app, "Error al crear contexto libwebsockets");
         return;
     }
     // Crear la conexión
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
         append_chat_text(app, "Error al intentar conectar al servidor");
         lws_context_destroy(app->context);
         app->context = NULL;
         return;
     }
     // Lanzar el hilo de lws_service
     pthread_t tid;
     pthread_create(&tid, NULL, ws_service_thread, (void *)app);
     pthread_detach(tid);
     append_chat_text(app, "Intentando conectar...");
 }
  
 // Al presionar "Enviar"
 static void on_button_send_clicked(GtkButton *button, gpointer user_data) {
     AppData *app = (AppData *)user_data;
     if (!app->connected) {
         append_chat_text(app, "No estás conectado al servidor");
         return;
     }
     const gchar *msg_text = gtk_entry_get_text(GTK_ENTRY(app->entry_message));
     if (!msg_text || strlen(msg_text) == 0)
         return; // No enviar mensaje vacío
     // Si se escribe "/salir", enviar mensaje de desconexión
     if (strncmp(msg_text, "/salir", 6) == 0) {
         char timestamp[64];
         get_timestamp(timestamp, sizeof(timestamp));
         char msg[WS_BUFFER];
         snprintf(msg, sizeof(msg),
                  "{\"type\":\"disconnect\",\"sender\":\"%s\",\"content\":\"Cierre de sesión\",\"timestamp\":\"%s\"}",
                  app->username, timestamp);
         pthread_mutex_lock(&app->lock_send_buffer);
         strncpy(app->send_buffer, msg, sizeof(app->send_buffer)-1);
         app->send_pending = 1;
         pthread_mutex_unlock(&app->lock_send_buffer);
         if (app->wsi)
             lws_callback_on_writable(app->wsi);
         return;
     }
     // Preparar mensaje JSON: si inicia con '@', es mensaje privado; sino, broadcast.
     char final_msg[WS_BUFFER];
     char timestamp[64];
     get_timestamp(timestamp, sizeof(timestamp));
     if (msg_text[0] == '@') {
         const char *space = strchr(msg_text, ' ');
         if (space) {
             size_t target_len = space - msg_text - 1;
             char target[128] = {0};
             strncpy(target, msg_text + 1, target_len);
             const char *content = space + 1;
             snprintf(final_msg, sizeof(final_msg),
                      "{\"type\":\"private\",\"sender\":\"%s\",\"target\":\"%s\",\"content\":\"%s\",\"timestamp\":\"%s\"}",
                      app->username, target, content, timestamp);
         } else {
             snprintf(final_msg, sizeof(final_msg),
                      "{\"type\":\"broadcast\",\"sender\":\"%s\",\"content\":\"%s\",\"timestamp\":\"%s\"}",
                      app->username, msg_text, timestamp);
         }
     } else {
         snprintf(final_msg, sizeof(final_msg),
                  "{\"type\":\"broadcast\",\"sender\":\"%s\",\"content\":\"%s\",\"timestamp\":\"%s\"}",
                  app->username, msg_text, timestamp);
     }
     pthread_mutex_lock(&app->lock_send_buffer);
     strncpy(app->send_buffer, final_msg, sizeof(app->send_buffer)-1);
     app->send_pending = 1;
     pthread_mutex_unlock(&app->lock_send_buffer);
     if (app->wsi)
         lws_callback_on_writable(app->wsi);
     // Limpiar el campo de mensaje
     gtk_entry_set_text(GTK_ENTRY(app->entry_message), "");
 }
  
 // ----------------- Creación de la Ventana Principal (GTK) -----------------
  
 static void activate(GtkApplication *gtk_app, gpointer user_data) {
     AppData *app = (AppData *)user_data;
     // Crear ventana principal
     app->window_main = gtk_application_window_new(gtk_app);
     gtk_window_set_title(GTK_WINDOW(app->window_main), "Cliente Chat");
     gtk_window_set_default_size(GTK_WINDOW(app->window_main), 600, 400);
     // Contenedor principal
     GtkWidget *grid = gtk_grid_new();
     gtk_container_add(GTK_CONTAINER(app->window_main), grid);
     // Fila 0: Campos de conexión y botón Conectar
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
     // Fila 1: Área de chat (TextView)
     app->textview_chat = gtk_text_view_new();
     gtk_text_view_set_editable(GTK_TEXT_VIEW(app->textview_chat), FALSE);
     gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->textview_chat), GTK_WRAP_WORD_CHAR);
     GtkWidget *scroll_chat = gtk_scrolled_window_new(NULL, NULL);
     gtk_container_add(GTK_CONTAINER(scroll_chat), app->textview_chat);
     gtk_widget_set_size_request(scroll_chat, 580, 250);
     gtk_grid_attach(GTK_GRID(grid), scroll_chat, 0, 1, 7, 1);
     // Fila 2: Campo de mensaje y botón Enviar
     app->entry_message = gtk_entry_new();
     gtk_grid_attach(GTK_GRID(grid), app->entry_message, 0, 2, 6, 1);
     app->btn_send = gtk_button_new_with_label("Enviar");
     g_signal_connect(app->btn_send, "clicked", G_CALLBACK(on_button_send_clicked), app);
     gtk_grid_attach(GTK_GRID(grid), app->btn_send, 6, 2, 1, 1);
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
 