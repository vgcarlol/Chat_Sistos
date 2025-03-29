# Proyecto: Sistema de Chat en C con Sockets y GTK
## 👤 Autores

- Carlos Valladares 221164
- Brandon Reyes 22992

---

Este proyecto implementa un sistema de chat distribuido usando el lenguaje C, con comunicación a través de WebSockets (usando `libwebsockets`) y una interfaz gráfica para el cliente desarrollada con GTK.

---

## 🧩 VER GUIA FUNCIONAMIENTO
ver GUIA DE FUNCIONAMIENTO: 
##  - 📄 [Ver GUIA DE FUNCIONAMIENTO (PDF)](Documentacion/GUIA_FUNCIONAMIENTO.pdf)
##  - 📄 [Ver GUIA DE FUNCIONAMIENTO (.md)](Documentacion/GUIA_FUNCIONAMIENTO.md)


GUIA_FUNCIONAMIENTO.pdf
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


