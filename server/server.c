/*
 * server.c — Servidor central de monitoreo IoT
 *
 * Uso: ./server <puerto> <archivo_logs>
 *
 * Compilar:
 *   gcc -o server server.c logger.c -lpthread -Wall -Wextra
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <netdb.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "protocol.h"
#include "logger.h"
#include "http_server.h"

/* ─────────────────────────────────────────────
   ESTADO GLOBAL (protegido por mutex)
───────────────────────────────────────────── */
static ServerState state;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ─────────────────────────────────────────────
   UTILIDADES
───────────────────────────────────────────── */

/* Elimina el \n o \r\n del final de un buffer */
static void strip_newline(char *buf)
{
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
    {
        buf[--len] = '\0';
    }
}

/* Devuelve 1 si el tipo de sensor es válido */
static int valid_sensor_type(const char *type)
{
    return (strcmp(type, TYPE_TEMPERATURE) == 0 ||
            strcmp(type, TYPE_VIBRATION) == 0 ||
            strcmp(type, TYPE_ENERGY) == 0 ||
            strcmp(type, TYPE_HUMIDITY) == 0 ||
            strcmp(type, TYPE_STATUS) == 0);
}

/* Devuelve la unidad esperada para un tipo de sensor */
static const char *unit_for_type(const char *type)
{
    if (strcmp(type, TYPE_TEMPERATURE) == 0)
        return UNIT_CELSIUS;
    if (strcmp(type, TYPE_VIBRATION) == 0)
        return UNIT_MM_S;
    if (strcmp(type, TYPE_ENERGY) == 0)
        return UNIT_WATTS;
    if (strcmp(type, TYPE_HUMIDITY) == 0)
        return UNIT_PERCENT;
    if (strcmp(type, TYPE_STATUS) == 0)
        return UNIT_BINARY;
    return "";
}

/* Busca un sensor por ID. Devuelve puntero o NULL. DEBE llamarse con mutex. */
static Sensor *find_sensor(const char *id)
{
    for (int i = 0; i < state.sensor_count; i++)
    {
        if (strcmp(state.sensors[i].id, id) == 0)
            return &state.sensors[i];
    }
    return NULL;
}

/* ─────────────────────────────────────────────
   ALERTAS: enviar a todos los operadores activos
───────────────────────────────────────────── */
static void broadcast_alert(const char *level, const char *sensor_id, const char *message)
{
    char alert_msg[BUFFER_SIZE];
    snprintf(alert_msg, sizeof(alert_msg),
             "%s %s %s %s\n", ALERT_CMD, level, sensor_id, message);

    pthread_mutex_lock(&state_mutex);
    for (int i = 0; i < state.operator_count; i++)
    {
        if (state.operators[i].active)
        {
            send(state.operators[i].socket_fd, alert_msg, strlen(alert_msg), 0);
        }
    }
    state.total_alerts++;
    pthread_mutex_unlock(&state_mutex);

    log_alert(level, sensor_id, message);
}

/* ─────────────────────────────────────────────
   DETECCIÓN DE ANOMALÍAS
───────────────────────────────────────────── */
static int check_anomaly(const char *type, double value,
                         const char *sensor_id)
{
    const char *level = NULL;
    char msg[256] = {0};

    if (strcmp(type, TYPE_TEMPERATURE) == 0)
    {
        if (value > TEMP_CRITICAL)
        {
            level = ALERT_CRITICAL;
            snprintf(msg, sizeof(msg),
                     "Temperature_above_critical:%.1f_CELSIUS", value);
        }
        else if (value > TEMP_WARNING)
        {
            level = ALERT_WARNING;
            snprintf(msg, sizeof(msg),
                     "Temperature_above_threshold:%.1f_CELSIUS", value);
        }
    }
    else if (strcmp(type, TYPE_VIBRATION) == 0)
    {
        if (value > VIB_CRITICAL)
        {
            level = ALERT_CRITICAL;
            snprintf(msg, sizeof(msg),
                     "Vibration_critical:%.2f_MM_S", value);
        }
        else if (value > VIB_WARNING)
        {
            level = ALERT_WARNING;
            snprintf(msg, sizeof(msg),
                     "Vibration_above_threshold:%.2f_MM_S", value);
        }
    }
    else if (strcmp(type, TYPE_ENERGY) == 0)
    {
        if (value > ENERGY_CRITICAL)
        {
            level = ALERT_CRITICAL;
            snprintf(msg, sizeof(msg),
                     "Energy_consumption_critical:%.1f_WATTS", value);
        }
        else if (value > ENERGY_WARNING)
        {
            level = ALERT_WARNING;
            snprintf(msg, sizeof(msg),
                     "Energy_consumption_high:%.1f_WATTS", value);
        }
    }
    else if (strcmp(type, TYPE_HUMIDITY) == 0)
    {
        if (value > HUM_CRITICAL)
        {
            level = ALERT_CRITICAL;
            snprintf(msg, sizeof(msg),
                     "Humidity_critical:%.1f_PERCENT", value);
        }
        else if (value > HUM_WARNING)
        {
            level = ALERT_WARNING;
            snprintf(msg, sizeof(msg),
                     "Humidity_above_threshold:%.1f_PERCENT", value);
        }
    }
    else if (strcmp(type, TYPE_STATUS) == 0)
    {
        if ((int)value == 0)
        {
            level = ALERT_CRITICAL;
            snprintf(msg, sizeof(msg), "Equipment_stopped:0_BINARY");
        }
    }

    if (level)
    {
        broadcast_alert(level, sensor_id, msg);
        return 1; /* anomalía detectada */
    }
    return 0;
}

