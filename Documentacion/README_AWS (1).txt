# README_AWS.txt

Guía de configuración básica de instancia EC2 para el proyecto "chat-servidor".

## ✅ Configuración utilizada

- **AMI**: Ubuntu Server 22.04 LTS (64-bit, x86)
- **Tipo de instancia**: t2.micro (Free Tier)
- **Par de claves**: chat2025.pem
- **Grupo de seguridad**: launch-wizard-1

## 🔐 Reglas de entrada del grupo de seguridad

| Tipo     | Protocolo | Puerto | Origen     |
|----------|-----------|--------|------------|
| SSH      | TCP       | 22     | 0.0.0.0/0  |
| Personal | TCP       | 8080   | 0.0.0.0/0  |

## 💡 Recomendaciones

- **No compartas** tu archivo `.pem`.
- Usa `chmod 400 chat2025.pem` para restringir permisos antes de conectarte.
- El archivo `.pem` solo se descarga una vez.
- Usa `ubuntu@IP_PUBLICA` para conectar por SSH.

## 🧠 Conexión SSH

```bash
ssh -i chat2025.pem ubuntu@IP_PUBLICA
```

---

¡Listo para trabajar en tu instancia!