# Sistema Distribuido de Monitoreo de Sensores IoT
Este proyecto implementa un sistema distribuido de monitoreo de sensores IoT en tiempo real, diseñado para simular un entorno donde múltiples dispositivos capturan información periódicamente, la envían a un servidor central y permiten que operadores humanos supervisen el sistema mediante interfaces de consulta y recepción de alertas.

La solución no está construida como una sola aplicación monolítica, sino como un conjunto de módulos independientes que cooperan entre sí a través de red. El sistema fue desarrollado usando varios lenguajes de programación, donde cada tecnología cumple un papel específico según las necesidades del módulo correspondiente.

## Objetivo del sistema 
El objetivo principal del proyecto es construir una arquitectura distribuida capaz de:
- recibir datos de múltiples sensores IoT simulados
- procesar y almacenar temporalmente esas mediciones
- detectar anomalías con base en umbrales definidos
- generar alertas en tiempo real
- permitir a operadores autenticados consultar el estado del sistema
- exponer una visualización web del monitoreo


## Arquitectura del sistema 
### 1. Servidor Central (C)
Desarrollado en C, es el núcleo del sistema.

Se encarga de:
- aceptar conexiones TCP de sensores y operadores
- interpretar los mensajes del protocolo de aplicación
- registrar clientes conectados
- recibir y procesar mediciones
- guardar en memoria las últimas lecturas de cada sensor
- detectar anomalías
- enviar alertas push a operadores conectados
- responder consultas del operador
- servir una interfaz web HTTP

Expone dos puertos:
- 8080/TCP → protocolo de monitoreo para sensores y operadores
- 8081/TCP → servicio HTTP para dashboard web

### 2. Servicio de Autenticación (Python + Flask)
Desarrollado en Python con Flask.

Es un servicio independiente que no procesa sensores ni alertas; su única responsabilidad es gestionar la autenticación del operador.

Sus funciones son:
- recibir credenciales de usuario vía HTTP
- validarlas contra un archivo JSON
- generar o devolver un token
- validar tokens cuando el servidor central lo solicite

Puerto expuesto:
5001/TCP

### 3. Sensores IoT (Python)
Desarrollados en Python.

Simulan dispositivos reales que generan mediciones periódicas. Cada sensor representa un tipo distinto de variable monitoreada, por ejemplo:
- temperatura
- vibración
- energía
- humedad
- estado

Cada sensor:
- abre una conexión TCP con el servidor
- se registra
- envía mediciones cada 5 segundos
- espera la respuesta del servidor
- intenta reconectarse si pierde la conexión

### 4. Cliente Operador (Java Swing)
Desarrollado en Java Swing.

Es una interfaz gráfica de escritorio que permite a un usuario autenticarse, conectarse al servidor y monitorear el sistema.

El operador puede:
- iniciar sesión usando el servicio de autenticación
- registrarse ante el servidor central con un token válido
- consultar sensores activos
- consultar historial de mediciones
- consultar el estado general del sistema
- recibir alertas en tiempo real sin necesidad de pedirlas


## Comunicacion entre modulos
### Comunicación entre sensores y servidor central
La comunicación entre sensores y servidor se hace por TCP usando un protocolo de aplicación basado en texto.

Flujo general:
1. El sensor abre un socket TCP al servidor.
2. Envía un mensaje de registro
3. El servidor valida el mensaje y responde.
4. Luego el sensor comienza a enviar mediciones
5. El servidor responde confirmando si recibió el dato y si generó o no una alerta.

Características clave:
- la conexión es persistente
- el sensor envía datos periódicos
- el servidor responde a cada mensaje del sensor
- si el sensor pierde la conexión, intenta reconectarse

### Comunicación entre operador y servicio de autenticación
Antes de hablar con el servidor central, el operador debe autenticarse contra un servicio externo.
Esta comunicación no usa el protocolo TCP textual del sistema, sino HTTP REST.

Flujo general:
1. El operador envía usuario y contraseña al servicio auth.
2. El servicio verifica las credenciales contra un archivo JSON.
3. Si son válidas, devuelve un token.
4. Ese token luego será usado para registrarse en el servidor central.

### Comunicación entre operador y servidor central
Una vez autenticado, el operador se comunica con el servidor central por TCP usando el protocolo textual.

Flujo general:
1. El operador abre una conexión TCP con el servidor.
2. Envía un mensaje de registro
3. El servidor valida ese token consultando al servicio auth.
4. Si todo es correcto, registra al operador.
5. El operador puede enviar consultas
6. En paralelo, el operador puede recibir alertas push

