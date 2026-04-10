import os
import time
import secrets
import sqlite3
import logging
import threading
import json
from flask import Flask, request, abort
from dotenv import load_dotenv

# Load environment variables from the .env file
load_dotenv()

app = Flask(__name__)

# --- Logging Configuration ---
class ContextFilter(logging.Filter):
    """ Custom Filter to inject client IP into every log record automatically """
    def filter(self, record):
        try:
            record.client_ip = request.remote_addr
        except RuntimeError:
            record.client_ip = "Server"
        return True

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] [%(client_ip)s] %(message)s',
    handlers=[logging.StreamHandler()]
)

logger = logging.getLogger(__name__)
logger.addFilter(ContextFilter())

# --- Server & Security Constants ---
PORT = int(os.environ.get("PORT", 8000))
DB_PATH = 'scores.db'
MAX_ACTIVE_SESSIONS = 5000 # Anti-OOM: Global limit of concurrent players
MAX_REQ_PER_SEC = 20       # Anti-Spam: 20 requests per second max per IP

# --- Dynamic Game Rules ---
rules = {
    "GameWidth": 25,
    "GameHeight": 20,
    "InitialDelay": 250000.0,
    "SpeedupFactor": 0.985,
    "PointsPerFruit": 10,
    "CheatTimeout": 5000,
    "InitialSize": 1,
    "PenaltyInterval": 10,
    "PenaltyAmount": 1,
    "SpawnFruitMaxAttempts": 10000
}

# 1. Override with external JSON file if available
try:
    with open("rules.json", "r") as f:
        custom_rules = json.load(f)
        rules.update(custom_rules)
        logger.info("Custom game rules loaded from rules.json")
except FileNotFoundError:
    pass
except Exception as e:
    logger.warning(f"Error parsing rules.json, relying on defaults: {e}")

# 2. Dynamically calculate the maximum possible score (used internally for security)
MAX_SCORE = rules["GameWidth"] * rules["GameHeight"] * rules["PointsPerFruit"]

# 3. Log active configuration for observability
logger.info(f"Active Rules: {rules['GameWidth']}x{rules['GameHeight']} | Delay: {int(rules['InitialDelay'])} | Speedup: {rules['SpeedupFactor']} | PPF: {rules['PointsPerFruit']} | Size: {rules['InitialSize']} | Penalty: {rules['PenaltyAmount']} | Interval: {rules['PenaltyInterval']}")

# --- Server States (Thread-Safe Architecture) ---
active_sessions = {}
ip_data_map = {}

# We use two separate locks to prevent Deadlocks
session_lock = threading.Lock() # Protects active_sessions
ip_lock = threading.Lock()      # Protects ip_data_map

# --- Helper Functions ---
def lcg_rand(seed):
    """ Pseudo-random generator synchronized with the C client """
    return (seed * 1103515245 + 12345) & 0x7fffffff

def apply_penalties(session, new_steps):
    """ Deducts points if the player takes too many steps without eating. """
    if rules["PenaltyInterval"] <= 0:
        return
        
    for step in range(session["last_steps"] + 1, new_steps + 1):
        if step % rules["PenaltyInterval"] == 0:
            session["score"] = max(0, session["score"] - rules["PenaltyAmount"])

# --- Middleware & Security ---
@app.before_request
def rate_limit_middleware():
    """ Middleware to protect against DDoS and Siege (Rate Limit only) """
    ip = request.remote_addr
    now = time.time()
    
    with ip_lock:
        if ip not in ip_data_map:
            ip_data_map[ip] = {"req_count": 0, "window_start": now}
        
        data = ip_data_map[ip]
        
        if now - data["window_start"] > 1.0:
            data["req_count"] = 0
            data["window_start"] = now
            
        data["req_count"] += 1
        if data["req_count"] > MAX_REQ_PER_SEC:
            logger.warning(f"[SECURITY] SPAM BLOCKED from IP {ip}")
            abort(429, description="Too Many Requests")

# --- Background Tasks & DB ---
def init_db():
    """ Creates the local database file and table if they don't exist. """
    try:
        with sqlite3.connect(DB_PATH) as conn:
            conn.execute('''CREATE TABLE IF NOT EXISTS scores
                         (name TEXT, score INTEGER, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)''')
        logger.info("Database initialized successfully.")
    except Exception as e:
        logger.error(f"Database initialization failed: {e}")

init_db()