/* ─────────────────────────────────────────────
   VALIDACIÓN DE TOKEN CON SERVICIO DE AUTH
   El auth corre en el mismo host, puerto 5001.
   GET http://auth-server:5001/validate?token=xxx
───────────────────────────────────────────── */
static int validate_token(const char *token, char *out_username, size_t out_size)
{
    /* Resolvemos el host por nombre, no por IP */
    /* Leer host y puerto del auth desde variables de entorno.
     * En Docker:    AUTH_HOST=auth-server  (nombre del servicio en docker-compose)
     * En local:     AUTH_HOST no definida  → usa "localhost" por defecto
     * En AWS:       AUTH_HOST=auth-server  (mismo docker-compose)            */
    const char *auth_host_env = getenv("AUTH_HOST");
    const char *auth_host = (auth_host_env && auth_host_env[0] != '\0')
                                ? auth_host_env
                                : "localhost";

    const char *auth_port_env = getenv("AUTH_PORT");
    int auth_port = (auth_port_env && auth_port_env[0] != '\0')
                        ? atoi(auth_port_env)
                        : 5001;

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", auth_port);

    if (getaddrinfo(auth_host, port_str, &hints, &res) != 0)
    {
        log_error("validate_token", "No se pudo resolver auth-server");
        return 0;
    }

    int sock = socket(res->ai_family, res->ai_socktype, 0);
    if (sock < 0)
    {
        freeaddrinfo(res);
        return 0;
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) < 0)
    {
        freeaddrinfo(res);
        close(sock);
        log_error("validate_token", "No se pudo conectar al servicio de auth");
        return 0;
    }
    freeaddrinfo(res);

    /* Enviamos petición HTTP GET */
    char request[512];
    snprintf(request, sizeof(request),
             "GET /validate?token=%s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n\r\n",
             token, auth_host);
    send(sock, request, strlen(request), 0);

    /* Leemos respuesta */
    char response[2048] = {0};
    int total = 0, n;
    while ((n = recv(sock, response + total,
                     sizeof(response) - total - 1, 0)) > 0)
        total += n;
    close(sock);
    response[total] = '\0';

    /* Buscamos "valid": true en el JSON del body */
    if (strstr(response, "\"valid\": true") ||
        strstr(response, "\"valid\":true"))
    {
        /* Extraemos username del JSON: "username": "jperez" */
        char *p = strstr(response, "\"username\"");
        if (p)
        {
            p = strchr(p, ':');
            if (p)
            {
                p++;
                while (*p == ' ' || *p == '"')
                    p++;
                int i = 0;
                while (*p && *p != '"' && i < (int)out_size - 1)
                    out_username[i++] = *p++;
                out_username[i] = '\0';
            }
        }
        return 1;
    }
    return 0;
}

/* ─────────────────────────────────────────────
   MANEJADORES DE COMANDOS
───────────────────────────────────────────── */

