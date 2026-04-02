/**
 * Config.java — Configuración centralizada del cliente operador.
 *
 * Lee SERVER_HOST, SERVER_PORT y AUTH_URL desde variables de entorno.
 * Si no están definidas, usa valores por defecto.
 *
 * Uso:
 *   export SERVER_HOST=iot-monitoring.example.com
 *   export SERVER_PORT=8080
 *   export AUTH_URL=http://auth-server:5001
 */
public class Config {

    /** Host del servidor principal de monitoreo IoT. */
    public static final String SERVER_HOST = getEnv("SERVER_HOST", "localhost");

    /** Puerto del servidor principal. */
    public static final int SERVER_PORT = Integer.parseInt(getEnv("SERVER_PORT", "8080"));

    /**
     * URL base del servicio de autenticación externo.
     * Ejemplos:
     *   http://auth-server:5001
     *   http://192.168.1.10:5001
     */
    public static final String AUTH_URL = getEnv("AUTH_URL", "http://localhost:5001");

    /** Timeout de conexión al servidor (ms). */
    public static final int CONNECT_TIMEOUT_MS = 5000;

    /** Timeout de lectura del socket (ms). 0 = sin timeout (espera indefinida). */
    public static final int READ_TIMEOUT_MS = 0;

    /** Intervalo de refresco de la lista de sensores en la GUI (ms). */
    public static final int SENSOR_REFRESH_MS = 5000;

    // ── Utilidad ───────────────────────────────────────────────────────────
    private static String getEnv(String key, String defaultValue) {
        String val = System.getenv(key);
        return (val != null && !val.isBlank()) ? val : defaultValue;
    }

    // No instanciar
    private Config() {}
}