Aspecto importante:
El operador no solo consulta; también recibe mensajes espontáneos del servidor.
Por eso el cliente operador debe manejar comunicación bidireccional persistente.

### Comunicación entre servidor central y servicio de autenticación
Cuando el operador envía su token al servidor principal, el servidor no “adivina” si es válido. Lo verifica consultando al auth service por HTTP.

Flujo:
1. El operador manda un mensaje de registro 
2. El servidor central hace una solicitud HTTP al auth service
3. El auth service responde si el token es válido o no.
4. Según esa respuesta, el servidor acepta o rechaza al operador.
De este modo, el servidor principal delega la autenticación al servicio especializado.

### Comunicación con la interfaz web
El servidor central también expone un puerto HTTP para servir el dashboard web.

Esto significa que el mismo servidor tiene dos canales de comunicación distintos:
- TCP en 8080 para protocolo de monitoreo
- HTTP en 8081 para visualización web

Así, el sistema separa la lógica de monitoreo de la capa de presentación web.



## Protocolo de Aplicación (Capa 7)
El sistema usa un protocolo de capa de aplicación diseñado específicamente para la comunicación entre sensores, operadores y servidor.

### Características del protocolo
- basado en texto
- transportado sobre TCP
- campos separados por espacios
- cada mensaje termina en \n
- comandos en mayúsculas
- fácil de implementar en C, Python y Java
- fácil de depurar manualmente

### Regla importante sobre TCP
TCP es un protocolo orientado a flujo, no a mensajes.
Eso significa que varios mensajes pueden llegar juntos o partidos en distintas lecturas.

Por eso, el sistema usa \n como delimitador obligatorio de fin de mensaje.
El receptor no debe procesar datos hasta haber leído un mensaje completo terminado en salto de línea.


## Qué hace el servidor internamente cuando recibe mensajes
Esta es la parte central del sistema. El servidor no solo recibe cadenas de texto: internamente sigue una secuencia lógica para decidir qué hacer con cada mensaje.

### Aceptación de conexiones
El servidor crea sockets de escucha para sus puertos y espera conexiones entrantes.

Cuando un cliente se conecta:
- acepta la conexión
- obtiene el socket del cliente
- crea un hilo dedicado para atenderlo
- ese hilo se encargará de leer y procesar sus mensajes

Gracias a esto, múltiples sensores y operadores pueden estar conectados simultáneamente.

### dentificación del cliente
Al conectarse, el cliente todavía no tiene rol asignado.

El primer mensaje obligatorio debe ser un REGISTER.

El servidor lee el mensaje y determina si el cliente es:
- un sensor
- un operador

Hasta que eso ocurra, el servidor no debería permitir otras operaciones.

### Lectura del mensaje completo
Como TCP trabaja por flujo, el servidor no puede asumir que cada recv() devuelve un mensaje completo.

Internamente debe:
- acumular bytes en un buffer
- buscar el delimitador \n
- extraer una línea completa
- recién ahí parsearla y procesarla

Esto evita errores por mensajes incompletos o concatenados.

### Parseo del comando
Una vez recibido un mensaje completo, el servidor:
1. separa la línea por espacios
2. identifica el comando principal
3. valida la cantidad de campos
4. verifica si el cliente ya está registrado
5. ejecuta la lógica correspondiente

Por ejemplo:
- si el mensaje empieza por REGISTER, entra a la lógica de registro
- si empieza por DATA, entra a la lógica de procesamiento de medición
- si empieza por GET, entra a la lógica de consulta
- si empieza por DISCONNECT, entra a la lógica de cierre limpio

### Si el mensaje es REGISTER SENSOR
El servidor:
1. valida el formato
2. verifica que el tipo de sensor sea válido
3. revisa si el ID ya existe
4. registra al sensor en sus estructuras internas
5. marca su estado como online
6. asocia ese socket con el sensor

Además, internamente puede crear o inicializar el almacenamiento en memoria de sus últimas mediciones.

### Si el mensaje es REGISTER OPERATOR
El servidor:
1. valida el formato
2. extrae usuario y token
3. consulta al servicio de autenticación vía HTTP
4. espera la respuesta
5. si el token es válido:
  - registra al operador
  - asocia ese socket con el operador
  - lo agrega a la lista de receptores de alertas
6. si el token no es válido:
  - responde error
  - no habilita al operador

Aquí el servidor actúa como coordinador entre el operador y el auth service.

