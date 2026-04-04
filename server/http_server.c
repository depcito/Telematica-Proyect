/*
 * http_server.c — Servidor HTTP básico para la interfaz web del sistema IoT
 *
 * Corre en un hilo separado lanzado desde main() en server.c.
 * Parsea la primera línea del request HTTP para determinar la ruta,
 * y responde con cabeceras HTTP válidas + el contenido correspondiente.
 *
 * Rutas soportadas:
 *   GET /         → index.html  (página de login)
 *   GET /status   → estado del sistema en JSON
 *   GET /health   → {"status":"ok"}
 *   Cualquier otra → 404 Not Found
 */

#include "http_server.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

/* ─────────────────────────────────────────────
   ESTADO GLOBAL — puntero al ServerState de server.c
───────────────────────────────────────────── */
static ServerState *g_state = NULL;

/* ─────────────────────────────────────────────
   UTILIDADES HTTP
───────────────────────────────────────────── */

/*
 * send_http_response() — Envía una respuesta HTTP completa al cliente.
 *
 * Parámetros:
 *   fd           — socket del cliente HTTP
 *   status_code  — código HTTP: 200, 404, 405, etc.
 *   status_text  — texto del código: "OK", "Not Found", etc.
 *   content_type — "text/html", "application/json", "text/plain"
 *   body         — contenido de la respuesta
 *   body_len     — longitud del body en bytes
 */
static void send_http_response(int fd,
                               int status_code,
                               const char *status_text,
                               const char *content_type,
                               const char *body,
                               size_t body_len)
{
    char headers[512];
    int hlen = snprintf(headers, sizeof(headers),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: %s; charset=UTF-8\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "Access-Control-Allow-Origin: *\r\n" /* CORS para desarrollo */
                        "\r\n",
                        status_code, status_text,
                        content_type,
                        body_len);

    send(fd, headers, hlen, 0);
    if (body && body_len > 0)
        send(fd, body, body_len, 0);
}

static void send_file_response(int fd, int status_code, const char *status_text,
                               const char *content_type, const char *filepath)
{
    FILE *file = fopen(filepath, "r");
    if (!file) {
        const char *body = "<h2>404 - Archivo no encontrado</h2>";
        send_http_response(fd, 404, "Not Found", "text/html", body, strlen(body));
        return;
    }

    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *body = malloc(fsize + 1);
    if (body) {
        fread(body, 1, fsize, file);
        body[fsize] = '\0';
        
        send_http_response(fd, status_code, status_text, content_type, body, fsize);
        free(body);
    } else {
        const char *err = "500 - Error Interno del Servidor";
        send_http_response(fd, 500, "Internal Server Error", "text/plain", err, strlen(err));
    }
    fclose(file);
}

/*
 * build_status_json() — Construye un JSON con el estado actual del sistema.
 *
 * Lee g_state (puntero al ServerState del servidor principal).
 * Formato de salida:
 * {
 *   "sensors": 5,
 *   "operators": 2,
 *   "alerts": 7,
 *   "uptime": 3600,
 *   "sensor_list": [
 *     {"id":"temp_01","type":"TEMPERATURE","status":"online"},
 *     ...
 *   ]
 * }
 */
static void build_status_json(char *buf, size_t buf_size)
{
    if (!g_state)
    {
        snprintf(buf, buf_size, "{\"error\":\"state not available\"}");
        return;
    }

    /*
     * Lectura "best effort" del estado — en producción se pasaría el mutex.
     * Para este proyecto educativo es aceptable.
     */
    long uptime = (long)(time(NULL) - g_state->start_time);
    int sensors = g_state->sensor_count;
    int operators = 0;
    int alerts = g_state->total_alerts;

    for (int i = 0; i < g_state->operator_count; i++)
        if (g_state->operators[i].active)
            operators++;

    int offset = 0;
    offset += snprintf(buf + offset, buf_size - offset,
                       "{\n"
                       "  \"sensors\": %d,\n"
                       "  \"operators\": %d,\n"
                       "  \"alerts\": %d,\n"
                       "  \"uptime\": %ld,\n"
                       "  \"sensor_list\": [\n",
                       sensors, operators, alerts, uptime);

    for (int i = 0; i < g_state->sensor_count && offset < (int)buf_size - 100; i++)
    {
        offset += snprintf(buf + offset, buf_size - offset,
                           "    {\"id\":\"%s\",\"type\":\"%s\",\"status\":\"%s\",\"alert_level\":\"%s\",\"alert_msg\":\"%s\"}%s\n",
                           g_state->sensors[i].id,
                           g_state->sensors[i].type,
                           g_state->sensors[i].online ? "online" : "offline",
                           strlen(g_state->sensors[i].last_alert_level) > 0 ? g_state->sensors[i].last_alert_level : "INFO",
                           strlen(g_state->sensors[i].last_alert_msg) > 0 ? g_state->sensors[i].last_alert_msg : "Normal",
                           (i < g_state->sensor_count - 1) ? "," : "");
    }

    offset += snprintf(buf + offset, buf_size - offset,
                       "  ]\n"
                       "}\n");
}

/* ─────────────────────────────────────────────
   MANEJADOR DE PETICIÓN HTTP
───────────────────────────────────────────── */

/*
 * handle_http_client() — Procesa una conexión HTTP completa.
 *
 * Lee la primera línea del request (ej: "GET /status HTTP/1.1"),
 * extrae el método y la ruta, y despacha la respuesta correspondiente.
 *
 * No mantiene conexiones persistentes (HTTP/1.0 style):
 * cada petición abre una conexión, recibe respuesta y se cierra.
 */
