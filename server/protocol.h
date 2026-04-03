#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <netinet/in.h> /* INET_ADDRSTRLEN */

/* ─────────────────────────────────────────────
   CONFIGURACIÓN DE RED
───────────────────────────────────────────── */
#define MAX_CLIENTS 64
#define BUFFER_SIZE 1024
#define MAX_SENSOR_HISTORY 20  /* mediciones almacenadas por sensor */
#define SENSOR_SEND_INTERVAL 5 /* segundos entre mediciones */

/* ─────────────────────────────────────────────
   COMANDOS (cliente → servidor)
───────────────────────────────────────────── */
#define CMD_REGISTER "REGISTER"
#define CMD_DATA "DATA"
#define CMD_GET "GET"
#define CMD_DISCONNECT "DISCONNECT"

/* Sub-comandos de REGISTER */
#define REG_SENSOR "SENSOR"
#define REG_OPERATOR "OPERATOR"

/* Sub-comandos de GET */
#define GET_SENSORS "SENSORS"
#define GET_DATA "DATA"
#define GET_STATUS "STATUS"

/* ─────────────────────────────────────────────
   TIPOS DE SENSOR
───────────────────────────────────────────── */
#define TYPE_TEMPERATURE "TEMPERATURE"
#define TYPE_VIBRATION "VIBRATION"
#define TYPE_ENERGY "ENERGY"
#define TYPE_HUMIDITY "HUMIDITY"
#define TYPE_STATUS "STATUS"

/* ─────────────────────────────────────────────
   UNIDADES
───────────────────────────────────────────── */
#define UNIT_CELSIUS "CELSIUS"
#define UNIT_MM_S "MM_S"
#define UNIT_WATTS "WATTS"
#define UNIT_PERCENT "PERCENT"
#define UNIT_BINARY "BINARY"

/* ─────────────────────────────────────────────
   RESPUESTAS DEL SERVIDOR → CLIENTE
───────────────────────────────────────────── */
#define RESP_OK_REGISTERED "OK REGISTERED"
#define RESP_OK_REGISTERED_OP "OK REGISTERED %s OPERATOR"
#define RESP_OK_DATA_RECEIVED "OK DATA_RECEIVED"
#define RESP_OK_DATA_ALERT "OK DATA_RECEIVED ALERT_TRIGGERED"
#define RESP_OK_BYE "OK BYE"

#define RESP_ERR_ALREADY_REG "ERROR ALREADY_REGISTERED"
#define RESP_ERR_INVALID_TYPE "ERROR INVALID_TYPE"
#define RESP_ERR_INVALID_TOKEN "ERROR INVALID_TOKEN"
#define RESP_ERR_AUTH_FAILED "ERROR AUTH_FAILED"
#define RESP_ERR_SENSOR_NOT_FOUND "ERROR SENSOR_NOT_FOUND"
#define RESP_ERR_INVALID_VALUE "ERROR INVALID_VALUE"
#define RESP_ERR_UNKNOWN_CMD "ERROR UNKNOWN_COMMAND"
#define RESP_ERR_NOT_REGISTERED "ERROR NOT_REGISTERED"

/* ─────────────────────────────────────────────
   ALERTAS (servidor → operadores, push)
───────────────────────────────────────────── */
#define ALERT_CMD "ALERT"
#define ALERT_INFO "INFO"
#define ALERT_WARNING "WARNING"
#define ALERT_CRITICAL "CRITICAL"

/* ─────────────────────────────────────────────
   UMBRALES DE ANOMALÍA
───────────────────────────────────────────── */

/* TEMPERATURE (CELSIUS) */
#define TEMP_WARNING 75.0
#define TEMP_CRITICAL 90.0

/* VIBRATION (MM_S) */
#define VIB_WARNING 3.5
#define VIB_CRITICAL 6.0

/* ENERGY (WATTS) */
#define ENERGY_WARNING 700.0
#define ENERGY_CRITICAL 900.0

/* HUMIDITY (PERCENT) */
#define HUM_WARNING 80.0
#define HUM_CRITICAL 90.0

/* STATUS (BINARY): CRITICAL si value == 0 */
#define STATUS_CRITICAL 0.0

/* ─────────────────────────────────────────────
   TIPOS DE CLIENTE (para identificar en el server)
───────────────────────────────────────────── */
#define CLIENT_UNKNOWN 0
#define CLIENT_SENSOR 1
#define CLIENT_OPERATOR 2

/* ─────────────────────────────────────────────
   STRUCTS
───────────────────────────────────────────── */

/* Una medición almacenada */
typedef struct
{
   double value;
   long timestamp;
} Measurement;

/* Un sensor registrado */
typedef struct
{
   char id[64];
   char type[32];
   char unit[16];
   int online;           /* 1 = online, 0 = offline */
   int socket_fd_sensor; /* fd del socket, para identificarlo al desconectar */
   Measurement history[MAX_SENSOR_HISTORY];
   int history_count;
   int history_index; /* índice circular */
} Sensor;

/* Un operador registrado */
typedef struct
{
   char username[64];
   int socket_fd;
   int active;
} Operator;

/* Info pasada al hilo de cada cliente */
typedef struct
{
   int socket_fd;
   char client_ip[INET_ADDRSTRLEN];
   int client_port;
} ClientInfo;

/* Estado global del servidor (compartido entre hilos) */
typedef struct
{
   Sensor sensors[MAX_CLIENTS];
   int sensor_count;

   Operator operators[MAX_CLIENTS];
   int operator_count;

   int total_alerts;
   long start_time;
} ServerState;

/* ─────────────────────────────────────────────
   MACROS DE UTILIDAD
───────────────────────────────────────────── */

/* Envía un mensaje terminado en \n al socket */
#define SEND_MSG(fd, msg)                        \
   do                                            \
   {                                             \
      char _buf[BUFFER_SIZE];                    \
      snprintf(_buf, sizeof(_buf), "%s\n", msg); \
      send(fd, _buf, strlen(_buf), 0);           \
   } while (0)

/* Envía un mensaje formateado terminado en \n */
#define SEND_MSGF(fd, fmt, ...)                            \
   do                                                      \
   {                                                       \
      char _buf[BUFFER_SIZE];                              \
      snprintf(_buf, sizeof(_buf), fmt "\n", __VA_ARGS__); \
      send(fd, _buf, strlen(_buf), 0);                     \
   } while (0)

#endif /* PROTOCOL_H */