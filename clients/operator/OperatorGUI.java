import javax.swing.*;
import javax.swing.border.*;
import java.awt.*;
import java.awt.event.*;
import java.util.ArrayList;
import java.util.List;
import java.util.Timer;
import java.util.TimerTask;

/**
 * OperatorGUI.java — Interfaz gráfica Swing del cliente operador.
 *
 * Pantallas:
 *  1. Login: usuario + contraseña + botón Conectar
 *  2. Principal (3 paneles):
 *     - Panel izquierdo: lista de sensores activos (refresco cada 5s)
 *     - Panel central:   mediciones del sensor seleccionado
 *     - Panel derecho:   log de alertas con colores (INFO=azul, WARNING=naranja, CRITICAL=rojo)
 *
 * Colores de alertas según protocolo (docs/PROTOCOL.md sección 8):
 *  INFO     → azul
 *  WARNING  → naranja
 *  CRITICAL → rojo
 */
@SuppressWarnings("unused")
public class OperatorGUI extends JFrame {

    // ── Estado ─────────────────────────────────────────────────────────────
    private ServerConnection connection = null;
    private Timer            refreshTimer = null;

    // ── Paleta de colores ──────────────────────────────────────────────────
    private static final Color BG_DARK      = new Color(30, 32, 40);
    private static final Color BG_PANEL     = new Color(42, 45, 58);
    private static final Color BG_CARD      = new Color(52, 56, 72);
    private static final Color ACCENT_BLUE  = new Color(64, 132, 238);
    private static final Color TEXT_PRIMARY  = new Color(230, 232, 240);
    private static final Color TEXT_SECONDARY = new Color(150, 155, 175);
    private static final Color ALERT_INFO    = new Color(80, 160, 255);
    private static final Color ALERT_WARNING = new Color(255, 165, 50);
    private static final Color ALERT_CRITICAL = new Color(230, 60, 60);
    private static final Color SENSOR_ONLINE  = new Color(70, 200, 120);
    private static final Color SENSOR_OFFLINE = new Color(180, 60, 60);

    // ── Componentes — Login ────────────────────────────────────────────────
    private JPanel      loginPanel;
    private JTextField  usernameField;
    private JPasswordField passwordField;
    private JButton     connectButton;
    private JLabel      loginStatusLabel;

    // ── Componentes — Principal ────────────────────────────────────────────
    private JPanel      mainPanel;
    private JList<String> sensorList;
    private DefaultListModel<String> sensorListModel;
    private JTextArea   dataArea;
    private JTextPane   alertArea;
    private JLabel      statusBar;
    private JLabel      connectedLabel;

    // ── Constructor ────────────────────────────────────────────────────────
    public OperatorGUI() {
        super("IoT Monitoring — Operator Client");
        setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        setSize(1100, 700);
        setMinimumSize(new Dimension(900, 600));
        setLocationRelativeTo(null);
        setBackground(BG_DARK);

        buildLoginPanel();
        buildMainPanel();
        showLogin();

        // Cierre limpio
        addWindowListener(new WindowAdapter() {
            @Override public void windowClosing(WindowEvent e) {
                if (connection != null && connection.isConnected()) {
                    connection.disconnect();
                }
            }
        });
    }

    // ─────────────────────────────────────────────────────────────────────
    //  PANTALLA DE LOGIN
    // ─────────────────────────────────────────────────────────────────────