def cleanup_stale_data():
    """ Removes sessions inactive for > 15 minutes and clears old IP records. """
    now = time.time()
    
    with session_lock:
        stale_tokens = [t for t, d in active_sessions.items() if now - d["last_ping"] > 900]
        # Keep track of the first 8 characters for the log
        removed_tokens_short = [t[:8] for t in stale_tokens]
        for t in stale_tokens:
            del active_sessions[t]
            
    with ip_lock:
        stale_ips = [ip for ip, d in ip_data_map.items() if now - d["window_start"] > 300]
        for ip in stale_ips:
            del ip_data_map[ip]
        removed_ips = len(stale_ips)

    # Only log if something was actually cleaned up
    if len(stale_tokens) > 0 or removed_ips > 0:
        tokens_str = f" ({', '.join(removed_tokens_short)})" if removed_tokens_short else ""
        logger.info(f"[CLEANUP] Swept {len(stale_tokens)} ghost sessions{tokens_str} and {removed_ips} stale IP records.")

def background_cleanup_task():
    """ Background thread to handle memory cleanup without slowing down endpoints """
    while True:
        time.sleep(300) # Run every 5 minutes
        try:
            cleanup_stale_data()
        except Exception as e:
            logger.error(f"Background cleanup error: {e}")

cleanup_thread = threading.Thread(target=background_cleanup_task, daemon=True)
cleanup_thread.start()

# --- API Routes ---
@app.route('/rules', methods=['GET'])
def get_rules():
    """ Public endpoint to fetch game dimensions and rules. """
    rule_values = [
        rules["GameWidth"], rules["GameHeight"], int(rules["InitialDelay"]), 
        rules["SpeedupFactor"], rules["PointsPerFruit"], rules["CheatTimeout"], 
        rules["InitialSize"], rules["PenaltyInterval"], rules["PenaltyAmount"], 
        rules["SpawnFruitMaxAttempts"]
    ]
    return "|".join(map(str, rule_values)), 200

