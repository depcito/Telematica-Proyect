#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

/*
 * http_server.h — Servidor HTTP básico para la interfaz web del sistema IoT
 *
 * Escucha en el puerto HTTP_PORT (por defecto 8081) en un hilo separado.
 * Responde a:
 *   GET /          → index.html  (página de login)
 *   GET /status    → estado del sistema en texto plano
 *   GET /health    → verificación rápida de que el servidor HTTP está vivo
 *
 * No depende de ninguna librería externa — solo sockets POSIX.
 */

#include "protocol.h"

/* Puerto del servidor HTTP — separado del puerto TCP del protocolo */
#define HTTP_PORT 8081

/* Tamaño del buffer para leer peticiones HTTP */
#define HTTP_BUFFER_SIZE 4096

/*
 * Inicia el servidor HTTP en un hilo separado.
 * Recibe un puntero a ServerState para poder leer el estado del sistema.
 * Retorna 0 si el hilo se lanzó correctamente, -1 si hubo error.
 */
int http_server_start(ServerState *state);

#endif /* HTTP_SERVER_H */