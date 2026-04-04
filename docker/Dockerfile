# ─────────────────────────────────────────────
# Dockerfile — Servidor C de monitoreo IoT
#
# Imagen base: gcc oficial (incluye gcc, make, libc)
# Proceso:
#   1. Copia el código fuente
#   2. Compila con make
#   3. Ejecuta el servidor en el puerto 8080
#
# Variables de entorno disponibles:
#   AUTH_HOST  → hostname del servicio de auth (default: localhost)
#   AUTH_PORT  → puerto del servicio de auth   (default: 5001)
# ─────────────────────────────────────────────

FROM gcc:latest

# Directorio de trabajo dentro del contenedor
WORKDIR /app

# Copiar todo el código fuente del servidor
COPY server/ .

# Copiar la carpeta web con los HTML físicos
COPY web/ web/

# Compilar
RUN make

# Crear directorio para los logs
RUN mkdir -p /logs

# Exponer el puerto TCP del protocolo IoT
EXPOSE 8080

# Variables de entorno por defecto
# En docker-compose estas se sobreescriben automáticamente
ENV AUTH_HOST=auth-server
ENV AUTH_PORT=5001

# Punto de entrada:
# El servidor recibe puerto y archivo de logs como argumentos
CMD ["./server", "8080", "/logs/server.log"]