@app.route('/token', methods=['GET'])
def get_token():
    """ Spawns a new session with synchronized seed and appropriate offset start position. """
    ip = request.remote_addr
    
    with session_lock:
        if len(active_sessions) >= MAX_ACTIVE_SESSIONS:
            logger.error(f"[SECURITY] OOM LIMIT REACHED. Refused connection for {ip}")
            abort(503, description="Server full")

    token = secrets.token_hex(16)
    original_seed = secrets.randbits(31) 
    
    current_seed = original_seed
    offset_x = 0
    if rules["GameWidth"] % 2 == 0:
        current_seed = lcg_rand(current_seed)
        offset_x = current_seed % 2

    offset_y = 0
    if rules["GameHeight"] % 2 == 0:
        current_seed = lcg_rand(current_seed)
        offset_y = current_seed % 2

    with session_lock:
        active_sessions[token] = {
            "score": 0,
            "fruits_eaten": 0,
            "last_ping": time.time(),
            "cheated": False,
            "seed": current_seed,
            "head_x": (rules["GameWidth"] // 2) - offset_x,
            "head_y": (rules["GameHeight"] // 2) - offset_y,
            "last_steps": 0
        }
    
    logger.info(f"SESSION START: Token {token[:8]}... | Seed: {original_seed}")
    return f"{token}|{original_seed}", 200

@app.route('/eat/<token>/<int:steps>/<int:fx>/<int:fy>', methods=['GET'])
def eat_fruit(token, steps, fx, fy):
    """ Validates a fruit consumption event and updates the session score securely. """
    ip = request.remote_addr
    now = time.time()
    
    with session_lock:
        if token not in active_sessions or active_sessions[token]["cheated"]:
            abort(403, description="Unauthorized")
        
        session = active_sessions[token]
        delta_steps = steps - session["last_steps"]

        if delta_steps <= 0:
            session["cheated"] = True
            logger.warning(f"CHEAT (Steps): {token[:8]} | Steps did not increase ({steps})")
            abort(400, description="Invalid steps")

        # 1. RNG Validation
        valid_fruit = False
        temp_seed = session["seed"]
        for _ in range(rules["SpawnFruitMaxAttempts"]):
            temp_seed = lcg_rand(temp_seed)
            cand_x = (temp_seed >> 16) % rules["GameWidth"]
            temp_seed = lcg_rand(temp_seed)
            cand_y = (temp_seed >> 16) % rules["GameHeight"]
            
            if cand_x == fx and cand_y == fy:
                valid_fruit = True
                session["seed"] = temp_seed
                break

        if not valid_fruit:
            session["cheated"] = True
            logger.warning(f"CHEAT (RNG): {token[:8]} | Client fruit at ({fx},{fy}) not in server sequence.")
            abort(400, description="Invalid fruit")

        # 2. Physical Validation (Manhattan Distance)
        manhattan = abs(fx - session["head_x"]) + abs(fy - session["head_y"])
        if delta_steps < manhattan:
            session["cheated"] = True
            logger.warning(f"CHEAT (Movement): {token[:8]} | {delta_steps} steps taken for dist {manhattan}")
            abort(400, description="Impossible move")

        # 3. The Time Corridor (Anti-Speedhack & Anti-Pause)
        current_delay_sec = (rules["InitialDelay"] / 1000000.0) * (rules["SpeedupFactor"] ** session["fruits_eaten"])
        expected_time = current_delay_sec * delta_steps
        
        actual_time = now - session["last_ping"]
        min_time = max(0, (expected_time * 0.75) - 0.5)               # Floor (Speedhack)
        max_time = expected_time + (rules["CheatTimeout"] / 1000.0) + 5.0 # Ceiling (LLDB/Pause)
        
        if actual_time < min_time:
            session["cheated"] = True
            logger.warning(f"CHEAT (Time): {token[:8]} | Speedhack. Expected ~{expected_time:.1f}s, took {actual_time:.1f}s")
            abort(400, description="Speedhack detected")
            
        if actual_time > max_time:
            session["cheated"] = True
            logger.warning(f"CHEAT (Timeout): {token[:8]} | Paused/Lag. Expected ~{expected_time:.1f}s, took {actual_time:.1f}s")
            abort(400, description="Timeout detected")

        # 4. Update Score & Penalties
        apply_penalties(session, steps)
        session["score"] += rules["PointsPerFruit"]
        session["fruits_eaten"] += 1

        # Check against dynamic MaxScore (strictly greater than)
        if session["score"] > MAX_SCORE:
            session["cheated"] = True
            logger.warning(f"CHEAT (Limit): {token[:8]} | Score {session['score']} exceeded capacity.")
            abort(400, description="Score limit exceeded")

        # 5. Update State
        session["last_ping"] = now
        session["head_x"] = fx
        session["head_y"] = fy
        session["last_steps"] = steps
    
    return "Yum", 200

@app.route('/cheat/<token>', methods=['GET'])
def flag_cheat(token):
    """ Endpoint for the client to report itself if local anomaly triggers. """
    ip = request.remote_addr
    with session_lock:
        if token in active_sessions:
            active_sessions[token]["cheated"] = True
            
    logger.warning(f"CHEAT (Local): {token[:8]} | TracerPid/Local anomaly reported by client.")
    return "Flagged", 200

@app.route('/submit/<token>/<name>/<int:steps>', methods=['GET'])
def submit_score(token, name, steps):
    """ Finalizes a session and saves the score to the database. """
    ip = request.remote_addr
    
    with session_lock:
        session = active_sessions.pop(token, None)

    if not session:
        logger.warning(f"SUBMIT REJECTED: Expired or invalid token {token[:8]}")
        return "OK", 200
    
    # Strict XSS prevention
    if not name.isalnum() or len(name) > 8:
        logger.warning(f"SUBMIT REJECTED: Invalid name '{name}' from {token[:8]}")
        return "OK", 200

    if session["cheated"]:
        logger.error(f"BANNED SUBMISSION: Player '{name}' attempted to submit from flagged session {token[:8]}.")
        session["score"] = 0
    elif session["score"] > 0:
        apply_penalties(session, steps)

    final_score = session["score"]
    
    if final_score < 0 or final_score > MAX_SCORE:
        logger.warning(f"SUBMIT REJECTED: Impossible score {final_score} for '{name}' (Session: {token[:8]}).")
        final_score = 0

    try:
        if final_score > 0:
            with sqlite3.connect(DB_PATH) as conn:
                conn.execute("INSERT INTO scores (name, score) VALUES (?, ?)", (name, final_score))
            logger.info(f"SCORE SAVED: {token[:8]} | Player: '{name}' | Score: {final_score} | Fruits: {session['fruits_eaten']}")
        else:
            logger.info(f"SCORE IGNORED: {token[:8]} | Player: '{name}' processed but not saved (Zero or Cheated).")
            
        return "OK", 200
    except Exception as e:
        logger.error(f"Database error: {e}")
        abort(500, description="Backend Error")

@app.route('/scores/<int:limit>', methods=['GET'])
def get_scores(limit):
    """ Retrieves the top scores from the database. """
    limit = max(1, min(limit, 100))

    try:
        with sqlite3.connect(DB_PATH) as conn:
            cursor = conn.execute("SELECT name, score FROM scores ORDER BY score DESC, timestamp ASC LIMIT ?", (limit,))
            rows = cursor.fetchall()
            
        return "".join([f"{r[0]}|{r[1]}|0|0\n" for r in rows]), 200
    except Exception as e:
        logger.error(f"Database error: {e}")
        abort(500, description="Backend Error")

if __name__ == '__main__':
    logger.info(f"Starting secure backend on 0.0.0.0:{PORT}")
    app.run(host='0.0.0.0', port=PORT)
