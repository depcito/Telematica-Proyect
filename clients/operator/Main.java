import javax.swing.*;

/**
 * Main.java — Punto de entrada del cliente operador IoT.
 *
 * Compilar (desde el directorio clients/operator/):
 *   javac *.java
 *
 * Ejecutar:
 *   java Main
 *
 *   # Con servidor en otra máquina:
 *   SERVER_HOST=iot-monitoring.example.com SERVER_PORT=8080 java Main
 *
 *   # Especificando también el servicio de auth:
 *   SERVER_HOST=192.168.1.10 AUTH_URL=http://192.168.1.10:5001 java Main
 *
 * Requisitos:
 *   - Java 17+ (para switch expressions)
 *   - Sin Maven ni Gradle — Java puro, JDK estándar.
 *   - Todos los .java deben estar en el mismo directorio.
 */
public class Main {

    public static void main(String[] args) {
        // Activar look & feel del sistema (opcional, mejora apariencia en Windows/macOS)
        try {
            // Intentar Nimbus para un look moderno multiplataforma
            for (UIManager.LookAndFeelInfo info : UIManager.getInstalledLookAndFeels()) {
                if ("Nimbus".equals(info.getName())) {
                    UIManager.setLookAndFeel(info.getClassName());
                    break;
                }
            }
        } catch (Exception ignored) {
            // Si Nimbus no está disponible, usar el L&F por defecto (Swing Metal/System)
        }

        // Lanzar GUI en el Event Dispatch Thread (EDT) — obligatorio en Swing
        SwingUtilities.invokeLater(() -> {
            OperatorGUI gui = new OperatorGUI();
            gui.setVisible(true);
        });
    }
}
