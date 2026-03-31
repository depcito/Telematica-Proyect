"""
run_all_sensors.py — Lanza los 5 sensores simultáneamente.

Uso:
    python run_all_sensors.py

    # Con servidor en otra máquina:
    SERVER_HOST=iot-monitoring.example.com SERVER_PORT=8080 python run_all_sensors.py
"""

import time
import signal
import sys
from sensors import (
    TemperatureSensor,
    VibrationSensor,
    EnergySensor,
    HumiditySensor,
    StatusSensor,
)

# ── Instanciar los 5 sensores ────────────────────────────────────────────────
SENSORS = [
    TemperatureSensor("temp_01"),
    VibrationSensor("vib_01"),
    EnergySensor("eng_01"),
    HumiditySensor("hum_01"),
    StatusSensor("sta_01"),
]


def shutdown(signum, frame):
    """Detiene todos los sensores limpiamente al recibir SIGINT / SIGTERM."""
    print("\n[MAIN] Deteniendo sensores...")
    for sensor in SENSORS:
        sensor.stop()
    sys.exit(0)


signal.signal(signal.SIGINT,  shutdown)
signal.signal(signal.SIGTERM, shutdown)

if __name__ == "__main__":
    print("[MAIN] Iniciando 5 sensores IoT simulados...")

    threads = []
    for sensor in SENSORS:
        t = sensor.start_thread()
        threads.append(t)
        time.sleep(0.5)   # pequeña pausa entre conexiones para no saturar el servidor

    print(f"[MAIN] {len(threads)} sensores en ejecución. Ctrl+C para detener.\n")

    # Mantener el proceso principal vivo
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        shutdown(None, None)
