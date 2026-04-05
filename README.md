# 🌐 Sistema Distribuido de Monitoreo de Sensores IoT

![Docker](https://img.shields.io/badge/Docker-Ready-blue)
![Python|Flask](https://img.shields.io/badge/Python|Flask-Backend-green)
![C](https://img.shields.io/badge/C-Backend-orange)
![Java](https://img.shields.io/badge/Java-Swing-red)

Este proyecto es una plataforma distribuida para el monitoreo en tiempo real de infraestructura IoT. Permite la recolección continua de datos desde múltiples sensores simulados, la detección de anomalías mediante un servidor centralizado, y la visualización de alertas a través de clientes de escritorio y un dashboard web interactivo.

Desarrollado para el curso de **Telemática: Internet, Arquitectura y Protocolos**.

---

## 🏗️ Arquitectura del Sistema

El sistema está compuesto por 4 módulos principales:

1. **Servidor Central (C):** Servidor multihilo implementado con Sockets Berkeley. Maneja conexiones TCP para el protocolo IoT (puerto `8080`) y sirve la interfaz web por HTTP (puerto `8081`).
2. **Servicio de Autenticación (Python/Flask):** API RESTful (puerto `5001`) encargada de validar credenciales contra un registro JSON y emitir tokens de acceso.
3. **Sensores IoT (Python):** Script concurrente que simula 5 tipos de dispositivos (Temperatura, Vibración, Energía, Humedad y Estado). Se conectan vía TCP para enviar mediciones cada 5 segundos.
4. **Cliente Operador (Java Swing):** Aplicación de escritorio multiplataforma. Mantiene una conexión TCP constante para recibir alertas *push* en tiempo real con códigos de colores.

---

## 📂 Estructura del Proyecto

    ├── auth/                   # Servicio de autenticación (users.json, auth_server.py)
    ├── clients/
    │   ├── operator/           # Cliente de escritorio Java (GUI, Sockets TCP, Auth HTTP)
    │   └── sensors/            # Scripts de Python simulando 5 sensores IoT concurrentes
    ├── docker/                 # Archivos de orquestación (Dockerfile, docker-compose.yml)
    ├── docs/                   # Documentación oficial (Arquitectura, Deployment, Protocolo)
    ├── server/                 # Código fuente del Servidor Central C (TCP + HTTP)
    └── web/                    # Vistas HTML físicas del frontend (index.html, status.html)

---

## 💻 Instalación y Pruebas (Entorno Local)

Todo el backend está dockerizado para garantizar su reproducibilidad. 

### 1. Levantar Servidores y Sensores (Docker)
Abre una terminal en la raíz del proyecto y orquesta los contenedores:

    cd docker
    docker compose up --build -d

*(Para ver los logs en vivo usa: `docker compose logs -f server`)*

### 2. Acceder al Dashboard Web
Abre tu navegador web e ingresa a: 👉 **`http://localhost:8081`**

### 3. Ejecutar el Cliente Operador (Java)
Abre una nueva terminal para lanzar la interfaz de escritorio nativa:

    cd clients/operator
    javac *.java
    SERVER_HOST=localhost AUTH_URL=http://localhost:5001 java Main

* **Nota:** <br>Credenciales de prueba: <br>Usuario: `admin` | Contraseña: `admin123`

---

## 🔌 Protocolo de Aplicación (Capa 7)

El sistema utiliza un protocolo TCP basado en texto diseñado específicamente para este proyecto. 
* **Reglas base:** Los campos se separan con un espacio `' '` y todo mensaje termina obligatoriamente con un salto de línea `\n`.

### Comandos de los Sensores (Cliente → Servidor)
| Comando | Formato | Descripción |
| :--- | :--- | :--- |
| **Registro** | `REGISTER SENSOR <id> <tipo>\n` | Registra el sensor en el sistema. Tipos válidos: `TEMPERATURE`, `VIBRATION`, `ENERGY`, `HUMIDITY`, `STATUS`. |
| **Medición** | `DATA <id> <valor> <unidad> <timestamp>\n` | Envía una medición periódica. |

### Comandos del Operador (Cliente → Servidor)
| Comando | Formato | Descripción |
| :--- | :--- | :--- |
| **Registro** | `REGISTER OPERATOR <usuario> <token>\n` | Autentica al operador usando el token de Flask. |
| **Listar** | `GET SENSORS\n` | Retorna los sensores activos y su estado. |
| **Historial** | `GET DATA <id> <n>\n` | Retorna las últimas *n* mediciones de un sensor. |
| **Estado** | `GET STATUS\n` | Retorna el uptime y conteo de alertas/operadores. |

### Alertas Push (Servidor → Operador)
Las alertas son enviadas asíncronamente por el servidor cuando una variable excede sus umbrales.
* **Formato:** `ALERT <nivel> <id_sensor> <mensaje>\n`
* **Niveles:** `INFO` (Normalización), `WARNING` (Peligro leve), `CRITICAL` (Falla inminente).

Para conocer los umbrales de advertencia/críticos y códigos de error, consulta [docs/PROTOCOL.md](docs/PROTOCOL.md).

---

*Desarrollado en equipo para la materia de Telemática.*