### Si el mensaje es DATA
Cuando llega una medición de un sensor, el servidor ejecuta una de las partes más importantes del sistema.
Internamente hace esto:
1. valida que el cliente esté registrado como sensor
2. valida el formato del mensaje
3. verifica que el sensor_id exista
4. convierte el valor a tipo numérico
5. valida unidad y timestamp
6. localiza el tipo de sensor asociado a ese ID
7. guarda la nueva medición en memoria
8. si hay más de 20 registros, conserva solo las últimas 20
9. evalúa el valor contra los umbrales del sensor
10. determina si hay condición normal, warning o critical
11. si hay anomalía:
  - construye un mensaje ALERT
  - lo envía a todos los operadores conectados
  - incrementa el contador de alertas

### Lógica de detección de anomalías
El servidor tiene internamente umbrales para cada tipo de sensor.

Ejemplo conceptual:
- temperatura mayor a 75 → WARNING
- temperatura mayor a 90 → CRITICAL

La lógica general es:
1. comparar el valor recibido con el rango esperado
2. si está fuera del rango normal, clasificar severidad
3. generar una alerta para operadores
4. opcionalmente detectar si volvió a normalidad y enviar un INFO

De esta forma, el servidor no solo almacena datos: también interpreta el estado del sistema.

### Si el mensaje es GET SENSORS
El servidor:
1. verifica que el cliente sea operador
2. recorre la lista de sensores registrados
3. construye una respuesta con:
  - cantidad total
  - ID
  - tipo
  - estado online/offline
4. envía la respuesta al operador

Esto permite al operador conocer qué sensores están activos.

### Si el mensaje es GET DATA
El servidor:
1. verifica que el cliente sea operador
2. busca el sensor solicitado
3. recupera sus últimas n mediciones
4. construye la respuesta textual
5. la envía al operador
Así el operador puede consultar historial reciente.

### Si el mensaje es GET STATUS
El servidor calcula y devuelve información global, como:
- cantidad de sensores
- cantidad de operadores
- cantidad de alertas generadas
- tiempo de actividad
Esto ofrece una vista general del estado del sistema.

### Si el mensaje es DISCONNECT
El servidor:
1. identifica al cliente asociado al socket
2. cierra el socket
3. libera memoria o estructuras asociadas
4. si era sensor, lo marca como offline
5. si era operador, lo elimina de la lista de receptores de alertas
Esto permite una salida ordenada

### Si un cliente se desconecta abruptamente
Si el cliente no envía DISCONNECT pero la conexión falla, el servidor debe detectarlo por error en lectura o escritura.

Internamente debe:
- cerrar el socket
- limpiar recursos
- evitar que el fallo de un cliente afecte a los demás
- seguir funcionando normalmente
Este punto es fundamental en un sistema concurrente robusto.


## Manejo de concurrencia
El servidor usa pthread para trabajar con múltiples clientes simultáneamente.

Modelo de concurrencia:
- el hilo principal escucha nuevas conexiones
- por cada conexión se crea un hilo de atención
- cada hilo maneja la sesión de un cliente
- varios sensores pueden enviar datos al mismo tiempo
- varios operadores pueden consultar y recibir alertas simultáneamente

Esto evita que un cliente bloquee a los demás y permite comportamiento en tiempo real.


## Tecnologias usadas
Cada tecnología fue elegida por una razón concreta dentro de la arquitectura.

### C en el servidor central
Rol: núcleo del sistema de monitoreo y comunicación concurrente.

Se usa C porque permite:
- control de bajo nivel sobre sockets
- manejo eficiente de memoria
- implementación directa de concurrencia con pthread
- alto rendimiento para procesamiento de conexiones
- cercanía con conceptos clásicos de sistemas y redes

Dentro del proyecto, C representa la parte más cercana a infraestructura de red y sistemas operativos.

### Python en el servicio de autenticación
Rol: microservicio REST de autenticación.

Se usa Python con Flask porque permite:
- construir APIs HTTP rápidamente
- manejar JSON con facilidad
- separar la lógica de autenticación del servidor principal
- facilitar pruebas y mantenimiento

Aquí Python cumple el papel de servicio ligero, desacoplado y fácil de extender.

### Python en los sensores
Rol: simulación rápida de dispositivos IoT.

Se usa Python porque:
- permite programar simulaciones de forma sencilla
- facilita temporización y reconexión
- hace muy simple generar datos periódicos
- acelera el desarrollo de múltiples clientes de prueba

En este contexto, Python no busca máximo rendimiento, sino rapidez de construcción y claridad.

### Java Swing en el cliente operador
Rol: interfaz gráfica de escritorio.

