#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

static FILE          *log_file   = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Utilidad interna: timestamp legible ── */
static void get_timestamp(char *buf, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", t);
}

/* ── Escribe en consola y en archivo ── */
static void write_log(const char *line) {
    pthread_mutex_lock(&log_mutex);

    /* Consola siempre */
    printf("%s\n", line);
    fflush(stdout);

    /* Archivo si está abierto */
    if (log_file) {
        fprintf(log_file, "%s\n", line);
        fflush(log_file);
    }

    pthread_mutex_unlock(&log_mutex);
}

/* ─────────────────────────────────────────────
   API PÚBLICA
───────────────────────────────────────────── */

void logger_init(const char *log_filepath) {
    log_file = fopen(log_filepath, "a");
    if (!log_file) {
        fprintf(stderr, "[LOGGER] No se pudo abrir el archivo de logs: %s\n", log_filepath);
        fprintf(stderr, "[LOGGER] Los logs solo se mostrarán en consola.\n");
    }
    char ts[32], line[512];
    get_timestamp(ts, sizeof(ts));
    snprintf(line, sizeof(line), "[%s] [INFO] ===== Servidor iniciado =====", ts);
    write_log(line);
}

void logger_close(void) {
    char ts[32], line[256];
    get_timestamp(ts, sizeof(ts));
    snprintf(line, sizeof(line), "[%s] [INFO] ===== Servidor detenido =====", ts);
    write_log(line);
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
}

void log_request(const char *client_ip, int client_port, const char *message) {
    char ts[32], line[BUFSIZ];
    get_timestamp(ts, sizeof(ts));
    snprintf(line, sizeof(line),
             "[%s] [REQUEST]  %s:%d  →  %s",
             ts, client_ip, client_port, message);
    write_log(line);
}

void log_response(const char *client_ip, int client_port, const char *response) {
    char ts[32], line[BUFSIZ];
    get_timestamp(ts, sizeof(ts));
    snprintf(line, sizeof(line),
             "[%s] [RESPONSE] %s:%d  ←  %s",
             ts, client_ip, client_port, response);
    write_log(line);
}

void log_error(const char *context, const char *detail) {
    char ts[32], line[512];
    get_timestamp(ts, sizeof(ts));
    snprintf(line, sizeof(line),
             "[%s] [ERROR]    [%s] %s",
             ts, context, detail);
    write_log(line);
}

void log_alert(const char *level, const char *sensor_id, const char *message) {
    char ts[32], line[512];
    get_timestamp(ts, sizeof(ts));
    snprintf(line, sizeof(line),
             "[%s] [ALERT]    %-8s  sensor=%s  msg=%s",
             ts, level, sensor_id, message);
    write_log(line);
}

void log_connection(const char *client_ip, int client_port, int connected) {
    char ts[32], line[256];
    get_timestamp(ts, sizeof(ts));
    snprintf(line, sizeof(line),
             "[%s] [%s] %s:%d",
             ts,
             connected ? "CONNECT   " : "DISCONNECT",
             client_ip, client_port);
    write_log(line);
}