    private void buildLoginPanel() {
        loginPanel = new JPanel(new GridBagLayout());
        loginPanel.setBackground(BG_DARK);

        JPanel card = new JPanel();
        card.setLayout(new BoxLayout(card, BoxLayout.Y_AXIS));
        card.setBackground(BG_PANEL);
        card.setBorder(new CompoundBorder(
                new LineBorder(new Color(70, 80, 100), 1, true),
                new EmptyBorder(40, 50, 40, 50)));

        // Título
        JLabel title = new JLabel("IoT Monitoring System");
        title.setFont(new Font("SansSerif", Font.BOLD, 22));
        title.setForeground(TEXT_PRIMARY);
        title.setAlignmentX(Component.CENTER_ALIGNMENT);

        JLabel subtitle = new JLabel("Operator Login");
        subtitle.setFont(new Font("SansSerif", Font.PLAIN, 13));
        subtitle.setForeground(TEXT_SECONDARY);
        subtitle.setAlignmentX(Component.CENTER_ALIGNMENT);

        card.add(title);
        card.add(Box.createVerticalStrut(4));
        card.add(subtitle);
        card.add(Box.createVerticalStrut(30));

        // Campos
        usernameField = styledTextField(20);
        passwordField = new JPasswordField(20);
        stylePasswordField(passwordField);

        card.add(formRow("Usuario", usernameField));
        card.add(Box.createVerticalStrut(14));
        card.add(formRow("Contraseña", passwordField));
        card.add(Box.createVerticalStrut(24));

        // Botón conectar
        connectButton = new JButton("Conectar");
        styleButton(connectButton, ACCENT_BLUE);
        connectButton.setAlignmentX(Component.CENTER_ALIGNMENT);
        connectButton.addActionListener(e -> doLogin());
        card.add(connectButton);

        // Hints de conexión
        card.add(Box.createVerticalStrut(16));
        JLabel hint = new JLabel("Servidor: " + Config.SERVER_HOST + ":" + Config.SERVER_PORT);
        hint.setFont(new Font("Monospaced", Font.PLAIN, 11));
        hint.setForeground(TEXT_SECONDARY);
        hint.setAlignmentX(Component.CENTER_ALIGNMENT);
        card.add(hint);

        // Status de error
        loginStatusLabel = new JLabel(" ");
        loginStatusLabel.setFont(new Font("SansSerif", Font.PLAIN, 12));
        loginStatusLabel.setForeground(ALERT_CRITICAL);
        loginStatusLabel.setAlignmentX(Component.CENTER_ALIGNMENT);
        card.add(Box.createVerticalStrut(10));
        card.add(loginStatusLabel);

        // Enter en contraseña también dispara login
        passwordField.addActionListener(e -> doLogin());
        usernameField.addActionListener(e -> passwordField.requestFocus());

        loginPanel.add(card);
    }

    private void doLogin() {
        String user = usernameField.getText().trim();
        String pass = new String(passwordField.getPassword());

        if (user.isEmpty() || pass.isEmpty()) {
            loginStatusLabel.setText("Ingresa usuario y contraseña.");
            return;
        }

        connectButton.setEnabled(false);
        loginStatusLabel.setForeground(TEXT_SECONDARY);
        loginStatusLabel.setText("Autenticando...");

        // Autenticar en hilo aparte para no bloquear EDT
        new Thread(() -> {
            try {
                AuthClient auth = new AuthClient();
                String token = auth.login(user, pass);

                // Conectar al servidor
                connection = new ServerConnection(user, token, this::onAlertReceived);
                connection.connect();

                SwingUtilities.invokeLater(() -> {
                    connectedLabel.setText("● Conectado como: " + user);
                    connectedLabel.setForeground(SENSOR_ONLINE);
                    showMain();
                    startAutoRefresh();
                    refreshSensors();
                });

            } catch (AuthClient.AuthException e) {
                SwingUtilities.invokeLater(() -> {
                    loginStatusLabel.setForeground(ALERT_CRITICAL);
                    loginStatusLabel.setText("Error de autenticación: " + e.getMessage());
                    connectButton.setEnabled(true);
                });
            } catch (ServerConnection.ProtocolException e) {
                SwingUtilities.invokeLater(() -> {
                    loginStatusLabel.setForeground(ALERT_CRITICAL);
                    loginStatusLabel.setText("Error de registro: " + e.getMessage());
                    connectButton.setEnabled(true);
                });
            } catch (Exception e) {
                SwingUtilities.invokeLater(() -> {
                    loginStatusLabel.setForeground(ALERT_CRITICAL);
                    loginStatusLabel.setText("Error de conexión: " + e.getMessage());
                    connectButton.setEnabled(true);
                });
            }
        }, "LoginThread").start();
    }

    // ─────────────────────────────────────────────────────────────────────
    //  PANTALLA PRINCIPAL
    // ─────────────────────────────────────────────────────────────────────

