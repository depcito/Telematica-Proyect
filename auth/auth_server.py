"""
auth_server.py — Servicio externo de autenticación.

Rutas:
    POST /login      → recibe {username, password}, devuelve token
    GET  /validate   → recibe ?token=xxx, devuelve si el token es válido

Uso:
    pip install flask
    python auth_server.py

    # En producción (dentro del contenedor):
    python auth_server.py --host 0.0.0.0 --port 5001
"""

import json
import time
import hashlib
import secrets
import argparse
from flask import Flask, request, jsonify

app = Flask(__name__)
from flask_cors import CORS
CORS(app)

# ── Tokens activos en memoria: {token: {username, role, expires}} ────────────
active_tokens: dict = {}
TOKEN_TTL = 3600 * 8   # 8 horas


# ── Cargar usuarios desde archivo ────────────────────────────────────────────
def load_users(filepath="users.json") -> dict:
    try:
        with open(filepath) as f:
            users_list = json.load(f)
        return {u["username"]: u for u in users_list}
    except FileNotFoundError:
        print(f"[AUTH] Archivo {filepath} no encontrado. Usando usuarios por defecto.")
        return {
            "admin":    {"username": "admin",    "password": "admin123",  "role": "operator"},
            "jperez":   {"username": "jperez",   "password": "pass1234",  "role": "operator"},
            "mgarcia":  {"username": "mgarcia",  "password": "securepass","role": "operator"},
        }


USERS = load_users()


def hash_password(password: str) -> str:
    return hashlib.sha256(password.encode()).hexdigest()


def generate_token() -> str:
    return secrets.token_hex(32)


def cleanup_expired_tokens():
    now = time.time()
    expired = [t for t, d in active_tokens.items() if d["expires"] < now]
    for t in expired:
        del active_tokens[t]


# ── Rutas ─────────────────────────────────────────────────────────────────────

@app.route("/login", methods=["POST"])
def login():
    """
    Body JSON: {"username": "jperez", "password": "pass1234"}
    Respuesta OK:    {"token": "abc...", "role": "operator", "username": "jperez"}
    Respuesta error: {"error": "Invalid credentials"}
    """
    data = request.get_json(silent=True)
    if not data:
        return jsonify({"error": "Body JSON requerido"}), 400

    username = data.get("username", "").strip()
    password = data.get("password", "").strip()

    if not username or not password:
        return jsonify({"error": "username y password requeridos"}), 400

    user = USERS.get(username)
    if not user:
        return jsonify({"error": "Invalid credentials"}), 401

    # Comparar contraseña (en producción usar hash)
    if user["password"] != password:
        return jsonify({"error": "Invalid credentials"}), 401

    cleanup_expired_tokens()

    token = generate_token()
    active_tokens[token] = {
        "username": username,
        "role":     user["role"],
        "expires":  time.time() + TOKEN_TTL,
    }

    print(f"[AUTH] Login exitoso: {username} (rol: {user['role']})")
    return jsonify({
        "token":    token,
        "username": username,
        "role":     user["role"],
    }), 200


@app.route("/validate", methods=["GET"])
def validate():
    """
    Query param: ?token=abc...
    Respuesta OK:    {"valid": true, "username": "jperez", "role": "operator"}
    Respuesta error: {"valid": false}
    """
    token = request.args.get("token", "").strip()
    if not token:
        return jsonify({"valid": False, "error": "token requerido"}), 400

    cleanup_expired_tokens()

    entry = active_tokens.get(token)
    if not entry:
        return jsonify({"valid": False}), 401

    if entry["expires"] < time.time():
        del active_tokens[token]
        return jsonify({"valid": False, "error": "token expirado"}), 401

    return jsonify({
        "valid":    True,
        "username": entry["username"],
        "role":     entry["role"],
    }), 200


@app.route("/health", methods=["GET"])
def health():
    """Endpoint de salud para verificar que el servicio está activo."""
    return jsonify({"status": "ok", "active_tokens": len(active_tokens)}), 200


# ── Punto de entrada ──────────────────────────────────────────────────────────
if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5001)
    args = parser.parse_args()

    print(f"[AUTH] Servicio de autenticación en {args.host}:{args.port}")
    print(f"[AUTH] Usuarios cargados: {list(USERS.keys())}")
    app.run(host=args.host, port=args.port, debug=False)
