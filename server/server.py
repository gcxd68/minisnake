import os
import time
import secrets
import sqlite3
import logging
from flask import Flask, request
from dotenv import load_dotenv

# Load environment variables from the .env file
load_dotenv()

app = Flask(__name__)

# --- Logging Configuration ---
# Custom Filter to inject client IP into every log record automatically
class ContextFilter(logging.Filter):
    def filter(self, record):
        # request.remote_addr captures the client's IP
        # We handle cases outside of request context (like startup) by defaulting to 'Server'
        try:
            record.client_ip = request.remote_addr
        except RuntimeError:
            record.client_ip = "Server"
        return True

# Logs are formatted with timestamps, severity levels, and client IP
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(client_ip)s - %(message)s',
    handlers=[logging.StreamHandler()]
)

logger = logging.getLogger(__name__)
logger.addFilter(ContextFilter())

# Look for PORT instead of VPS_PORT
PORT = int(os.environ.get("PORT", 8000))

# Server dictated game rules
GAME_WIDTH = 25
GAME_HEIGHT = 20
INITIAL_DELAY = 250000
SPEEDUP_FACTOR = 0.985
POINTS_PER_FRUIT = 10
CHEAT_TIMEOUT = 5000
INITIAL_SIZE = 1
PENALTY_INTERVAL = 10
PENALTY_AMOUNT = 1
SPAWN_FRUIT_MAX_ATTEMPTS = 10000
MIN_PING_INTERVAL = 0.05
MAX_SCORE = GAME_WIDTH * GAME_HEIGHT * POINTS_PER_FRUIT

# Pseudo-random generator synchronized with the C client (LCG algorithm)
def lcg_rand(seed):
    return (seed * 1103515245 + 12345) & 0x7fffffff

# --- SQLite Database Initialization ---
def init_db():
    """ Creates the local database file and table if they don't exist. """
    try:
        conn = sqlite3.connect('scores.db')
        c = conn.cursor()
        c.execute('''CREATE TABLE IF NOT EXISTS scores
                     (name TEXT, score INTEGER, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)''')
        conn.commit()
        conn.close()
        logger.info("Database initialized successfully.")
    except Exception as e:
        logger.error(f"Database initialization failed: {e}")

init_db()

# Dictionary to store active game sessions
active_sessions = {}

def cleanup_stale_sessions():
    """ Removes sessions inactive for > 15 minutes. """
    now = time.time()
    stale_tokens = [t for t, d in active_sessions.items() if now - d["last_ping"] > 900]
    for t in stale_tokens:
        del active_sessions[t]
    if stale_tokens:
        logger.info(f"CLEANUP: Removed {len(stale_tokens)} abandoned sessions.")

@app.route('/rules', methods=['GET'])
def get_rules():
    """ Public endpoint to fetch game dimensions and rules. """
    return f"{GAME_WIDTH}|{GAME_HEIGHT}|{INITIAL_DELAY}|{SPEEDUP_FACTOR}|{POINTS_PER_FRUIT}|{CHEAT_TIMEOUT}|{INITIAL_SIZE}|{PENALTY_INTERVAL}|{PENALTY_AMOUNT}|{SPAWN_FRUIT_MAX_ATTEMPTS}", 200

