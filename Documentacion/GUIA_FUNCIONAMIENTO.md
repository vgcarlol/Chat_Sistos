# 💬 Proyecto Chat – Sistemas Operativos

Este proyecto implementa un sistema de chat en C con sockets y multithreading (pthread), incluyendo cliente y servidor. Cumple con los requisitos de la clase de Sistemas Operativos (UVG), e incluye soporte para despliegue en AWS.

---

## ✅ 1. Requisitos del entorno

### 📦 Instalaciones necesarias (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install build-essential libgtk-3-dev
```

 #### - Librerías utilizadas
    - pthread: para multithreading
    - sockets: incluidas en <sys/socket.h>, <arpa/inet.h>, etc.
    - GTK 3: solo si se usa interfaz gráfica en el cliente
    - lwebsockets: conexión de server-cliente

## 📁 2. Estructura de archivos recomendada
```
    Chat_Sistos/
    ├── chat_server/
    │   └── src/
    │       ├── server.c           # código puro del servidor
    │       └── Makefile           # (opcional)
    ├── chat_client/
    │   └── src/
    │       ├── chat_client_gtk.c  # cliente con GTK o consola
    │       └── Makefile
```

## 🛠️ 3. Compilar
#### 🧠 Servidor
```
cd chat_server/src
gcc server.c -o chat_server -lpthread
```
### 👤 Cliente (GTK)
```
cd chat_client/src
gcc chat_client_gtk.c -o chat_client_gtk \
    $(pkg-config --cflags --libs gtk+-3.0) \
    -lwebsockets -lcjson -lpthread

./chat_client_gtk
```
## 🚀 4. Ejecutar localmente
🖥️ Servidor
```
./chat_server 8082
```
### 👤 Cliente
```
./chat_client <nombre_usuario> <ip_del_servidor> 8082
```
Ejemplo:
```
./chat_client juan 127.0.0.1 8082
```
## 5. Despliegue en AWS con EC2
✅ Requisitos
 - Cuenta de AWS (Free Tier)
 - Instancia EC2 con Ubuntu 20.04 o superior
 - Puerto 8082 abierto (TCP) en el grupo de seguridad

### 📦 Pasos para configuración
ver pdf creación instancia: 
 - 📄 [Ver creación instancia (.PDF)](Lanzar_una_instancia_EC2.pdf)

1. Iniciá una instancia EC2 (tipo t2.micro, Ubuntu)


2. En el grupo de seguridad, agregá una regla para permitir tráfico TCP en el puerto 8082


3. ené la IP pública de la instancia (ejemplo: 3.88.45.122)

4. Subí los archivos con scp:

```
scp -i "tu_clave.pem" server.c ubuntu@<IP_PUBLICA>:~/Chat_Sistos/chat_server/src/
```

5. Conectate a la instancia:

```
ssh -i "tu_clave.pem" ubuntu@<IP_PUBLICA>
```

6. Instalá dependencias en la instancia:

```
sudo apt update
sudo apt install build-essential libgtk-3-dev
```

7. Compilá y ejecutá el servidor:

```
cd ~/Chat_Sistos/chat_server/src
gcc server.c -o chat_server -lwebsockets -lcjson -lpthread
./chat_server <puerto>
```

## 🌍 6. Conexión remota desde el cliente
En tu máquina local:

```
./chat_client <usuario> <IP_PUBLICA_EC2> 8082
```
Ejemplo:

```
./chat_client ana 3.88.45.122 8082
```
## 📜 7. Comandos del cliente compatibles

| Comando               | Función                                                      |
|-----------------------|--------------------------------------------------------------|
| `mensaje`             | Enviar mensaje general a todos los usuarios (broadcast)      |
| `@usuario mensaje`    | Enviar mensaje privado a un usuario                          |
| `/usuarios`           | Ver la lista de usuarios conectados                          |
| `/info <usuario>`     | Ver IP y estado de un usuario                                |
| `/estado ACTIVO`      | Cambiar estado a ACTIVO                                      |
| `/estado OCUPADO`     | Cambiar estado a OCUPADO                                     |
| `/estado INACTIVO`    | Cambiar estado manualmente a INACTIVO (opcional)             |
| `/salir`              | Salir del chat                                               |



---

