# Protocolo de Aplicación — Sistema Distribuido de Monitoreo IoT

**Versión:** 1.0  
**Fecha:** 30 de marzo de 2025  
**Proyecto:** IoT Monitoring System — Telemática

\---

## Tabla de contenidos

1. [Descripción general](#1-descripción-general)
2. [Parámetros base del protocolo](#2-parámetros-base-del-protocolo)
3. [Tipos de clientes](#3-tipos-de-clientes)
4. [Formato general de los mensajes](#4-formato-general-de-los-mensajes)
5. [Mensajes de registro](#5-mensajes-de-registro)
6. [Mensajes de datos](#6-mensajes-de-datos)
7. [Mensajes de consulta](#7-mensajes-de-consulta)
8. [Mensajes de alerta (push del servidor)](#8-mensajes-de-alerta-push-del-servidor)
9. [Mensajes de desconexión](#9-mensajes-de-desconexión)
10. [Códigos de respuesta del servidor](#10-códigos-de-respuesta-del-servidor)
11. [Umbrales de detección de anomalías](#11-umbrales-de-detección-de-anomalías)
12. [Flujos de sesión completos](#12-flujos-de-sesión-completos)
13. [Reglas de implementación](#13-reglas-de-implementación)
14. [Resumen de comandos](#14-resumen-de-comandos)

\---

## 1\. Descripción general

Este documento especifica el protocolo de la capa de aplicación diseñado para la comunicación entre los tres componentes del sistema:

* **Sensores IoT simulados** → envían mediciones periódicas al servidor
* **Operadores del sistema** → consultan datos y reciben alertas en tiempo real
* **Servidor central de monitoreo** → recibe, procesa y distribuye la información

El protocolo es **basado en texto**, lo que facilita la implementación en múltiples lenguajes de programación (C, Python, Java) y simplifica la depuración durante el desarrollo.

\---

## 2\. Parámetros base del protocolo

|Parámetro|Valor|Justificación|
|-|-|-|
|**Transporte**|TCP (SOCK\_STREAM)|Garantiza entrega ordenada y sin pérdidas. Crítico para datos de sensores y alertas.|
|**Separador de campos**|Espacio `' '` (0x20)|Simple de parsear con `split()` en cualquier lenguaje.|
|**Terminador de mensaje**|Newline `'\\n'` (0x0A)|El servidor lee del socket hasta `\\n` para delimitar un mensaje completo.|
|**Encoding**|UTF-8|Estándar universal, compatible con todos los lenguajes del proyecto.|
|**Puerto del servidor**|Configurable por argumento|`./server <puerto> <archivo\_logs>`|
|**Intervalo de envío de sensores**|5 segundos (recomendado)|Configurable en `config.py` / `config.java`|

> \*\*Importante sobre TCP y mensajes:\*\* TCP es un protocolo de flujo, no de mensajes. Dos mensajes enviados consecutivamente pueden llegar concatenados al receptor. Por esta razón, el terminador `\\n` es obligatorio al final de \*\*cada mensaje\*\*, y el receptor debe leer hasta encontrar `\\n` antes de procesar.

\---

## 3\. Tipos de clientes

El servidor maneja dos tipos de clientes con comportamientos distintos:

### Sensor

* Se conecta y permanece conectado indefinidamente.
* Envía mediciones periódicas cada N segundos.
* El servidor responde a cada mensaje del sensor.
* No recibe alertas push (solo recibe confirmaciones de sus propios mensajes).

### Operador

* Se conecta después de autenticarse.
* Puede hacer consultas al servidor en cualquier momento.
* **Recibe alertas push del servidor** sin haberlas solicitado (cuando ocurre una anomalía).
* Debe mantener un hilo de escucha activo en todo momento para no perder alertas.

\---

## 4\. Formato general de los mensajes

Todos los mensajes siguen esta estructura:

```
COMANDO \[campo1] \[campo2] \[campo3] \[...]\\n
```

* Los campos están separados por un único espacio.
* El mensaje termina obligatoriamente con `\\n`.
* Los comandos están en MAYÚSCULAS.
* Los identificadores de sensores y operadores son sensibles a mayúsculas/minúsculas.
* Los campos entre `\[ ]` son opcionales dependiendo del comando.
* Los campos entre `< >` son obligatorios.

**Ejemplo de mensaje válido:**

```
DATA temp\_01 87.3 CELSIUS 1717000001\\n
```

**Ejemplo de mensaje inválido (sin terminador):**

```
DATA temp\_01 87.3 CELSIUS 1717000001
```

\---

## 5\. Mensajes de registro

El registro es **el primer mensaje obligatorio** que debe enviar cualquier cliente al conectarse. El servidor no procesará ningún otro mensaje de un cliente que no se haya registrado primero.

\---

### 5.1 `REGISTER SENSOR` — Registro de sensor

**Dirección:** Sensor → Servidor

**Formato:**

```
REGISTER SENSOR <sensor\_id> <type>\\n
```

**Campos:**

|Campo|Tipo|Descripción|
|-|-|-|
|`sensor\_id`|string|Identificador único del sensor. Sin espacios. Ej: `temp\_01`|
|`type`|enum|Tipo de sensor. Ver valores válidos abajo.|

**Tipos de sensor válidos:**

|Valor|Descripción|
|-|-|
|`TEMPERATURE`|Sensor de temperatura|
|`VIBRATION`|Sensor de vibración mecánica|
|`ENERGY`|Sensor de consumo energético|
|`HUMIDITY`|Sensor de humedad|
|`STATUS`|Sensor de estado operativo de equipo|

**Ejemplos:**

```
REGISTER SENSOR temp\_01 TEMPERATURE\\n
REGISTER SENSOR vib\_01 VIBRATION\\n
REGISTER SENSOR eng\_01 ENERGY\\n
REGISTER SENSOR hum\_01 HUMIDITY\\n
REGISTER SENSOR sta\_01 STATUS\\n
```

**Respuestas del servidor:**

|Situación|Respuesta|
|-|-|
|Registro exitoso|`OK REGISTERED <sensor\_id>\\n`|
|El sensor ya está registrado|`ERROR ALREADY\_REGISTERED <sensor\_id>\\n`|
|Tipo de sensor inválido|`ERROR INVALID\_TYPE <type>\\n`|

\---

### 5.2 `REGISTER OPERATOR` — Registro de operador

**Dirección:** Operador → Servidor

**Formato:**

```
REGISTER OPERATOR <username> <token>\\n
```

**Campos:**

|Campo|Tipo|Descripción|
|-|-|-|
|`username`|string|Nombre de usuario. Sin espacios.|
|`token`|string|Token de autenticación obtenido del servicio de auth.|

**Flujo de autenticación:**

1. El cliente operador solicita un token al servicio de autenticación externo (HTTP POST `/login`).
2. El servicio devuelve un token si las credenciales son válidas.
3. El operador envía `REGISTER OPERATOR` con ese token al servidor principal.
4. El servidor valida el token consultando el servicio de auth (HTTP GET `/validate?token=xxx`).
5. Si el token es válido, el operador queda registrado.

**Ejemplo:**

```
REGISTER OPERATOR jperez abc123token\\n
```

**Respuestas del servidor:**

|Situación|Respuesta|
|-|-|
|Registro exitoso|`OK REGISTERED <username> OPERATOR\\n`|
|Token inválido o expirado|`ERROR INVALID\_TOKEN\\n`|
|Fallo de autenticación|`ERROR AUTH\_FAILED\\n`|
|Operador ya registrado|`ERROR ALREADY\_REGISTERED <username>\\n`|

\---

## 6\. Mensajes de datos

### 6.1 `DATA` — Envío de medición

**Dirección:** Sensor → Servidor

**Formato:**

```
DATA <sensor\_id> <value> <unit> <timestamp>\\n
```

**Campos:**

|Campo|Tipo|Descripción|
|-|-|-|
|`sensor\_id`|string|ID del sensor (debe estar previamente registrado).|
|`value`|float|Valor de la medición. Usar punto como separador decimal.|
|`unit`|enum|Unidad de medida. Ver tabla abajo.|
|`timestamp`|integer|Unix timestamp (segundos desde epoch).|

**Unidades válidas por tipo de sensor:**

|Tipo de sensor|Unidad|Descripción|
|-|-|-|
|`TEMPERATURE`|`CELSIUS`|Grados Celsius|
|`VIBRATION`|`MM\_S`|Milímetros por segundo|
|`ENERGY`|`WATTS`|Vatios|
|`HUMIDITY`|`PERCENT`|Porcentaje|
|`STATUS`|`BINARY`|1 = encendido, 0 = apagado/falla|

**Ejemplos:**

```
DATA temp\_01 87.3 CELSIUS 1717000001\\n
DATA vib\_01 4.8 MM\_S 1717000002\\n
DATA eng\_01 320.5 WATTS 1717000003\\n
DATA hum\_01 91.2 PERCENT 1717000004\\n
DATA sta\_01 0 BINARY 1717000005\\n
```

**Respuestas del servidor:**

|Situación|Respuesta|
|-|-|
|Dato recibido, sin anomalía|`OK DATA\_RECEIVED\\n`|
|Dato recibido, anomalía detectada|`OK DATA\_RECEIVED ALERT\_TRIGGERED\\n`|
|Sensor no registrado|`ERROR SENSOR\_NOT\_FOUND <sensor\_id>\\n`|
|Formato de valor inválido|`ERROR INVALID\_VALUE\\n`|

> Cuando el servidor responde `ALERT\_TRIGGERED`, significa que ya envió un mensaje `ALERT` a todos los operadores conectados. El sensor solo recibe la confirmación.

\---

## 7\. Mensajes de consulta

Estos mensajes los envía el **operador** para solicitar información al servidor. El servidor responde de forma síncrona (el operador espera la respuesta antes de enviar otra consulta).

\---

### 7.1 `GET SENSORS` — Lista de sensores activos

**Dirección:** Operador → Servidor

**Formato:**

```
GET SENSORS\\n
```

**Respuesta del servidor:**

```
SENSORS <count> <id:type:status> \[<id:type:status> ...]\\n
```

**Campos de la respuesta:**

|Campo|Descripción|
|-|-|
|`count`|Número total de sensores registrados|
|`id:type:status`|Cada sensor separado por espacio. Estado: `online` u `offline`|

**Ejemplo:**

```
→ GET SENSORS\\n
← SENSORS 3 temp\_01:TEMPERATURE:online vib\_01:VIBRATION:online eng\_01:ENERGY:offline\\n
```

\---

### 7.2 `GET DATA` — Últimas mediciones de un sensor

**Dirección:** Operador → Servidor

**Formato:**

```
GET DATA <sensor\_id> <n>\\n
```

**Campos:**

|Campo|Tipo|Descripción|
|-|-|-|
|`sensor\_id`|string|ID del sensor a consultar.|
|`n`|integer|Número de mediciones recientes a devolver (máximo 20).|

**Respuesta del servidor:**

```
DATA <sensor\_id> <count> <value:timestamp> \[<value:timestamp> ...]\\n
```

**Ejemplo:**

```
→ GET DATA temp\_01 3\\n
← DATA temp\_01 3 72.1:1717000010 85.3:1717000015 91.5:1717000020\\n
```

**Respuesta en caso de error:**

```
ERROR SENSOR\_NOT\_FOUND temp\_01\\n
```

\---

### 7.3 `GET STATUS` — Estado general del sistema

**Dirección:** Cualquier cliente → Servidor

**Formato:**

```
GET STATUS\\n
```

**Respuesta del servidor:**

```
STATUS sensors:<n> operators:<n> alerts:<n> uptime:<segundos>\\n
```

**Ejemplo:**

```
→ GET STATUS\\n
← STATUS sensors:5 operators:2 alerts:7 uptime:3600\\n
```

\---

## 8\. Mensajes de alerta (push del servidor)

Las alertas son mensajes que el servidor envía **espontáneamente** a todos los operadores conectados cuando detecta una anomalía en los datos de un sensor. El operador **no solicita** estas alertas — las recibe en cualquier momento.

**Dirección:** Servidor → Todos los operadores conectados

**Formato:**

```
ALERT <level> <sensor\_id> <message>\\n
```

**Campos:**

|Campo|Tipo|Descripción|
|-|-|-|
|`level`|enum|Nivel de severidad: `INFO`, `WARNING` o `CRITICAL`|
|`sensor\_id`|string|ID del sensor que generó la alerta|
|`message`|string|Descripción del evento. Sin espacios (usar `\_` como separador).|

**Niveles de alerta:**

|Nivel|Descripción|Color sugerido en GUI|
|-|-|-|
|`INFO`|Condición normalizada o informativa|Azul|
|`WARNING`|Valor fuera del rango normal|Amarillo/naranja|
|`CRITICAL`|Valor en rango peligroso o equipo detenido|Rojo|

**Ejemplos:**

```
← ALERT WARNING temp\_01 Temperature\_above\_threshold:87.3\_CELSIUS\\n
← ALERT CRITICAL eng\_01 Energy\_consumption\_critical:950\_WATTS\\n
← ALERT CRITICAL sta\_01 Equipment\_stopped:0\_BINARY\\n
← ALERT INFO vib\_01 Vibration\_normalized:1.2\_MM\_S\\n
```

> \*\*Nota para el operador:\*\* El cliente operador debe tener un hilo dedicado a escuchar el socket en todo momento. Si el operador está esperando la respuesta a un `GET`, el servidor podría enviarle una `ALERT` al mismo tiempo. El cliente debe manejar ambos casos correctamente.

> \*\*El operador no envía ninguna respuesta al recibir un `ALERT`.\*\* Es un mensaje unidireccional.

\---

## 9\. Mensajes de desconexión

### 9.1 `DISCONNECT` — Cierre limpio de sesión

**Dirección:** Cualquier cliente → Servidor

**Formato:**

```
DISCONNECT <client\_id>\\n
```

**Ejemplos:**

```
DISCONNECT temp\_01\\n
DISCONNECT jperez\\n
```

**Respuesta del servidor:**

```
OK BYE\\n
```

Después de enviar `OK BYE`, el servidor cierra el socket y libera los recursos del cliente. Si el cliente era un sensor, su estado pasa a `offline`. Si era un operador, se elimina de la lista de receptores de alertas.

> \*\*Desconexión abrupta:\*\* Si un cliente se desconecta sin enviar `DISCONNECT` (caída de red, cierre forzado), el servidor debe detectarlo cuando la siguiente escritura o lectura al socket falle, y liberar los recursos igualmente. El servidor \*\*nunca\*\* debe terminar su ejecución por una desconexión inesperada de un cliente.

\---

## 10\. Códigos de respuesta del servidor

### Respuestas de éxito

|Respuesta|Significado|
|-|-|
|`OK REGISTERED <id>`|Cliente registrado exitosamente|
|`OK REGISTERED <id> OPERATOR`|Operador registrado exitosamente|
|`OK DATA\_RECEIVED`|Medición recibida y procesada sin anomalías|
|`OK DATA\_RECEIVED ALERT\_TRIGGERED`|Medición recibida y se generó una alerta|
|`OK BYE`|Desconexión aceptada|

### Respuestas de error

|Respuesta|Significado|
|-|-|
|`ERROR ALREADY\_REGISTERED <id>`|El cliente ya estaba registrado|
|`ERROR INVALID\_TYPE <type>`|Tipo de sensor no reconocido|
|`ERROR INVALID\_TOKEN`|Token de autenticación inválido|
|`ERROR AUTH\_FAILED`|No se pudo contactar el servicio de autenticación|
|`ERROR SENSOR\_NOT\_FOUND <id>`|Sensor no encontrado en el sistema|
|`ERROR INVALID\_VALUE`|El valor de la medición no es un número válido|
|`ERROR UNKNOWN\_COMMAND`|Comando no reconocido por el servidor|
|`ERROR NOT\_REGISTERED`|El cliente intentó enviar datos sin haberse registrado|

\---

## 11\. Umbrales de detección de anomalías

El servidor usa estos umbrales para decidir cuándo generar una alerta. Son valores internos del servidor — los sensores no los conocen.

|Tipo de sensor|Unidad|Rango normal|WARNING|CRITICAL|
|-|-|-|-|-|
|`TEMPERATURE`|CELSIUS|0 – 75|> 75.0|> 90.0|
|`VIBRATION`|MM\_S|0 – 3.5|> 3.5|> 6.0|
|`ENERGY`|WATTS|0 – 700|> 700.0|> 900.0|
|`HUMIDITY`|PERCENT|0 – 80|> 80.0|> 90.0|
|`STATUS`|BINARY|1|—|== 0|

**Lógica de evaluación:**

1. Cada vez que el servidor recibe un mensaje `DATA`, evalúa el valor contra los umbrales del tipo de sensor correspondiente.
2. Si el valor supera el umbral `CRITICAL`, se genera `ALERT CRITICAL`.
3. Si supera `WARNING` pero no `CRITICAL`, se genera `ALERT WARNING`.
4. Si el valor vuelve al rango normal después de una alerta, se puede generar `ALERT INFO` para notificar la normalización.

\---

## 12\. Flujos de sesión completos

### 12.1 Sesión típica de un sensor

```
\[Sensor se conecta al servidor por TCP]

sensor  →  REGISTER SENSOR temp\_01 TEMPERATURE\\n
servidor→  OK REGISTERED temp\_01\\n

\[Cada 5 segundos:]

sensor  →  DATA temp\_01 72.1 CELSIUS 1717000010\\n
servidor→  OK DATA\_RECEIVED\\n

sensor  →  DATA temp\_01 87.3 CELSIUS 1717000015\\n
servidor→  OK DATA\_RECEIVED\\n

sensor  →  DATA temp\_01 91.5 CELSIUS 1717000020\\n
servidor→  OK DATA\_RECEIVED ALERT\_TRIGGERED\\n

\[El servidor, en paralelo, envía a todos los operadores:]
servidor→  ALERT CRITICAL temp\_01 Temperature\_above\_threshold:91.5\_CELSIUS\\n

\[Al cerrar el programa:]

sensor  →  DISCONNECT temp\_01\\n
servidor→  OK BYE\\n

\[Servidor cierra el socket del sensor, marca temp\_01 como offline]
```

\---

### 12.2 Sesión típica de un operador

```
\[Operador obtiene token del servicio de auth via HTTP]
\[Operador se conecta al servidor por TCP]

operador→  REGISTER OPERATOR jperez abc123token\\n
servidor→  OK REGISTERED jperez OPERATOR\\n

operador→  GET SENSORS\\n
servidor→  SENSORS 3 temp\_01:TEMPERATURE:online vib\_01:VIBRATION:online eng\_01:ENERGY:online\\n

operador→  GET DATA temp\_01 3\\n
servidor→  DATA temp\_01 3 72.1:1717000010 85.3:1717000015 91.5:1717000020\\n

\[En cualquier momento, sin que el operador lo pida:]
servidor→  ALERT CRITICAL temp\_01 Temperature\_above\_threshold:91.5\_CELSIUS\\n
servidor→  ALERT WARNING eng\_01 Energy\_consumption\_high:720\_WATTS\\n

\[El operador continúa haciendo consultas normalmente:]
operador→  GET STATUS\\n
servidor→  STATUS sensors:5 operators:2 alerts:7 uptime:7200\\n

operador→  DISCONNECT jperez\\n
servidor→  OK BYE\\n
```

\---

### 12.3 Flujo de autenticación del operador (servicio externo)

```
\[Antes de conectarse al servidor principal]

operador  →  HTTP POST auth-server:5001/login
             Body: {"username": "jperez", "password": "1234"}

auth-srv  →  HTTP 200 OK
             Body: {"token": "abc123token", "role": "operator"}

\[Ahora el operador usa ese token para registrarse en el servidor principal]
operador  →  REGISTER OPERATOR jperez abc123token\\n

\[El servidor principal valida el token internamente:]
servidor  →  HTTP GET auth-server:5001/validate?token=abc123token
auth-srv  →  HTTP 200 OK  {"valid": true, "username": "jperez", "role": "operator"}

servidor  →  OK REGISTERED jperez OPERATOR\\n
```

\---

## 13\. Reglas de implementación

Estas reglas son obligatorias para todos los integrantes del equipo.

### Para el servidor (C)

* El servidor se ejecuta como: `./server <puerto> <archivo\_logs>`
* Debe usar `pthreads` — un hilo por cliente conectado.
* El hilo de un cliente no debe afectar a los demás si falla.
* El servidor **nunca** termina su ejecución por un error de un cliente.
* Todas las direcciones IP de servicios externos se resuelven por DNS, nunca hardcodeadas.
* El logging registra: IP del cliente, puerto de origen, mensaje recibido, respuesta enviada, timestamp.
* El servidor almacena en memoria las últimas 20 mediciones por sensor.

### Para los clientes (Python / Java)

* El `SERVER\_HOST` nunca es `localhost` hardcodeado — se lee de `config.py` o `config.java`.
* Los sensores deben intentar reconectarse si pierden la conexión con el servidor.
* El cliente operador debe tener **dos hilos**: uno para enviar consultas, otro para escuchar alertas push.
* Los mensajes siempre terminan en `\\n` al enviarse por el socket.
* Al recibir mensajes, leer hasta encontrar `\\n` antes de procesar.

### Para todos

* Ningún archivo fuente puede tener una dirección IP hardcodeada.
* Cualquier cambio al protocolo debe actualizarse en este documento **antes** de implementarse.
* Si se agrega un nuevo comando, todos los integrantes deben ser notificados.

\---

## 14\. Resumen de comandos

|Comando|Dirección|Descripción|
|-|-|-|
|`REGISTER SENSOR <id> <type>`|Sensor → Servidor|Registrar un sensor nuevo|
|`REGISTER OPERATOR <user> <token>`|Operador → Servidor|Registrar un operador autenticado|
|`DATA <id> <value> <unit> <timestamp>`|Sensor → Servidor|Enviar una medición|
|`GET SENSORS`|Operador → Servidor|Listar sensores activos|
|`GET DATA <id> <n>`|Operador → Servidor|Últimas N mediciones de un sensor|
|`GET STATUS`|Cualquiera → Servidor|Estado general del sistema|
|`ALERT <level> <id> <message>`|Servidor → Operadores|Notificación de anomalía (push)|
|`DISCONNECT <id>`|Cualquiera → Servidor|Cierre limpio de sesión|

\---