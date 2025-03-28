# Proyecto: Sistema de Chat en C con Sockets y GTK

Este proyecto implementa un sistema de chat distribuido usando el lenguaje C, con comunicación a través de WebSockets (usando `libwebsockets`) y una interfaz gráfica para el cliente desarrollada con GTK.

---

## 🧩 Estructura del Proyecto

```
├── server.c               # Código fuente del servidor
├── chat_client_gtk.c     # Código fuente del cliente con interfaz GTK
├── server_comentado.c    # Versión comentada del servidor (para estudio)
├── README.md             # Este archivo
```

---

## 🚀 Compilación

### Cliente (GTK + WebSockets):

```bash
sudo apt install libgtk-3-dev libwebsockets-dev libcjson-dev

gcc chat_client_gtk.c -o chat_client_gtk \
    $(pkg-config --cflags --libs gtk+-3.0) \
    -lwebsockets -lcjson -lpthread
```

### Servidor (WebSockets):

```bash
sudo apt install libwebsockets-dev libcjson-dev

gcc server.c -o chat_server \
    -lwebsockets -lcjson -lpthread
```

---

## 📦 Ejecución

### Servidor

```bash
./chat_server 8080
```

```
./chat_server <PUERTO>
```

### Cliente

```bash
./chat_client_gtk <nombre_usuario> <IP_SERVIDOR> <PUERTO>
```

---

## 🛠️ Funcionalidades del Proyecto

- Registro único de usuarios
- Envío de mensajes broadcast
- Mensajes privados entre usuarios
- Cambio de estado: ACTIVO, OCUPADO, INACTIVO
- Listado de usuarios conectados
- Solicitud de información de otros usuarios
- Detección de desconexiones

---

## 🧪 Recomendaciones de desarrollo

- Asegúrate de que no existan usuarios con el mismo nombre o IP conectados al mismo tiempo.
- Puedes correr el servidor en una instancia EC2 (AWS Free Tier).
- Prueba en red local antes de pasar a pruebas en la nube.

---

## 👤 Autores

- Carlos Valladares 221164
- Brandon Reyes 22992

---



Event Loop + Callback Dispatcher


    +-------------------------+
          |    lws_service(ctx,5)   |   <--- 1 solo hilo

    +-------------------------+
                          |
       +-------------+-------------------------------+
       |                                                        |
[Socket Cliente A]                         [Socket Cliente B]
       |                                                        |
evento en A (msg)                   evento en B (disconnect)
       |                                                         |
callback_chat()                                    callback_chat()
 LWS_CALLBACK_RECEIVE       LWS_CALLBACK_CLOSED



Conexiones múltiples  --->  [libwebsockets]  --->  [1 solo hilo manejando todo por turnos]





* Hay un ciclo principal (lws_service()) que se ejecuta constantemente.
* Cada vez que ocurre un evento (como una nueva conexión, un mensaje recibido, etc.), se llama una función de callback que tú defines.

* La librería no usa un hilo por conexión, sino que multiplexa todas las conexiones activas en ese ciclo de eventos.
