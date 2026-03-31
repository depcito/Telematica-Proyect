"""
sensor_base.py — Clase base para todos los sensores IoT simulados.

Cada sensor concreto (temperatura, vibración, etc.) hereda de esta clase
y solo necesita implementar el método generate_value().
"""

import socket
import time
import threading
import random
from config import SERVER_HOST, SERVER_PORT, SEND_INTERVAL, RECONNECT_WAIT, MAX_RECONNECTS


class SensorBase:
    """
    Clase base que gestiona la conexión TCP, el registro y el envío
    periódico de mediciones al servidor de monitoreo.
    """

    def __init__(self, sensor_id: str, sensor_type: str, unit: str):
        self.sensor_id   = sensor_id
        self.sensor_type = sensor_type
        self.unit        = unit
        self.sock        = None
        self.running     = False
        self._lock       = threading.Lock()

    # ── Implementar en cada subclase ─────────────────────────────────────
    def generate_value(self) -> float:
        """Devuelve una medición simulada. Debe sobrescribirse."""
        raise NotImplementedError("generate_value() debe implementarse en la subclase")

    # ── Protocolo: construcción de mensajes ──────────────────────────────
    def _build_register(self) -> str:
        return f"REGISTER SENSOR {self.sensor_id} {self.sensor_type}\n"

    def _build_data(self, value: float) -> str:
        timestamp = int(time.time())
        return f"DATA {self.sensor_id} {value:.2f} {self.unit} {timestamp}\n"

    def _build_disconnect(self) -> str:
        return f"DISCONNECT {self.sensor_id}\n"

    # ── Comunicación con el servidor ─────────────────────────────────────
    def _send(self, message: str) -> str:
        """Envía un mensaje y espera la respuesta del servidor."""
        try:
            self.sock.sendall(message.encode("utf-8"))
            response = b""
            while b"\n" not in response:
                chunk = self.sock.recv(1024)
                if not chunk:
                    raise ConnectionError("El servidor cerró la conexión")
                response += chunk
            return response.decode("utf-8").strip()
        except Exception as e:
            raise ConnectionError(f"Error de comunicación: {e}")

    def _connect_and_register(self) -> bool:
        """
        Intenta conectarse al servidor y registrar el sensor.
        Devuelve True si el registro fue exitoso.
        """
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.settimeout(10)
            self.sock.connect((SERVER_HOST, SERVER_PORT))
            self.sock.settimeout(None)
            print(f"[{self.sensor_id}] Conectado a {SERVER_HOST}:{SERVER_PORT}")

            response = self._send(self._build_register())
            print(f"[{self.sensor_id}] REGISTER → {response}")

            # Aceptamos OK o ALREADY_REGISTERED (reconexión)
            if response.startswith("OK") or "ALREADY_REGISTERED" in response:
                return True
            else:
                print(f"[{self.sensor_id}] Registro rechazado: {response}")
                return False

        except Exception as e:
            print(f"[{self.sensor_id}] No se pudo conectar: {e}")
            if self.sock:
                self.sock.close()
                self.sock = None
            return False

    # ── Bucle principal ──────────────────────────────────────────────────
    def run(self):
        """
        Bucle de vida del sensor:
        1. Conectar y registrar
        2. Enviar mediciones cada SEND_INTERVAL segundos
        3. Reconectar si la conexión se pierde
        """
        self.running  = True
        reconnects    = 0

        while self.running:
            # Intentar conectar
            if not self._connect_and_register():
                reconnects += 1
                if MAX_RECONNECTS > 0 and reconnects >= MAX_RECONNECTS:
                    print(f"[{self.sensor_id}] Máximo de reconexiones alcanzado. Deteniendo.")
                    break
                print(f"[{self.sensor_id}] Reintentando en {RECONNECT_WAIT}s... "
                      f"(intento {reconnects})")
                time.sleep(RECONNECT_WAIT)
                continue

            reconnects = 0  # reset al reconectar exitosamente

            # Bucle de envío de datos
            while self.running:
                try:
                    value    = self.generate_value()
                    msg      = self._build_data(value)
                    response = self._send(msg)

                    print(f"[{self.sensor_id}] DATA {value:.2f} {self.unit} → {response}")

                    if "ALERT_TRIGGERED" in response:
                        print(f"[{self.sensor_id}] ⚠ ALERTA generada por valor {value:.2f}")

                    time.sleep(SEND_INTERVAL)

                except ConnectionError as e:
                    print(f"[{self.sensor_id}] Conexión perdida: {e}")
                    if self.sock:
                        self.sock.close()
                        self.sock = None
                    break  # salir del bucle interno → reconectar

        self._disconnect()

    def _disconnect(self):
        """Envía DISCONNECT y cierra el socket limpiamente."""
        if self.sock:
            try:
                self.sock.sendall(self._build_disconnect().encode("utf-8"))
                resp = self.sock.recv(1024).decode("utf-8").strip()
                print(f"[{self.sensor_id}] DISCONNECT → {resp}")
            except Exception:
                pass
            finally:
                self.sock.close()
                self.sock = None

    def stop(self):
        """Detiene el sensor desde otro hilo."""
        self.running = False

    def start_thread(self) -> threading.Thread:
        """Lanza el sensor en un hilo daemon y devuelve el hilo."""
        t = threading.Thread(target=self.run, name=self.sensor_id, daemon=True)
        t.start()
        return t
