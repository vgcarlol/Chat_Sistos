# README_stop_start.txt

Este archivo describe cómo detener y volver a iniciar una instancia EC2 de AWS para evitar costos innecesarios y reanudar el trabajo posteriormente.

## 🚫 Detener instancia

1. Ir al panel de EC2.
2. Seleccionar la instancia (ej: chat-servidor).
3. Ir a "Estado de la instancia" > "Detener instancia".
4. Confirmar la acción.
5. Verificar que el estado cambió a "Detenida".

> ⚠️ Esto detiene la máquina, pero **el volumen (EBS)** y **la IP pública** se liberan (cambiarán al reiniciar).

---

## ▶️ Iniciar instancia

1. Desde el panel de EC2, seleccionar la instancia detenida.
2. Ir a "Estado de la instancia" > "Iniciar instancia".
3. Esperar a que esté "En ejecución".
4. Copiar la nueva **Dirección IP pública**.
5. Conectarse nuevamente vía SSH:
   ```bash
   ssh -i chat2025.pem ubuntu@NUEVA_IP_PUBLICA
   ```

---

## 🔁 Nota

Si no puedes conectarte por SSH:
- Verifica el grupo de seguridad (debe permitir tráfico en el puerto 22).
- Asegúrate que el archivo `.pem` tenga permisos seguros: `chmod 400 chat2025.pem`