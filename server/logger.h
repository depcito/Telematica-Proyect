#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

/* Inicializa el logger con el archivo de destino.
   Llama esto UNA vez al arrancar el servidor. */
void logger_init(const char *log_filepath);

/* Cierra el archivo de logs. */
void logger_close(void);

/* Registra un evento de petición entrante. */
void log_request(const char *client_ip, int client_port, const char *message);

/* Registra una respuesta enviada al cliente. */
void log_response(const char *client_ip, int client_port, const char *response);

/* Registra un error interno del servidor. */
void log_error(const char *context, const char *detail);

/* Registra una alerta generada por el sistema. */
void log_alert(const char *level, const char *sensor_id, const char *message);

/* Registra una conexión o desconexión de cliente. */
void log_connection(const char *client_ip, int client_port, int connected);

#endif /* LOGGER_H */
