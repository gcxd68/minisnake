import os
import time
import secrets
import sqlite3
import logging
import threading
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
    format='%(asctime)s [%(levelname)s] %(client_ip)s - %(message)s',
    handlers=[logging.StreamHandler()]
)

logger = logging.getLogger(__name__)
logger.addFilter(ContextFilter())

# --- Configuration & Game Rules ---
PORT = int(os.environ.get("PORT", 8000))
DB_PATH = 'scores.db'

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
MAX_SCORE = GAME_WIDTH * GAME_HEIGHT * POINTS_PER_FRUIT

# --- SECURITY LIMITS ---
MAX_ACTIVE_SESSIONS = 5000
MAX_REQ_PER_SEC = 20

# Server States (Thread-Safe Architecture)
active_sessions = {}
ip_data_map = {}

# We use two separate locks to prevent Deadlocks (mirroring the Go architecture)
session_lock = threading.Lock() # Protects active_sessions
ip_lock = threading.Lock()      # Protects ip_data_map

# --- Helper Functions ---
def lcg_rand(seed):
    """ Pseudo-random generator synchronized with the C client """
    return (seed * 1103515245 + 12345) & 0x7fffffff

def apply_penalties(session, new_steps):
    """ Applies score penalties if the player takes too many steps without eating. """
    if PENALTY_INTERVAL <= 0:
        return
        
    for step in range(session["last_steps"] + 1, new_steps + 1):
        if step % PENALTY_INTERVAL == 0:
            session["score"] = max(0, session["score"] - PENALTY_AMOUNT)

# --- Security Functions ---
@app.before_request
def rate_limit_middleware():
    """ Middleware to protect against DDoS and Siege (Rate Limit only) """
    ip = request.remote_addr
    now = time.time()
    
    with ip_lock:
        if ip not in ip_data_map:
            ip_data_map[ip] = {"req_count": 0, "window_start": now}
        
        data = ip_data_map[ip]
        
        # Enforce Rate Limit (20 req/sec)
        if now - data["window_start"] > 1.0:
            data["req_count"] = 0
            data["window_start"] = now
            
        data["req_count"] += 1
        if data["req_count"] > MAX_REQ_PER_SEC:
            logger.warning(f"[SECURITY] SPAM BLOCKED from IP {ip}")
            abort(429, description="Too Many Requests")

# --- Database Management ---
def init_db():
    """ Creates the local database file and table if they don't exist. """
    try:
        with sqlite3.connect(DB_PATH) as conn:
            conn.execute('''CREATE TABLE IF NOT EXISTS scores
                         (name TEXT, score INTEGER, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)''')
        logger.info("[Server] Database initialized successfully.")
    except Exception as e:
        logger.error(f"Database initialization failed: {e}")

init_db()

def cleanup_stale_data():
    """ Removes sessions inactive for > 15 minutes and clears old IP records. """
    now = time.time()
    
    # 1. Clean sessions safely
    with session_lock:
        stale_tokens = [t for t, d in active_sessions.items() if now - d["last_ping"] > 900]
        for t in stale_tokens:
            del active_sessions[t]
            
    # 2. Clean IP map safely (Prevent memory leaks from IP spoofing)
    with ip_lock:
        stale_ips = [ip for ip, d in ip_data_map.items() if now - d["window_start"] > 300]
        for ip in stale_ips:
            del ip_data_map[ip]

def background_cleanup_task():
    """ Background thread to handle memory cleanup without slowing down endpoints """
    while True:
        time.sleep(300) # Run every 5 minutes
        try:
            cleanup_stale_data()
        except Exception as e:
            logger.error(f"Background cleanup error: {e}")

# Launch the background cleanup task
cleanup_thread = threading.Thread(target=background_cleanup_task, daemon=True)
cleanup_thread.start()