static void handle_http_client(int client_fd, const char *client_ip)
{
    char buf[HTTP_BUFFER_SIZE] = {0};
    int n = recv(client_fd, buf, sizeof(buf) - 1, 0);

    if (n <= 0)
    {
        close(client_fd);
        return;
    }
    buf[n] = '\0';

    /* Parsear la primera línea: "GET /ruta HTTP/1.x" */
    char method[16] = {0};
    char path[256] = {0};
    char version[16] = {0};

    if (sscanf(buf, "%15s %255s %15s", method, path, version) < 2)
    {
        /* Request malformado */
        const char *body = "Bad Request";
        send_http_response(client_fd, 400, "Bad Request",
                           "text/plain", body, strlen(body));
        close(client_fd);
        return;
    }

    log_request(client_ip, 0, buf[0] ? buf : "HTTP request");

    /* Solo aceptamos GET */
    if (strcmp(method, "GET") != 0)
    {
        const char *body = "Method Not Allowed";
        send_http_response(client_fd, 405, "Method Not Allowed",
                           "text/plain", body, strlen(body));
        log_response(client_ip, HTTP_PORT, "405 Method Not Allowed");
        close(client_fd);
        return;
    }

    /* ── Enrutamiento ── */

    /* GET / → página de login */
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
    {
        /* Ahora lee el archivo físico */
        send_file_response(client_fd, 200, "OK", "text/html", "web/index.html");
        log_response(client_ip, HTTP_PORT, "200 GET /");
    }

    /* GET /status → JSON con estado del sistema */
    else if (strcmp(path, "/status") == 0)
    {
        char json_buf[4096] = {0};
        build_status_json(json_buf, sizeof(json_buf));
        send_http_response(client_fd, 200, "OK", "application/json", json_buf, strlen(json_buf));
        log_response(client_ip, HTTP_PORT, "200 GET /status");
    }

    /* GET /dashboard → página visual de estado */
    else if (strcmp(path, "/dashboard") == 0 || strcmp(path, "/status.html") == 0)
    {
        /* Ahora lee el archivo físico */
        send_file_response(client_fd, 200, "OK", "text/html", "web/status.html");
        log_response(client_ip, HTTP_PORT, "200 GET /dashboard");
    }

    /* GET /health → verificación rápida */
    else if (strcmp(path, "/health") == 0)
    {
        const char *body = "{\"status\":\"ok\",\"service\":\"iot-monitoring\"}\n";
        send_http_response(client_fd, 200, "OK",
                           "application/json",
                           body, strlen(body));
        log_response(client_ip, HTTP_PORT, "200 GET /health");
    }

    /* Cualquier otra ruta → 404 */
    else
    {
        const char *body =
            "<!DOCTYPE html><html><body>"
            "<h2>404 — Not Found</h2>"
            "<p>Rutas disponibles: / &nbsp; /status &nbsp; /health</p>"
            "</body></html>";
        send_http_response(client_fd, 404, "Not Found",
                           "text/html",
                           body, strlen(body));
        log_response(client_ip, HTTP_PORT, "404 Not Found");
    }

    close(client_fd);
}

/* ─────────────────────────────────────────────
   HILO PRINCIPAL DEL SERVIDOR HTTP
───────────────────────────────────────────── */

/*
 * http_server_thread() — Bucle principal del servidor HTTP.
 *
 * Crea su propio socket TCP en HTTP_PORT, acepta conexiones,
 * y lanza handle_http_client() por cada petición recibida.
 * Corre en su propio hilo — no bloquea el servidor TCP principal.
 */
static void *http_server_thread(void *arg)
{
    (void)arg; /* no usamos el argumento */

    /* Crear socket */
    int http_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (http_fd < 0)
    {
        log_error("http_server", "No se pudo crear el socket HTTP");
        return NULL;
    }

    int opt = 1;
    setsockopt(http_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind al puerto HTTP */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(HTTP_PORT);

    if (bind(http_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        log_error("http_server", "bind() falló en puerto HTTP");
        close(http_fd);
        return NULL;
    }

    if (listen(http_fd, 16) < 0)
    {
        log_error("http_server", "listen() falló");
        close(http_fd);
        return NULL;
    }

    printf("[HTTP]   Servidor web en puerto %d\n", HTTP_PORT);
    printf("[HTTP]   Abrir en navegador: http://localhost:%d\n", HTTP_PORT);

    /* Bucle de aceptación */
    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(http_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);
        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue;
            log_error("http_server", "accept() falló");
            continue;
        }

        /* Obtener IP del cliente */
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr,
                  client_ip, sizeof(client_ip));

        /*
         * Atendemos la petición HTTP en el mismo hilo (sin crear uno nuevo).
         * Las peticiones HTTP son rápidas (no mantienen conexión abierta),
         * así que esto es aceptable para este proyecto.
         * En producción se lanzaría un hilo por petición.
         */
        handle_http_client(client_fd, client_ip);
    }

    close(http_fd);
    return NULL;
}

/* ─────────────────────────────────────────────
   API PÚBLICA
───────────────────────────────────────────── */

/*
 * http_server_start() — Lanza el servidor HTTP en un hilo separado.
 *
 * Guarda el puntero al ServerState para que el hilo HTTP
 * pueda leer el estado del sistema cuando alguien pida /status.
 *
 * Retorna 0 si el hilo se creó correctamente, -1 si hubo error.
 */
int http_server_start(ServerState *state)
{
    g_state = state;

    pthread_t tid;
    if (pthread_create(&tid, NULL, http_server_thread, NULL) != 0)
    {
        log_error("http_server_start", "No se pudo crear el hilo HTTP");
        return -1;
    }
    pthread_detach(tid);
    return 0;
}