    private void buildMainPanel() {
        mainPanel = new JPanel(new BorderLayout(0, 0));
        mainPanel.setBackground(BG_DARK);

        // ── Barra superior ────────────────────────────────────────────────
        JPanel topBar = new JPanel(new BorderLayout());
        topBar.setBackground(BG_PANEL);
        topBar.setBorder(new EmptyBorder(10, 16, 10, 16));

        JLabel appTitle = new JLabel("IoT Monitoring System");
        appTitle.setFont(new Font("SansSerif", Font.BOLD, 16));
        appTitle.setForeground(TEXT_PRIMARY);

        connectedLabel = new JLabel("● Desconectado");
        connectedLabel.setFont(new Font("SansSerif", Font.PLAIN, 12));
        connectedLabel.setForeground(SENSOR_OFFLINE);

        JButton disconnectBtn = new JButton("Desconectar");
        disconnectBtn.setFont(new Font("SansSerif", Font.PLAIN, 11));
        disconnectBtn.setBackground(new Color(90, 40, 40));
        disconnectBtn.setForeground(TEXT_PRIMARY);
        disconnectBtn.setBorderPainted(false);
        disconnectBtn.setFocusPainted(false);
        disconnectBtn.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
        disconnectBtn.addActionListener(e -> doDisconnect());

        JPanel rightTop = new JPanel(new FlowLayout(FlowLayout.RIGHT, 12, 0));
        rightTop.setOpaque(false);
        rightTop.add(connectedLabel);
        rightTop.add(disconnectBtn);

        topBar.add(appTitle, BorderLayout.WEST);
        topBar.add(rightTop, BorderLayout.EAST);

        // ── Panel de 3 columnas ───────────────────────────────────────────
        JSplitPane splitRight = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT,
                buildDataPanel(), buildAlertPanel());
        splitRight.setDividerLocation(500);
        splitRight.setDividerSize(4);
        splitRight.setBorder(null);
        splitRight.setBackground(BG_DARK);

        JSplitPane splitMain = new JSplitPane(JSplitPane.HORIZONTAL_SPLIT,
                buildSensorPanel(), splitRight);
        splitMain.setDividerLocation(230);
        splitMain.setDividerSize(4);
        splitMain.setBorder(null);
        splitMain.setBackground(BG_DARK);

        // ── Barra de estado inferior ──────────────────────────────────────
        statusBar = new JLabel("  Listo.");
        statusBar.setFont(new Font("Monospaced", Font.PLAIN, 11));
        statusBar.setForeground(TEXT_SECONDARY);
        statusBar.setBackground(BG_PANEL);
        statusBar.setOpaque(true);
        statusBar.setBorder(new EmptyBorder(4, 12, 4, 12));