# --- API Routes ---
@app.route('/rules', methods=['GET'])
def get_rules():
    """ Public endpoint to fetch game dimensions and rules. """
    rules = [GAME_WIDTH, GAME_HEIGHT, INITIAL_DELAY, SPEEDUP_FACTOR, POINTS_PER_FRUIT, 
             CHEAT_TIMEOUT, INITIAL_SIZE, PENALTY_INTERVAL, PENALTY_AMOUNT, SPAWN_FRUIT_MAX_ATTEMPTS]
    return "|".join(map(str, rules)), 200

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
    
    # Seed advancement logic (offset) required to maintain strict cryptographic sync with the C client
    current_seed = original_seed
    offset_x = 0
    if GAME_WIDTH % 2 == 0:
        current_seed = lcg_rand(current_seed)
        offset_x = current_seed % 2

    offset_y = 0
    if GAME_HEIGHT % 2 == 0:
        current_seed = lcg_rand(current_seed)
        offset_y = current_seed % 2

    with session_lock:
        active_sessions[token] = {
            "score": 0,
            "fruits_eaten": 0,
            "last_ping": time.time(),
            "cheated": False,
            "seed": current_seed,
            "head_x": (GAME_WIDTH // 2) - offset_x,
            "head_y": (GAME_HEIGHT // 2) - offset_y,
            "last_steps": 0
        }
    
    logger.info(f"SESSION START: Token {token[:8]}... | Seed: {original_seed}")
    return f"{token}|{original_seed}", 200

@app.route('/eat/<token>/<int:steps>/<int:fx>/<int:fy>', methods=['GET'])
def eat_fruit(token, steps, fx, fy):
    """ Validates a fruit consumption event and updates the session score securely. """
    ip = request.remote_addr
    now = time.time()
    
    # The entire validation process is locked to prevent Race Conditions 
    # from simultaneous requests arriving on the same token via Gunicorn threads.
    with session_lock:
        if token not in active_sessions or active_sessions[token]["cheated"]:
            abort(403, description="Unauthorized")
        
        session = active_sessions[token]
        delta_steps = steps - session["last_steps"]

        if delta_steps <= 0:
            session["cheated"] = True
            logger.warning(f"CHEAT (Steps): {token[:8]} | Steps did not increase ({steps})")
            abort(400, description="Invalid steps")

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
                session["seed"] = temp_seed # Sync seed
                break

        if not valid_fruit:
            session["cheated"] = True
            logger.warning(f"CHEAT (RNG): {token[:8]} | Client fruit at ({fx},{fy}) not in server sequence.")
            abort(400, description="Invalid fruit")

        # 2. Manhattan Distance
        manhattan = abs(fx - session["head_x"]) + abs(fy - session["head_y"])
        if delta_steps < manhattan:
            session["cheated"] = True
            logger.warning(f"CHEAT (Movement): {token[:8]} | {delta_steps} steps taken for dist {manhattan}")
            abort(400, description="Impossible move")

        # 3. Time-based Anti-Spam
        current_delay_sec = (INITIAL_DELAY / 1000000.0) * (SPEEDUP_FACTOR ** session["fruits_eaten"])
        # Hybrid tolerance: 25% margin against speedhacks + 0.5s fixed buffer against network jitter
        min_ping_interval = max(0, (current_delay_sec * delta_steps * 0.75) - 0.5)
        
        if now - session["last_ping"] < min_ping_interval:
            session["cheated"] = True
            logger.warning(f"CHEAT (Time): {token[:8]} | Ping too fast for {delta_steps} steps.")
            abort(400, description="Speedhack detected")

        # 4. Score & Penalty Calculation
        apply_penalties(session, steps)
        session["score"] += POINTS_PER_FRUIT
        session["fruits_eaten"] += 1

        if session["score"] >= MAX_SCORE:
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
    """ Endpoint for the client to report itself if local anti-cheat triggers. """
    ip = request.remote_addr
    with session_lock:
        if token in active_sessions:
            active_sessions[token]["cheated"] = True
            
    logger.warning(f"CHEAT (Local): {token[:8]} | TracerPid/Local anomaly detected. Session flagged.")
    return "Flagged", 200

@app.route('/submit/<token>/<name>/<int:steps>', methods=['GET'])
def submit_score(token, name, steps):
    """ Finalizes a session and saves the score to the database. """
    ip = request.remote_addr
    
    # Pop detaches the session from active_sessions immediately to prevent double submissions
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
        logger.error(f"BANNED SUBMISSION: Player '{name}' attempted to submit from flagged session. Forcing score to 0.")
        session["score"] = 0
    elif session["score"] > 0:
        apply_penalties(session, steps)

    final_score = session["score"]
    
    if final_score < 0 or final_score > MAX_SCORE:
        logger.warning(f"SUBMIT REJECTED: Impossible score {final_score} for '{name}'. Forcing score to 0.")
        final_score = 0

    try:
        if final_score > 0:
            with sqlite3.connect(DB_PATH) as conn:
                conn.execute("INSERT INTO scores (name, score) VALUES (?, ?)", (name, final_score))
            logger.info(f"SCORE SAVED: '{name}' | Score: {final_score} | Fruits: {session['fruits_eaten']}")
        else:
            logger.info(f"SCORE IGNORED (Zero or Cheated): '{name}' submission processed but not saved to DB.")
            
        return "OK", 200
    except Exception as e:
        logger.error(f"Database error: {e}")
        abort(500, description="Backend Error")

@app.route('/scores/<int:limit>', methods=['GET'])
def get_scores(limit):
    """ Retrieves the top scores from the database. """
    # Bound the limit to prevent OOM (Out Of Memory) or excessive database queries
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
    app.run(host='0.0.0.0', port=PORT)