/* REGISTER SENSOR <id> <type> */
static void handle_register_sensor(int fd, const char *client_ip, int client_port,
                                   char *args)
{
    char id[64] = {0}, type[32] = {0};
    if (sscanf(args, "%63s %31s", id, type) != 2)
    {
        SEND_MSG(fd, RESP_ERR_UNKNOWN_CMD);
        return;
    }
    if (!valid_sensor_type(type))
    {
        char resp[128];
        snprintf(resp, sizeof(resp), "%s %s", RESP_ERR_INVALID_TYPE, type);
        SEND_MSG(fd, resp);
        log_response(client_ip, client_port, resp);
        return;
    }

    pthread_mutex_lock(&state_mutex);

    Sensor *existing = find_sensor(id);
    if (existing)
    {
        existing->online = 1;
        existing->socket_fd_sensor = fd; /* actualizar fd si reconecta */
        pthread_mutex_unlock(&state_mutex);
        char resp[128];
        snprintf(resp, sizeof(resp), "%s %s", RESP_ERR_ALREADY_REG, id);
        SEND_MSG(fd, resp);
        log_response(client_ip, client_port, resp);
        return;
    }

    if (state.sensor_count >= MAX_CLIENTS)
    {
        pthread_mutex_unlock(&state_mutex);
        SEND_MSG(fd, "ERROR SERVER_FULL");
        return;
    }

    Sensor *s = &state.sensors[state.sensor_count++];
    strncpy(s->id, id, sizeof(s->id) - 1);
    strncpy(s->type, type, sizeof(s->type) - 1);
    strncpy(s->unit, unit_for_type(type), sizeof(s->unit) - 1);
    s->online = 1;
    s->history_count = 0;
    s->history_index = 0;

    pthread_mutex_unlock(&state_mutex);

    char resp[128];
    snprintf(resp, sizeof(resp), "%s %s", RESP_OK_REGISTERED, id);
    SEND_MSG(fd, resp);
    log_response(client_ip, client_port, resp);
}