@app.route('/token', methods=['GET'])
def get_token():
    """ Spawns a new session with synchronized seed. """
    cleanup_stale_sessions()
    token = secrets.token_hex(16)
    original_seed = secrets.randbits(31) 
    
    # Simulate the initial C client offset logic to keep seeds synced
    current_seed = original_seed
    
    offset_x = 0
    if GAME_WIDTH % 2 == 0:
        current_seed = lcg_rand(current_seed)
        offset_x = current_seed % 2

    offset_y = 0
    if GAME_HEIGHT % 2 == 0:
        current_seed = lcg_rand(current_seed)
        offset_y = current_seed % 2

    active_sessions[token] = {
        "score": 0,
        "fruits_eaten": 0,
        "last_ping": time.time(),
        "cheated": False,
        "seed": current_seed,
        "last_x": (GAME_WIDTH // 2) - offset_x,
        "last_y": (GAME_HEIGHT // 2) - offset_y,
        "last_steps": 0
    }
    
    logger.info(f"SESSION START: Token {token[:8]}... | Seed: {original_seed}")
    return f"{token}|{original_seed}", 200

@app.route('/eat/<token>/<int:steps>/<int:fx>/<int:fy>', methods=['GET'])
def eat_fruit(token, steps, fx, fy):
    if token not in active_sessions or active_sessions[token]["cheated"]:
        return "Unauthorized", 403
    
    now = time.time()
    session = active_sessions[token]
    delta_steps = steps - session["last_steps"]

    if delta_steps <= 0:
        session["cheated"] = True
        logger.warning(f"CHEAT (Steps): {token[:8]} | Steps did not increase ({steps})")
        return "Invalid steps", 400

    # 1. Fruit Validation
    valid_fruit = False
    temp_seed = session["seed"]
    for _ in range(SPAWN_FRUIT_MAX_ATTEMPTS):
        temp_seed = lcg_rand(temp_seed)
        cand_x = (temp_seed >> 16) % GAME_WIDTH
        temp_seed = lcg_rand(temp_seed)
        cand_y = (temp_seed >> 16) % GAME_HEIGHT
        if cand_x == fx and cand_y == fy:
            valid_fruit = True
            session["seed"] = temp_seed
            break

    if not valid_fruit:
        session["cheated"] = True
        logger.warning(f"CHEAT (RNG): {token[:8]} | Client fruit at ({fx},{fy}) not in server sequence.")
        return "Invalid fruit", 400

    # 2. Manhattan Distance (Speedhack/Teleport)
    manhattan = abs(fx - session["last_x"]) + abs(fy - session["last_y"])
    if delta_steps < manhattan:
        session["cheated"] = True
        logger.warning(f"CHEAT (Movement): {token[:8]} | {delta_steps} steps taken for dist {manhattan}")
        return "Impossible move", 400

    # 3. Time-based Anti-Spam
    current_delay_sec = (INITIAL_DELAY / 1000000.0) * (SPEEDUP_FACTOR ** session["fruits_eaten"])
    min_ping_interval = current_delay_sec * delta_steps * 0.8 # 20% network margin
    
    if now - session["last_ping"] < min_ping_interval:
        session["cheated"] = True
        logger.warning(f"CHEAT (Time): {token[:8]} | Ping too fast for {delta_steps} steps.")
        return "Speedhack detected", 400

    # 4. Score Calculation
    for step in range(session["last_steps"] + 1, steps + 1):
        if PENALTY_INTERVAL > 0 and step % PENALTY_INTERVAL == 0:
            session["score"] = max(0, session["score"] - PENALTY_AMOUNT)

    session["score"] += POINTS_PER_FRUIT
    session["fruits_eaten"] += 1

    if session["score"] >= MAX_SCORE:
        session["cheated"] = True
        logger.warning(f"CHEAT (Limit): {token[:8]} | Score {session['score']} exceeded board capacity.")
        return "Score limit exceeded", 400

    session["last_ping"], session["last_x"], session["last_y"], session["last_steps"] = now, fx, fy, steps
    return "Yum", 200

@app.route('/cheat/<token>', methods=['GET'])
def flag_cheat(token):
    if token in active_sessions:
        active_sessions[token]["cheated"] = True
        logger.warning(f"CHEAT FLAG: Local detection triggered for {token[:8]}")
    return "Flagged", 200

@app.route('/submit/<token>/<name>/<int:steps>', methods=['GET'])
def submit_score(token, name, steps):
    if token not in active_sessions:
        return "Unauthorized", 403
    
    if not name.isalnum() or len(name) > 8:
        logger.warning(f"SUBMIT REJECTED: Invalid name '{name}' from {token[:8]}")
        return "Invalid Name", 400

    session = active_sessions.pop(token)
    
    if session["cheated"]:
        logger.error(f"BANNED SUBMISSION: Player '{name}' attempted to submit from flagged session.")
        return "Banned", 403

    # Final degradation check
    for step in range(session["last_steps"] + 1, steps + 1):
        if PENALTY_INTERVAL > 0 and step % PENALTY_INTERVAL == 0:
            session["score"] = max(0, session["score"] - PENALTY_AMOUNT)

    final_score = session["score"]
    if final_score <= 0 or final_score > MAX_SCORE:
        logger.warning(f"SUBMIT REJECTED: Invalid score {final_score} for '{name}'")
        return "Invalid Score", 400

    try:
        conn = sqlite3.connect('scores.db')
        c = conn.cursor()
        c.execute("INSERT INTO scores (name, score) VALUES (?, ?)", (name, final_score))
        conn.commit()
        conn.close()
        logger.info(f"SCORE SAVED: '{name}' | Score: {final_score} | Fruits: {session['fruits_eaten']}")
        return "OK", 200
    except Exception as e:
        logger.error(f"Database error: {e}")
        return "Backend Error", 500

@app.route('/scores/<int:limit>', methods=['GET'])
def get_scores(limit):
    try:
        conn = sqlite3.connect('scores.db')
        c = conn.cursor()
        c.execute("SELECT name, score FROM scores ORDER BY score DESC, timestamp ASC LIMIT ?", (limit,))
        rows = c.fetchall()
        conn.close()
        return "".join([f"{r[0]}|{r[1]}|0|0\n" for r in rows]), 200
    except Exception as e:
        logger.error(f"Database error: {e}")
        return "Backend Error", 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=PORT)