        mainPanel.add(topBar,    BorderLayout.NORTH);
        mainPanel.add(splitMain, BorderLayout.CENTER);
        mainPanel.add(statusBar, BorderLayout.SOUTH);
    }

    private JPanel buildSensorPanel() {
        JPanel panel = new JPanel(new BorderLayout());
        panel.setBackground(BG_PANEL);
        panel.setBorder(new EmptyBorder(0, 0, 0, 0));

        // Título
        JPanel header = sectionHeader("Sensores Activos");
        JButton refreshBtn = new JButton("↻");
        refreshBtn.setFont(new Font("SansSerif", Font.BOLD, 14));
        refreshBtn.setForeground(ACCENT_BLUE);
        refreshBtn.setBackground(BG_PANEL);
        refreshBtn.setBorderPainted(false);
        refreshBtn.setFocusPainted(false);
        refreshBtn.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
        refreshBtn.setToolTipText("Actualizar lista de sensores");
        refreshBtn.addActionListener(e -> refreshSensors());
        header.add(refreshBtn, BorderLayout.EAST);

        // Lista de sensores
        sensorListModel = new DefaultListModel<>();
        sensorList = new JList<>(sensorListModel);
        sensorList.setBackground(BG_CARD);
        sensorList.setForeground(TEXT_PRIMARY);
        sensorList.setFont(new Font("Monospaced", Font.PLAIN, 12));
        sensorList.setSelectionBackground(new Color(64, 100, 180));
        sensorList.setSelectionForeground(Color.WHITE);
        sensorList.setCellRenderer(new SensorCellRenderer());
        sensorList.setFixedCellHeight(44);

        sensorList.addListSelectionListener(e -> {
            if (!e.getValueIsAdjusting()) {
                String selected = sensorList.getSelectedValue();
                if (selected != null) {
                    // Extraer sensor_id (primer token antes del espacio / |)
                    String sensorId = selected.split("[ |:]+")[0].trim();
                    loadSensorData(sensorId);
                }
            }
        });

        JScrollPane scroll = new JScrollPane(sensorList);
        scroll.setBorder(null);
        scroll.setBackground(BG_CARD);

        // Botón GET STATUS
        JButton statusBtn = new JButton("Ver Estado del Sistema");
        styleButton(statusBtn, new Color(60, 80, 120));
        statusBtn.setMargin(new Insets(6, 8, 6, 8));
        statusBtn.addActionListener(e -> loadStatus());

        JPanel bottom = new JPanel(new BorderLayout());
        bottom.setBackground(BG_PANEL);
        bottom.setBorder(new EmptyBorder(6, 8, 8, 8));
        bottom.add(statusBtn);

        panel.add(header, BorderLayout.NORTH);
        panel.add(scroll,  BorderLayout.CENTER);
        panel.add(bottom,  BorderLayout.SOUTH);
        return panel;
    }

    private JPanel buildDataPanel() {
        JPanel panel = new JPanel(new BorderLayout());
        panel.setBackground(BG_PANEL);

        JPanel header = sectionHeader("Mediciones del Sensor");

        dataArea = new JTextArea();
        dataArea.setEditable(false);
        dataArea.setBackground(BG_CARD);
        dataArea.setForeground(TEXT_PRIMARY);
        dataArea.setFont(new Font("Monospaced", Font.PLAIN, 12));
        dataArea.setBorder(new EmptyBorder(10, 12, 10, 12));
        dataArea.setLineWrap(true);
        dataArea.setWrapStyleWord(true);
        dataArea.setText("← Selecciona un sensor para ver sus mediciones.");

        JScrollPane scroll = new JScrollPane(dataArea);
        scroll.setBorder(null);

        panel.add(header, BorderLayout.NORTH);
        panel.add(scroll,  BorderLayout.CENTER);
        return panel;
    }

    private JPanel buildAlertPanel() {
        JPanel panel = new JPanel(new BorderLayout());
        panel.setBackground(BG_PANEL);

        JPanel header = sectionHeader("Alertas en Tiempo Real");

        JButton clearBtn = new JButton("Limpiar");
        clearBtn.setFont(new Font("SansSerif", Font.PLAIN, 11));
        clearBtn.setForeground(TEXT_SECONDARY);
        clearBtn.setBackground(BG_PANEL);
        clearBtn.setBorderPainted(false);
        clearBtn.setFocusPainted(false);
        clearBtn.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
        clearBtn.addActionListener(e -> {
            alertArea.setText("");
        });
        header.add(clearBtn, BorderLayout.EAST);

        alertArea = new JTextPane();
        alertArea.setEditable(false);
        alertArea.setBackground(BG_CARD);
        alertArea.setBorder(new EmptyBorder(8, 10, 8, 10));

        JScrollPane scroll = new JScrollPane(alertArea);
        scroll.setBorder(null);

        // Leyenda de colores
        JPanel legend = new JPanel(new FlowLayout(FlowLayout.LEFT, 12, 4));
        legend.setBackground(BG_PANEL);
        legend.add(legendItem("INFO", ALERT_INFO));
        legend.add(legendItem("WARNING", ALERT_WARNING));
        legend.add(legendItem("CRITICAL", ALERT_CRITICAL));

        panel.add(header, BorderLayout.NORTH);
        panel.add(scroll,  BorderLayout.CENTER);
        panel.add(legend,  BorderLayout.SOUTH);
        return panel;
    }

    // ─────────────────────────────────────────────────────────────────────
    //  LÓGICA DE DATOS
    // ─────────────────────────────────────────────────────────────────────

    /** Carga la lista de sensores y actualiza el panel izquierdo. */
    private void refreshSensors() {
        if (connection == null || !connection.isConnected()) return;
        new Thread(() -> {
            try {
                String resp = connection.getSensors();
                // Formato: SENSORS 3 temp_01:TEMPERATURE:online vib_01:VIBRATION:online ...
                SwingUtilities.invokeLater(() -> updateSensorList(resp));
            } catch (Exception e) {
                SwingUtilities.invokeLater(() ->
                        statusBar.setText("  Error al obtener sensores: " + e.getMessage()));
            }
        }).start();
    }

    private void updateSensorList(String resp) {
        if (resp == null || !resp.startsWith("SENSORS")) return;
        String[] parts = resp.split(" ");
        sensorListModel.clear();
        // parts[0]=SENSORS, parts[1]=count, parts[2..n]=id:type:status
        for (int i = 2; i < parts.length; i++) {
            sensorListModel.addElement(parts[i]);  // ej: "temp_01:TEMPERATURE:online"
        }
        statusBar.setText("  Sensores actualizados. Total: " + (parts.length - 2));
    }

    /** Carga las últimas 10 mediciones del sensor seleccionado. */
    private void loadSensorData(String sensorId) {
        if (connection == null || !connection.isConnected()) return;
        dataArea.setText("Cargando mediciones de " + sensorId + "...");
        new Thread(() -> {
            try {
                String resp = connection.getData(sensorId, 10);
                SwingUtilities.invokeLater(() -> formatDataResponse(sensorId, resp));
            } catch (Exception e) {
                SwingUtilities.invokeLater(() ->
                        dataArea.setText("Error al obtener datos: " + e.getMessage()));
            }
        }).start();
    }

    private void formatDataResponse(String sensorId, String resp) {
        if (resp == null) { dataArea.setText("Sin respuesta del servidor."); return; }
        if (resp.startsWith("ERROR")) { dataArea.setText("⚠ " + resp); return; }

        // Formato: DATA temp_01 3 72.10:1717000010 85.30:1717000015 91.50:1717000020
        String[] parts = resp.split(" ");
        if (parts.length < 3) { dataArea.setText(resp); return; }

        StringBuilder sb = new StringBuilder();
        sb.append("Sensor: ").append(sensorId).append("\n");
        sb.append("Mediciones disponibles: ").append(parts[2]).append("\n");
        sb.append("─────────────────────────────────────\n");

        for (int i = 3; i < parts.length; i++) {
            String[] pair = parts[i].split(":");
            if (pair.length == 2) {
                try {
                    double value = Double.parseDouble(pair[0]);
                    long   ts    = Long.parseLong(pair[1]);
                    java.util.Date date = new java.util.Date(ts * 1000L);
                    sb.append(String.format("  %.2f   %s%n", value, date.toString()));
                } catch (NumberFormatException e) {
                    sb.append("  ").append(parts[i]).append("\n");
                }
            }
        }
        dataArea.setText(sb.toString());
    }

    private void loadStatus() {
        if (connection == null || !connection.isConnected()) return;
        new Thread(() -> {
            try {
                String resp = connection.getStatus();
                SwingUtilities.invokeLater(() -> dataArea.setText("Estado del sistema:\n\n" + resp
                        .replace(" ", "\n  ")));
            } catch (Exception e) {
                SwingUtilities.invokeLater(() ->
                        dataArea.setText("Error: " + e.getMessage()));
            }
        }).start();
    }

    // ─────────────────────────────────────────────────────────────────────
    //  ALERTAS PUSH
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Llamado por el hilo listener de ServerConnection cuando llega un ALERT.
     * Debe ser thread-safe — se despacha al EDT con SwingUtilities.invokeLater.
     *
     * Formato: ALERT WARNING temp_01 Temperature_above_threshold:87.3_CELSIUS
     */
    private void onAlertReceived(String alertLine) {
        SwingUtilities.invokeLater(() -> appendAlert(alertLine));
    }

    private void appendAlert(String alertLine) {
        // Parsear: ALERT <LEVEL> <sensor_id> <message>
        String[] parts = alertLine.split(" ", 4);
        String level   = parts.length > 1 ? parts[1] : "INFO";
        String sensor  = parts.length > 2 ? parts[2] : "?";
        String message = parts.length > 3 ? parts[3].replace("_", " ") : "";

        Color color = switch (level) {
            case "CRITICAL" -> ALERT_CRITICAL;
            case "WARNING"  -> ALERT_WARNING;
            default         -> ALERT_INFO;
        };

        // Timestamp actual
        String ts = new java.text.SimpleDateFormat("HH:mm:ss").format(new java.util.Date());

        // Insertar texto coloreado en JTextPane
        javax.swing.text.StyledDocument doc = alertArea.getStyledDocument();
        javax.swing.text.Style style = alertArea.addStyle("alert", null);
        javax.swing.text.StyleConstants.setForeground(style, color);
        javax.swing.text.StyleConstants.setBold(style, level.equals("CRITICAL"));
        javax.swing.text.StyleConstants.setFontFamily(style, "Monospaced");
        javax.swing.text.StyleConstants.setFontSize(style, 12);

        try {
            String text = String.format("[%s] %-8s | %s | %s%n", ts, level, sensor, message);
            doc.insertString(doc.getLength(), text, style);
            // Auto-scroll al final
            alertArea.setCaretPosition(doc.getLength());
        } catch (javax.swing.text.BadLocationException ignored) {}

        // Flash en la barra de estado
        statusBar.setForeground(color);
        statusBar.setText("  Nueva alerta: " + level + " — " + sensor);
        new Timer().schedule(new TimerTask() {
            @Override public void run() {
                SwingUtilities.invokeLater(() -> {
                    statusBar.setForeground(TEXT_SECONDARY);
                    statusBar.setText("  Listo.");
                });
            }
        }, 4000);
    }

    // ─────────────────────────────────────────────────────────────────────
    //  NAVEGACIÓN ENTRE PANTALLAS
    // ─────────────────────────────────────────────────────────────────────

    private void showLogin() {
        getContentPane().removeAll();
        getContentPane().add(loginPanel);
        connectButton.setEnabled(true);
        loginStatusLabel.setText(" ");
        revalidate(); repaint();
    }

    private void showMain() {
        getContentPane().removeAll();
        getContentPane().add(mainPanel);
        revalidate(); repaint();
    }

    private void doDisconnect() {
        stopAutoRefresh();
        if (connection != null) {
            connection.disconnect();
            connection = null;
        }
        showLogin();
    }

    // ─────────────────────────────────────────────────────────────────────
    //  AUTO-REFRESCO DE SENSORES (cada 5 segundos)
    // ─────────────────────────────────────────────────────────────────────

    private void startAutoRefresh() {
        refreshTimer = new Timer("SensorRefresh", true);
        refreshTimer.scheduleAtFixedRate(new TimerTask() {
            @Override public void run() { refreshSensors(); }
        }, Config.SENSOR_REFRESH_MS, Config.SENSOR_REFRESH_MS);
    }

    private void stopAutoRefresh() {
        if (refreshTimer != null) {
            refreshTimer.cancel();
            refreshTimer = null;
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    //  HELPERS DE ESTILO
    // ─────────────────────────────────────────────────────────────────────

    private JPanel sectionHeader(String title) {
        JPanel p = new JPanel(new BorderLayout());
        p.setBackground(new Color(35, 38, 50));
        p.setBorder(new CompoundBorder(
                new MatteBorder(0, 0, 1, 0, new Color(60, 65, 85)),
                new EmptyBorder(8, 12, 8, 12)));
        JLabel lbl = new JLabel(title);
        lbl.setFont(new Font("SansSerif", Font.BOLD, 13));
        lbl.setForeground(TEXT_PRIMARY);
        p.add(lbl, BorderLayout.WEST);
        return p;
    }

    private JPanel formRow(String label, JComponent field) {
        JPanel row = new JPanel(new BorderLayout(0, 4));
        row.setOpaque(false);
        JLabel lbl = new JLabel(label);
        lbl.setFont(new Font("SansSerif", Font.PLAIN, 12));
        lbl.setForeground(TEXT_SECONDARY);
        row.add(lbl, BorderLayout.NORTH);
        row.add(field, BorderLayout.CENTER);
        return row;
    }

    private JTextField styledTextField(int cols) {
        JTextField f = new JTextField(cols);
        f.setBackground(BG_CARD);
        f.setForeground(TEXT_PRIMARY);
        f.setCaretColor(TEXT_PRIMARY);
        f.setFont(new Font("SansSerif", Font.PLAIN, 13));
        f.setBorder(new CompoundBorder(
                new LineBorder(new Color(70, 80, 110), 1, true),
                new EmptyBorder(6, 10, 6, 10)));
        return f;
    }

    private void stylePasswordField(JPasswordField f) {
        f.setBackground(BG_CARD);
        f.setForeground(TEXT_PRIMARY);
        f.setCaretColor(TEXT_PRIMARY);
        f.setFont(new Font("SansSerif", Font.PLAIN, 13));
        f.setBorder(new CompoundBorder(
                new LineBorder(new Color(70, 80, 110), 1, true),
                new EmptyBorder(6, 10, 6, 10)));
    }

    private void styleButton(JButton btn, Color bg) {
        btn.setBackground(bg);
        btn.setForeground(Color.WHITE);
        btn.setFont(new Font("SansSerif", Font.BOLD, 13));
        btn.setBorderPainted(false);
        btn.setFocusPainted(false);
        btn.setCursor(Cursor.getPredefinedCursor(Cursor.HAND_CURSOR));
        btn.setPreferredSize(new Dimension(200, 38));
        btn.setMaximumSize(new Dimension(Integer.MAX_VALUE, 38));
    }

    private JLabel legendItem(String text, Color color) {
        JLabel lbl = new JLabel("■ " + text);
        lbl.setFont(new Font("SansSerif", Font.BOLD, 11));
        lbl.setForeground(color);
        return lbl;
    }

    // ─────────────────────────────────────────────────────────────────────
    //  RENDERER DE LA LISTA DE SENSORES
    // ─────────────────────────────────────────────────────────────────────

    /**
     * Renderiza cada celda de la lista de sensores con icono de estado y colores.
     * Cada elemento tiene el formato: "temp_01:TEMPERATURE:online"
     */
    private class SensorCellRenderer extends DefaultListCellRenderer {
        @Override
        public Component getListCellRendererComponent(JList<?> list, Object value,
                int index, boolean isSelected, boolean cellHasFocus) {

            JPanel cell = new JPanel(new BorderLayout(8, 0));
            cell.setBorder(new EmptyBorder(8, 12, 8, 12));

            String raw = value.toString();
            String[] parts = raw.split(":");
            String id     = parts.length > 0 ? parts[0] : raw;
            String type   = parts.length > 1 ? parts[1] : "";
            String status = parts.length > 2 ? parts[2] : "offline";

            boolean online = "online".equalsIgnoreCase(status);

            // Indicador de estado (círculo de color)
            JLabel dot = new JLabel("●");
            dot.setFont(new Font("SansSerif", Font.BOLD, 14));
            dot.setForeground(online ? SENSOR_ONLINE : SENSOR_OFFLINE);

            // Info del sensor
            JPanel info = new JPanel();
            info.setLayout(new BoxLayout(info, BoxLayout.Y_AXIS));
            info.setOpaque(false);

            JLabel idLabel = new JLabel(id);
            idLabel.setFont(new Font("Monospaced", Font.BOLD, 12));
            idLabel.setForeground(TEXT_PRIMARY);

            JLabel typeLabel = new JLabel(type + " · " + status.toUpperCase());
            typeLabel.setFont(new Font("SansSerif", Font.PLAIN, 10));
            typeLabel.setForeground(online ? SENSOR_ONLINE : SENSOR_OFFLINE);

            info.add(idLabel);
            info.add(typeLabel);

            cell.add(dot,  BorderLayout.WEST);
            cell.add(info, BorderLayout.CENTER);

            if (isSelected) {
                cell.setBackground(new Color(64, 100, 180));
            } else {
                cell.setBackground(index % 2 == 0 ? BG_CARD : new Color(48, 52, 66));
            }

            return cell;
        }
    }
}
