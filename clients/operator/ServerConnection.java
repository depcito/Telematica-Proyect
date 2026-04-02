import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.function.Consumer;

/**
 * ServerConnection.java — Conexión TCP al servidor de monitoreo IoT.
 *
 * Responsabilidades:
 *  - Conectar al servidor y registrarse como OPERATOR
 *  - Exponer métodos getSensors(), getData(id, n), getStatus(), disconnect()
 *  - Mantener un hilo de escucha permanente (listener) que detecta mensajes
 *    ALERT entrantes y los reenvía a la GUI mediante un callback.
 *
 * IMPORTANTE (protocolo TCP):
 *  TCP es un protocolo de flujo. Dos mensajes pueden llegar concatenados.
 *  Se usa un BufferedReader que lee línea por línea (\n como delimitador).
 *  El hilo listener consume todas las líneas entrantes. Las respuestas a
 *  queries GET se entregan por una cola sincronizada para no mezclarlas
 *  con las alertas push.
 *
 * Uso típico:
 *   ServerConnection conn = new ServerConnection(username, token, alertHandler);
 *   conn.connect();
 *   String sensors = conn.getSensors();
 *   String data    = conn.getData("temp_01", 5);
 *   conn.disconnect();
 */
public class ServerConnection {

    // ── Estado ─────────────────────────────────────────────────────────────
    private Socket          socket;
    private PrintWriter     writer;
    private BufferedReader  reader;
    private Thread          listenerThread;
    private volatile boolean running = false;

    // Callback para alertas push
    private final Consumer<String> alertHandler;

    // Cola de una sola respuesta: el hilo listener deposita aquí las
    // respuestas a comandos GET/REGISTER, y el hilo principal las recoge.
    private String  pendingResponse = null;
    private boolean responseReady   = false;
    private final Object responseLock = new Object();

    // Info del operador
    private final String username;
    private final String token;

    // ── Constructor ────────────────────────────────────────────────────────

    /**
     * @param username     nombre de usuario del operador
     * @param token        token obtenido del servicio de auth
     * @param alertHandler callback invocado en el hilo listener cuando llega ALERT
     */
    public ServerConnection(String username, String token, Consumer<String> alertHandler) {
        this.username     = username;
        this.token        = token;
        this.alertHandler = alertHandler;
    }

    // ── API PÚBLICA ────────────────────────────────────────────────────────

    /**
     * Conecta al servidor y envía REGISTER OPERATOR.
     *
     * @throws IOException   si falla la conexión TCP
     * @throws ProtocolException si el servidor rechaza el registro
     */
    public void connect() throws IOException, ProtocolException {
        socket = new Socket();
        socket.connect(
                new InetSocketAddress(Config.SERVER_HOST, Config.SERVER_PORT),
                Config.CONNECT_TIMEOUT_MS);
        socket.setSoTimeout(Config.READ_TIMEOUT_MS);  // 0 = sin timeout

        writer = new PrintWriter(
                new OutputStreamWriter(socket.getOutputStream(), StandardCharsets.UTF_8),
                true);  // autoflush
        reader = new BufferedReader(
                new InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8));

        running = true;
        startListener();

        // Registrarse
        String resp = sendAndWait("REGISTER OPERATOR " + username + " " + token);
        if (resp == null || !resp.startsWith("OK")) {
            running = false;
            socket.close();
            throw new ProtocolException("Registro rechazado por el servidor: " + resp);
        }
    }

    /** @return respuesta SENSORS del servidor */
    public String getSensors() throws IOException {
        return sendAndWait("GET SENSORS");
    }

    /**
     * @param sensorId ID del sensor
     * @param n        número de mediciones recientes (max 20)
     * @return respuesta DATA del servidor
     */
    public String getData(String sensorId, int n) throws IOException {
        return sendAndWait("GET DATA " + sensorId + " " + n);
    }

    /** @return respuesta STATUS del servidor */
    public String getStatus() throws IOException {
        return sendAndWait("GET STATUS");
    }

    /** Envía DISCONNECT y cierra el socket. */
    public void disconnect() {
        running = false;
        try {
            if (writer != null) {
                writer.println("DISCONNECT " + username);
            }
        } catch (Exception ignored) {}
        closeSocket();
    }

    /** @return true si la conexión está activa */
    public boolean isConnected() {
        return socket != null && !socket.isClosed() && running;
    }

    // ── HILO LISTENER ─────────────────────────────────────────────────────

    /**
     * El hilo listener lee todas las líneas entrantes del socket.
     * - Si la línea empieza con "ALERT" → llama al alertHandler (GUI)
     * - Si no → es la respuesta a un comando GET → la deposita en pendingResponse
     */
    private void startListener() {
        listenerThread = new Thread(() -> {
            try {
                String line;
                while (running && (line = reader.readLine()) != null) {
                    line = line.trim();
                    if (line.isEmpty()) continue;

                    if (line.startsWith("ALERT")) {
                        // Alerta push: notificar GUI en otro hilo para no bloquear listener
                        final String alert = line;
                        if (alertHandler != null) {
                            new Thread(() -> alertHandler.accept(alert)).start();
                        }
                    } else {
                        // Respuesta a un comando: despertar al hilo que espera
                        synchronized (responseLock) {
                            pendingResponse = line;
                            responseReady   = true;
                            responseLock.notifyAll();
                        }
                    }
                }
            } catch (IOException e) {
                if (running) {
                    // Conexión perdida inesperadamente
                    running = false;
                    if (alertHandler != null) {
                        alertHandler.accept("ALERT CRITICAL connection Connection_lost_with_server");
                    }
                }
            }
        }, "AlertListener-" + username);
        listenerThread.setDaemon(true);
        listenerThread.start();
    }

    // ── ENVÍO Y ESPERA DE RESPUESTA ────────────────────────────────────────

    /**
     * Envía un comando al servidor y espera (bloqueante) su respuesta.
     * Timeout interno de 10 segundos.
     */
    private String sendAndWait(String command) throws IOException {
        if (!isConnected()) throw new IOException("No hay conexión con el servidor");

        synchronized (responseLock) {
            responseReady   = false;
            pendingResponse = null;
        }

        writer.println(command);  // envía con \n (autoflush activado)

        synchronized (responseLock) {
            long deadline = System.currentTimeMillis() + 10_000;
            while (!responseReady) {
                long remaining = deadline - System.currentTimeMillis();
                if (remaining <= 0) throw new IOException("Timeout esperando respuesta del servidor");
                try {
                    responseLock.wait(remaining);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new IOException("Interrumpido esperando respuesta");
                }
            }
            return pendingResponse;
        }
    }

    private void closeSocket() {
        try { if (reader != null) reader.close(); }    catch (Exception ignored) {}
        try { if (writer != null) writer.close(); }    catch (Exception ignored) {}
        try { if (socket != null) socket.close(); }    catch (Exception ignored) {}
    }

    // ── EXCEPCIÓN INTERNA ──────────────────────────────────────────────────

    public static class ProtocolException extends Exception {
        public ProtocolException(String msg) { super(msg); }
    }
}
