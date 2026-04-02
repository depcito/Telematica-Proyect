import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;

/**
 * AuthClient.java — Comunicación con el servicio externo de autenticación.
 *
 * Flujo:
 *  1. login(username, password) → hace HTTP POST /login → devuelve token
 *  2. El token se pasa a ServerConnection para el REGISTER OPERATOR
 *
 * Sin dependencias externas: usa HttpURLConnection de la JDK estándar.
 * Parsea el JSON de la respuesta manualmente (sin librerías externas).
 */
public class AuthClient {

    private final String authUrl;

    public AuthClient() {
        this.authUrl = Config.AUTH_URL;
    }

    public AuthClient(String authUrl) {
        this.authUrl = authUrl;
    }

    // ── API PÚBLICA ────────────────────────────────────────────────────────

    /**
     * Intenta autenticar al usuario contra el servicio de auth.
     *
     * @param username nombre de usuario
     * @param password contraseña en texto plano
     * @return token de autenticación si es válido
     * @throws AuthException si las credenciales son incorrectas o hay error de red
     */
    public String login(String username, String password) throws AuthException {
        String endpoint = authUrl + "/login";
        String body     = String.format(
                "{\"username\": \"%s\", \"password\": \"%s\"}",
                escapeJson(username), escapeJson(password));

        String response;
        try {
            response = httpPost(endpoint, body);
        } catch (IOException e) {
            throw new AuthException("No se pudo conectar al servicio de autenticación: " + e.getMessage());
        }

        // Parsear "token" del JSON: {"token": "abc...", "role": "operator", ...}
        String token = extractJsonString(response, "token");
        if (token == null || token.isBlank()) {
            // El servidor devolvió error: {"error": "Invalid credentials"}
            String error = extractJsonString(response, "error");
            throw new AuthException(error != null ? error : "Credenciales inválidas");
        }

        return token;
    }

    // ── HTTP ───────────────────────────────────────────────────────────────

    private String httpPost(String url, String jsonBody) throws IOException {
        HttpURLConnection conn = (HttpURLConnection) new URL(url).openConnection();
        conn.setRequestMethod("POST");
        conn.setRequestProperty("Content-Type", "application/json; charset=UTF-8");
        conn.setConnectTimeout(Config.CONNECT_TIMEOUT_MS);
        conn.setReadTimeout(5000);
        conn.setDoOutput(true);

        try (OutputStream os = conn.getOutputStream()) {
            os.write(jsonBody.getBytes(StandardCharsets.UTF_8));
        }

        // Leer respuesta (puede ser 200 o 401)
        InputStream is;
        try {
            is = conn.getInputStream();
        } catch (IOException e) {
            is = conn.getErrorStream();
            if (is == null) throw e;
        }

        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(is, StandardCharsets.UTF_8))) {
            StringBuilder sb = new StringBuilder();
            String line;
            while ((line = reader.readLine()) != null) sb.append(line);
            return sb.toString();
        }
    }

    // ── PARSEO JSON MANUAL ─────────────────────────────────────────────────
    // Sin dependencias externas. Funciona para JSONs planos (sin anidamiento).

    /**
     * Extrae el valor de una clave string del JSON.
     * Ejemplo: extractJsonString({"token": "abc123"}, "token") → "abc123"
     */
    static String extractJsonString(String json, String key) {
        if (json == null) return null;
        // Buscar: "key": "value"
        String search = "\"" + key + "\"";
        int idx = json.indexOf(search);
        if (idx < 0) return null;

        int colon = json.indexOf(':', idx + search.length());
        if (colon < 0) return null;

        // Avanzar al primer "
        int start = json.indexOf('"', colon + 1);
        if (start < 0) return null;

        int end = json.indexOf('"', start + 1);
        if (end < 0) return null;

        return json.substring(start + 1, end);
    }

    /** Escapa caracteres especiales dentro de un valor JSON string. */
    private static String escapeJson(String s) {
        return s.replace("\\", "\\\\")
                .replace("\"", "\\\"")
                .replace("\n", "\\n")
                .replace("\r", "\\r");
    }

    // ── EXCEPCIÓN INTERNA ──────────────────────────────────────────────────

    /**
     * Excepción específica para errores de autenticación.
     * Separada de IOException para que la GUI pueda mostrar mensajes claros.
     */
    public static class AuthException extends Exception {
        public AuthException(String message) {
            super(message);
        }
    }
}
