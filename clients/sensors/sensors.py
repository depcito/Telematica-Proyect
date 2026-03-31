"""
sensors.py — Los 5 sensores simulados del sistema IoT.

Cada clase hereda de SensorBase y solo implementa generate_value()
con valores aleatorios realistas para su tipo de sensor.
"""

import random
from sensor_base import SensorBase


class TemperatureSensor(SensorBase):
    """
    Sensor de temperatura industrial.
    Rango normal: 60–75 °C
    Zona WARNING:  75–90 °C
    Zona CRITICAL: > 90 °C
    """
    def __init__(self, sensor_id="temp_01"):
        super().__init__(sensor_id, "TEMPERATURE", "CELSIUS")
        self._trend = 0.0   # simula tendencia de subida/bajada

    def generate_value(self) -> float:
        # Simula subidas y bajadas graduales con algo de ruido
        self._trend += random.uniform(-2.0, 2.5)
        self._trend  = max(-10.0, min(self._trend, 25.0))
        base  = 72.0 + self._trend
        noise = random.uniform(-1.5, 1.5)
        return round(max(50.0, min(base + noise, 100.0)), 1)


class VibrationSensor(SensorBase):
    """
    Sensor de vibración mecánica.
    Rango normal: 0.5–3.5 mm/s
    Zona WARNING:  3.5–6.0 mm/s
    Zona CRITICAL: > 6.0 mm/s
    """
    def __init__(self, sensor_id="vib_01"):
        super().__init__(sensor_id, "VIBRATION", "MM_S")

    def generate_value(self) -> float:
        # Mayoría de valores normales, picos ocasionales
        if random.random() < 0.1:           # 10% de probabilidad de pico
            return round(random.uniform(4.0, 8.0), 2)
        return round(random.uniform(0.5, 3.5), 2)


class EnergySensor(SensorBase):
    """
    Sensor de consumo energético.
    Rango normal: 200–700 W
    Zona WARNING:  700–900 W
    Zona CRITICAL: > 900 W
    """
    def __init__(self, sensor_id="eng_01"):
        super().__init__(sensor_id, "ENERGY", "WATTS")
        self._base_load = random.uniform(300.0, 600.0)

    def generate_value(self) -> float:
        # Consumo base con variaciones aleatorias
        delta = random.uniform(-50.0, 80.0)
        value = self._base_load + delta
        # Picos ocasionales de consumo
        if random.random() < 0.08:
            value += random.uniform(200.0, 400.0)
        return round(max(100.0, min(value, 1000.0)), 1)


class HumiditySensor(SensorBase):
    """
    Sensor de humedad relativa.
    Rango normal: 40–80 %
    Zona WARNING:  80–90 %
    Zona CRITICAL: > 90 %
    """
    def __init__(self, sensor_id="hum_01"):
        super().__init__(sensor_id, "HUMIDITY", "PERCENT")
        self._level = random.uniform(50.0, 70.0)

    def generate_value(self) -> float:
        # Cambios lentos y graduales
        self._level += random.uniform(-3.0, 3.5)
        self._level   = max(30.0, min(self._level, 95.0))
        return round(self._level, 1)


class StatusSensor(SensorBase):
    """
    Sensor de estado operativo del equipo.
    1 = equipo encendido y funcionando (normal)
    0 = equipo apagado o en falla (CRITICAL)
    """
    def __init__(self, sensor_id="sta_01"):
        super().__init__(sensor_id, "STATUS", "BINARY")

    def generate_value(self) -> float:
        # 95% del tiempo el equipo está encendido
        return 0.0 if random.random() < 0.05 else 1.0
