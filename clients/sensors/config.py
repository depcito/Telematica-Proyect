import os

# ─── Conexión al servidor ───────────────────────────────────────────────────
# Nunca hardcodear una IP. Se lee de variable de entorno o se usa el nombre DNS.
SERVER_HOST = os.environ.get("SERVER_HOST", "localhost")
SERVER_PORT = int(os.environ.get("SERVER_PORT", "8080"))

# ─── Comportamiento de los sensores ────────────────────────────────────────
SEND_INTERVAL  = int(os.environ.get("SEND_INTERVAL", "5"))  # segundos entre cada medición
RECONNECT_WAIT = 5      # segundos antes de intentar reconectar
MAX_RECONNECTS = 10     # intentos máximos de reconexión (0 = infinito)