/* REGISTER OPERATOR <username> <token> */
static void handle_register_operator(int fd, const char *client_ip, int client_port,
                                     char *args)
{
    char username[64] = {0}, token[256] = {0};
    if (sscanf(args, "%63s %255s", username, token) != 2)
    {
        SEND_MSG(fd, RESP_ERR_UNKNOWN_CMD);
        return;
    }

    char validated_user[64] = {0};
    if (!validate_token(token, validated_user, sizeof(validated_user)))
    {
        SEND_MSG(fd, RESP_ERR_INVALID_TOKEN);
        log_response(client_ip, client_port, RESP_ERR_INVALID_TOKEN);
        return;
    }

    pthread_mutex_lock(&state_mutex);

    /* Verificar si ya está registrado */
    for (int i = 0; i < state.operator_count; i++)
    {
        if (state.operators[i].active &&
            strcmp(state.operators[i].username, username) == 0)
        {
            pthread_mutex_unlock(&state_mutex);
            char resp[128];
            snprintf(resp, sizeof(resp), "%s %s", RESP_ERR_ALREADY_REG, username);
            SEND_MSG(fd, resp);
            log_response(client_ip, client_port, resp);
            return;
        }
    }

    /* Buscar slot libre (operador desconectado) */
    int slot = -1;
    for (int i = 0; i < state.operator_count; i++)
    {
        if (!state.operators[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot == -1)
    {
        if (state.operator_count >= MAX_CLIENTS)
        {
            pthread_mutex_unlock(&state_mutex);
            SEND_MSG(fd, "ERROR SERVER_FULL");
            return;
        }
        slot = state.operator_count++;
    }

    strncpy(state.operators[slot].username, username,
            sizeof(state.operators[slot].username) - 1);
    state.operators[slot].socket_fd = fd;
    state.operators[slot].active = 1;

    pthread_mutex_unlock(&state_mutex);

    char resp[128];
    snprintf(resp, sizeof(resp), "OK REGISTERED %s OPERATOR", username);
    SEND_MSG(fd, resp);
    log_response(client_ip, client_port, resp);
}

/* DATA <sensor_id> <value> <unit> <timestamp> */
static void handle_data(int fd, const char *client_ip, int client_port,
                        char *args)
{
    char sensor_id[64] = {0}, unit[16] = {0};
    double value = 0.0;
    long timestamp = 0;

    if (sscanf(args, "%63s %lf %15s %ld",
               sensor_id, &value, unit, &timestamp) != 4)
    {
        SEND_MSG(fd, RESP_ERR_UNKNOWN_CMD);
        return;
    }

    pthread_mutex_lock(&state_mutex);
    Sensor *s = find_sensor(sensor_id);
    if (!s)
    {
        pthread_mutex_unlock(&state_mutex);
        char resp[128];
        snprintf(resp, sizeof(resp), "%s %s", RESP_ERR_SENSOR_NOT_FOUND, sensor_id);
        SEND_MSG(fd, resp);
        log_response(client_ip, client_port, resp);
        return;
    }

    /* Guardar medición en el historial circular */
    s->history[s->history_index].value = value;
    s->history[s->history_index].timestamp = timestamp;
    s->history_index = (s->history_index + 1) % MAX_SENSOR_HISTORY;
    if (s->history_count < MAX_SENSOR_HISTORY)
        s->history_count++;

    char type_copy[32];
    strncpy(type_copy, s->type, sizeof(type_copy) - 1);

    pthread_mutex_unlock(&state_mutex);

    /* Verificar anomalías (fuera del mutex para no bloquear) */
    int alert = check_anomaly(type_copy, value, sensor_id);

    const char *resp = alert ? RESP_OK_DATA_ALERT : RESP_OK_DATA_RECEIVED;
    SEND_MSG(fd, resp);
    log_response(client_ip, client_port, resp);
}

/* GET SENSORS */
static void handle_get_sensors(int fd, const char *client_ip, int client_port)
{
    char resp[BUFFER_SIZE] = {0};
    int offset = 0;

    pthread_mutex_lock(&state_mutex);

    offset += snprintf(resp + offset, sizeof(resp) - offset,
                       "SENSORS %d", state.sensor_count);

    for (int i = 0; i < state.sensor_count; i++)
    {
        offset += snprintf(resp + offset, sizeof(resp) - offset,
                           " %s:%s:%s",
                           state.sensors[i].id,
                           state.sensors[i].type,
                           state.sensors[i].online ? "online" : "offline");
    }

    pthread_mutex_unlock(&state_mutex);

    SEND_MSG(fd, resp);
    log_response(client_ip, client_port, resp);
}

/* GET DATA <sensor_id> <n> */
static void handle_get_data(int fd, const char *client_ip, int client_port,
                            char *args)
{
    char sensor_id[64] = {0};
    int n = 0;

    if (sscanf(args, "%63s %d", sensor_id, &n) != 2 || n <= 0)
    {
        SEND_MSG(fd, RESP_ERR_UNKNOWN_CMD);
        return;
    }
    if (n > MAX_SENSOR_HISTORY)
        n = MAX_SENSOR_HISTORY;

    pthread_mutex_lock(&state_mutex);
    Sensor *s = find_sensor(sensor_id);
    if (!s)
    {
        pthread_mutex_unlock(&state_mutex);
        char resp[128];
        snprintf(resp, sizeof(resp), "%s %s", RESP_ERR_SENSOR_NOT_FOUND, sensor_id);
        SEND_MSG(fd, resp);
        log_response(client_ip, client_port, resp);
        return;
    }

    /* Las mediciones están en el buffer circular: reconstruimos las últimas n */
    int available = s->history_count < n ? s->history_count : n;
    char resp[BUFFER_SIZE] = {0};
    int offset = 0;

    offset += snprintf(resp + offset, sizeof(resp) - offset,
                       "DATA %s %d", sensor_id, available);

    /* Recorremos el historial de más antiguo a más reciente */
    for (int i = available - 1; i >= 0; i--)
    {
        int idx = (s->history_index - 1 - i + MAX_SENSOR_HISTORY) % MAX_SENSOR_HISTORY;
        offset += snprintf(resp + offset, sizeof(resp) - offset,
                           " %.2f:%ld",
                           s->history[idx].value,
                           s->history[idx].timestamp);
    }

    pthread_mutex_unlock(&state_mutex);

    SEND_MSG(fd, resp);
    log_response(client_ip, client_port, resp);
}

/* GET STATUS */
static void handle_get_status(int fd, const char *client_ip, int client_port)
{
    pthread_mutex_lock(&state_mutex);
    long uptime = (long)(time(NULL) - state.start_time);
    int sensors = state.sensor_count;
    int operators = 0;
    for (int i = 0; i < state.operator_count; i++)
        if (state.operators[i].active)
            operators++;
    int alerts = state.total_alerts;
    pthread_mutex_unlock(&state_mutex);

    char resp[256];
    snprintf(resp, sizeof(resp),
             "STATUS sensors:%d operators:%d alerts:%d uptime:%ld",
             sensors, operators, alerts, uptime);
    SEND_MSG(fd, resp);
    log_response(client_ip, client_port, resp);
}

/* DISCONNECT <id> */
static void handle_disconnect(int fd, const char *client_ip, int client_port,
                              char *args)
{
    char id[64] = {0};
    sscanf(args, "%63s", id);

    pthread_mutex_lock(&state_mutex);

    /* Marcar sensor offline si aplica */
    Sensor *s = find_sensor(id);
    if (s)
        s->online = 0;

    /* Marcar operador inactivo si aplica */
    for (int i = 0; i < state.operator_count; i++)
    {
        if (state.operators[i].active &&
            strcmp(state.operators[i].username, id) == 0)
        {
            state.operators[i].active = 0;
            break;
        }
    }

    pthread_mutex_unlock(&state_mutex);

    SEND_MSG(fd, RESP_OK_BYE);
    log_response(client_ip, client_port, RESP_OK_BYE);
}

/* ─────────────────────────────────────────────
   HILO POR CLIENTE
───────────────────────────────────────────── */
static void *client_thread(void *arg)
{
    ClientInfo info = *(ClientInfo *)arg;
    free(arg);

    int fd = info.socket_fd;
    char *client_ip = info.client_ip;
    int client_port = info.client_port;

    log_connection(client_ip, client_port, 1);

    char buf[BUFFER_SIZE];
    char partial[BUFFER_SIZE * 2] = {0}; /* buffer para mensajes parciales */
    int partial_len = 0;
    int registered = 0; /* 0=no, CLIENT_SENSOR, CLIENT_OPERATOR */

    while (1)
    {
        int n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0)
            break; /* cliente cerró o error de red */
        buf[n] = '\0';

        /* Acumular en buffer parcial */
        if (partial_len + n < (int)sizeof(partial) - 1)
        {
            memcpy(partial + partial_len, buf, n);
            partial_len += n;
            partial[partial_len] = '\0';
        }

        /* Procesar todos los mensajes completos (terminados en \n) */
        char *newline;
        while ((newline = strchr(partial, '\n')) != NULL)
        {
            *newline = '\0';
            strip_newline(partial);

            if (strlen(partial) == 0)
            {
                /* Mensaje vacío, ignorar */
                memmove(partial, newline + 1,
                        strlen(newline + 1) + 1);
                partial_len = strlen(partial);
                continue;
            }

            log_request(client_ip, client_port, partial);

            /* Parsear comando principal */
            char cmd[32] = {0};
            char rest[BUFFER_SIZE] = {0};
            sscanf(partial, "%31s %[^\n]", cmd, rest);

            /* ── REGISTER ── */
            if (strcmp(cmd, CMD_REGISTER) == 0)
            {
                char sub[32] = {0}, args[BUFFER_SIZE] = {0};
                sscanf(rest, "%31s %[^\n]", sub, args);

                if (strcmp(sub, REG_SENSOR) == 0)
                {
                    handle_register_sensor(fd, client_ip, client_port, args);
                    registered = CLIENT_SENSOR;
                }
                else if (strcmp(sub, REG_OPERATOR) == 0)
                {
                    handle_register_operator(fd, client_ip, client_port, args);
                    registered = CLIENT_OPERATOR;
                }
                else
                {
                    SEND_MSG(fd, RESP_ERR_UNKNOWN_CMD);
                }

                /* ── DATA ── */
            }
            else if (strcmp(cmd, CMD_DATA) == 0)
            {
                if (!registered)
                {
                    SEND_MSG(fd, RESP_ERR_NOT_REGISTERED);
                }
                else
                {
                    handle_data(fd, client_ip, client_port, rest);
                }

                /* ── GET ── */
            }
            else if (strcmp(cmd, CMD_GET) == 0)
            {
                if (!registered)
                {
                    SEND_MSG(fd, RESP_ERR_NOT_REGISTERED);
                }
                else
                {
                    char sub[32] = {0}, args[BUFFER_SIZE] = {0};
                    sscanf(rest, "%31s %[^\n]", sub, args);

                    if (strcmp(sub, GET_SENSORS) == 0)
                    {
                        handle_get_sensors(fd, client_ip, client_port);
                    }
                    else if (strcmp(sub, GET_DATA) == 0)
                    {
                        handle_get_data(fd, client_ip, client_port, args);
                    }
                    else if (strcmp(sub, GET_STATUS) == 0)
                    {
                        handle_get_status(fd, client_ip, client_port);
                    }
                    else
                    {
                        SEND_MSG(fd, RESP_ERR_UNKNOWN_CMD);
                    }
                }

                /* ── DISCONNECT ── */
            }
            else if (strcmp(cmd, CMD_DISCONNECT) == 0)
            {
                handle_disconnect(fd, client_ip, client_port, rest);
                /* Mover resto del buffer y salir del loop de mensajes */
                memmove(partial, newline + 1,
                        strlen(newline + 1) + 1);
                partial_len = strlen(partial);
                goto done; /* salir del while(1) externo también */
            }
            else
            {
                SEND_MSG(fd, RESP_ERR_UNKNOWN_CMD);
                log_response(client_ip, client_port, RESP_ERR_UNKNOWN_CMD);
            }

            /* Avanzar el buffer parcial */
            memmove(partial, newline + 1, strlen(newline + 1) + 1);
            partial_len = strlen(partial);
        }
    }

done:
    /* Limpiar estado del cliente al desconectarse */
    pthread_mutex_lock(&state_mutex);

    /* Si era sensor, marcarlo offline */
    for (int i = 0; i < state.sensor_count; i++)
    {
        /* Comparamos por fd: el sensor guarda el fd cuando se registra.
           Aquí lo buscamos por fd directamente. */
        if (state.sensors[i].online)
        {
            /* No tenemos fd en Sensor, así que marcamos offline
               solo si fue un CLIENT_SENSOR */
            if (registered == CLIENT_SENSOR)
                state.sensors[i].online = 0;
        }
    }

    /* Si era operador, marcarlo inactivo */
    if (registered == CLIENT_OPERATOR)
    {
        for (int i = 0; i < state.operator_count; i++)
        {
            if (state.operators[i].active &&
                state.operators[i].socket_fd == fd)
            {
                state.operators[i].active = 0;
                break;
            }
        }
    }

    pthread_mutex_unlock(&state_mutex);

    log_connection(client_ip, client_port, 0);
    close(fd);
    return NULL;
}

/* ─────────────────────────────────────────────
   MAIN
───────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Uso: %s <puerto> <archivo_logs>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    char *log_path = argv[2];

    if (port <= 0 || port > 65535)
    {
        fprintf(stderr, "Puerto inválido: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    /* Inicializar logger */
    logger_init(log_path);

    /* Inicializar estado global */
    memset(&state, 0, sizeof(state));
    state.start_time = time(NULL);

    /* Crear socket TCP */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    /* Reutilizar puerto si el servidor se reinicia */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    /* Listen */
    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("[SERVER] Escuchando en puerto %d\n", port);
    printf("[SERVER] Logs en: %s\n", log_path);

    /* Lanzar servidor HTTP en hilo separado */
    if (http_server_start(&state) != 0)
    {
        fprintf(stderr, "[SERVER] Advertencia: servidor HTTP no pudo iniciarse.\n");
    }

    /* Bucle principal de aceptación */
    while (1)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &client_len);
        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue; /* señal interrumpió accept */
            log_error("accept", strerror(errno));
            continue;
        }

        /* Crear info del cliente para el hilo */
        ClientInfo *info = malloc(sizeof(ClientInfo));
        if (!info)
        {
            log_error("malloc", "No se pudo alocar ClientInfo");
            close(client_fd);
            continue;
        }
        info->socket_fd = client_fd;
        info->client_port = ntohs(client_addr.sin_port);
        inet_ntop(AF_INET, &client_addr.sin_addr,
                  info->client_ip, sizeof(info->client_ip));

        /* Lanzar hilo */
        pthread_t tid;
        if (pthread_create(&tid, NULL, client_thread, info) != 0)
        {
            log_error("pthread_create", strerror(errno));
            free(info);
            close(client_fd);
            continue;
        }
        pthread_detach(tid); /* el hilo libera sus recursos al terminar */
    }

    logger_close();
    close(server_fd);
    return EXIT_SUCCESS;
}