Se usa Java Swing porque:
- permite construir una GUI completa
- facilita separar lógica visual y lógica de red
- soporta programación multihilo
- puede mantener una conexión persistente con el servidor mientras actualiza la interfaz

Java cumple el papel de cliente rico de escritorio, orientado a interacción humana.

### Docker
Rol: empaquetado y ejecución uniforme de servicios.

Docker permite:
- aislar dependencias
- ejecutar cada módulo en su propio entorno
- simplificar despliegue y pruebas
- asegurar reproducibilidad entre máquinas
- orquestar varios servicios con docker-compose

En el sistema distribuido, Docker actúa como la capa que hace posible ejecutar todos los módulos de forma coordinada.

### Docker Compose
Rol: orquestación local del sistema distribuido.

Permite levantar varios servicios juntos, por ejemplo:
- servidor central
- auth service
- sensores

Además, habilita red interna con resolución DNS por nombre de servicio, evitando IPs hardcodeadas.

### AWS EC2
Rol: infraestructura real de despliegue.

EC2 aporta:
- acceso remoto
- ejecución del sistema en una máquina real en la nube
- disponibilidad para pruebas desde otras redes
- exposición de puertos necesarios para sensores, auth y dashboard

Esto convierte el proyecto en una práctica real de despliegue de servicios distribuidos.

### TCP
Rol: transporte confiable del protocolo del sistema.

Se usa TCP porque garantiza:
- entrega ordenada
- integridad del flujo
- persistencia de conexión
- base adecuada para alertas push y mensajes de consulta

Es especialmente importante cuando no se quiere perder información de sensores ni alertas.

### HTTP
Rol: comunicación entre servicios y acceso web.

HTTP se usa para:
- autenticación del operador
- validación de tokens
- exposición del dashboard web

Mientras TCP maneja el protocolo propio del sistema, HTTP se usa para interoperabilidad y servicios tipo API.


## Flujo de funcionamiento
### Inicio del sistema
El sistema se levanta con Docker Compose, que inicia los contenedores necesarios.

### Registro de sensores
Cada sensor se conecta al servidor y envía su registro.

### Envío periódico de datos
Los sensores mandan mediciones cada 5 segundos.

### Procesamiento en servidor
El servidor recibe, parsea, valida, almacena y evalúa cada dato.

### Detección de anomalías
Si una lectura está fuera de rango, el servidor construye una alerta.

### Autenticación del operador
El operador obtiene un token desde el auth service.

### Registro del operador
Con ese token, el operador se registra en el servidor central.

### Alertas push
Cuando ocurre una anomalía, el servidor envía alertas a todos los operadores conectados.

### Consultas del operador
El operador puede pedir lista de sensores, historial y estado general.

### Desconexión
Cuando un cliente sale, el servidor libera recursos y actualiza su estado.


## Contenerización y despliegue
El sistema puede desplegarse sobre una instancia Linux usando Docker.

### Infraestructura usada
- AWS EC2
- Ubuntu
- Docker
- Docker Compose

### Puertos necesarios
- 22 → SSH
- 5001 → autenticación
- 8080 → protocolo del sistema
- 8081 → dashboard web


## Ventajas arquitectónicas del sistema
Esta arquitectura distribuida ofrece varias ventajas:
- separación clara de responsabilidades
- facilidad para probar módulos por separado
- posibilidad de reemplazar o mejorar servicios sin rehacer todo
- comunicación interoperable entre distintos lenguajes
- concurrencia real
- despliegue portable gracias a Docker
- diseño cercano a un sistema de monitoreo real


## Conclusion
Este proyecto demuestra la construcción de un sistema distribuido de monitoreo IoT funcional, en el que varios módulos especializados cooperan a través de la red para recolectar, procesar y visualizar información en tiempo real. Su arquitectura permite que los sensores simulados envíen mediciones periódicas al servidor central, que el servicio de autenticación gestione de forma desacoplada el acceso de los operadores y que estos últimos puedan consultar el estado del sistema y recibir alertas de manera inmediata. Además, el servidor cumple un papel fundamental al coordinar la comunicación, interpretar los mensajes del protocolo, almacenar temporalmente las mediciones, detectar anomalías y distribuir notificaciones a los clientes conectados. El uso combinado de C, Python, Java, Docker, TCP, HTTP y AWS no solo responde a necesidades técnicas concretas dentro del sistema, sino que también evidencia una integración realista de conceptos de telemática, concurrencia, protocolos de aplicación, servicios distribuidos y despliegue en la nube, convirtiendo el proyecto en una solución sólida tanto desde el punto de vista académico como práctico.