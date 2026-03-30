import os
import requests
from flask import Flask
from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

app = Flask(__name__)

# --- CONFIGURATION ---
# These must match your local 'keys' file and Dreamlo dashboard
VPS_PRIVATE_KEY = os.environ.get("PRIVATE_KEY")
VPS_PUBLIC_KEY  = os.environ.get("PUBLIC_KEY")
DREAMLO_PRIVATE_KEY = os.environ.get("DREAMLO_PRIVATE_KEY")
DREAMLO_PUBLIC_KEY  = os.environ.get("DREAMLO_PUBLIC_KEY")

# Anti-cheat: Theoretical max score for a 25x20 grid (500 cells * 10 pts)
MAX_POSSIBLE_SCORE = 5000

@app.route('/lb/<key>/add/<name>/<int:score>', methods=['GET'])
def add_score(key, name, score):
    # 1. Authenticate the Game Client
    if key != VPS_PRIVATE_KEY:
        print(f"SECURITY ALERT: Unauthorized access attempt by '{name}'")
        return "Unauthorized", 403

    # 2. Anti-Cheat Validation
    # Checks if the score is physically possible within the game rules
    if score > MAX_POSSIBLE_SCORE:
        print(f"CHEAT DETECTED: {name} tried to submit {score} pts (Max allowed: {MAX_POSSIBLE_SCORE})")
        return "Invalid Score", 400

    # 3. Forward to Dreamlo
    print(f"Valid score received: {name} -> {score}")
    dreamlo_url = f"http://dreamlo.com/lb/{DREAMLO_PRIVATE_KEY}/add/{name}/{score}"
    
    try:
        response = requests.get(dreamlo_url, timeout=5)
        return response.text
    except Exception as e:
        print(f"Error communicating with Dreamlo: {e}")
        return "Backend Error", 500

@app.route('/lb/<key>/pipe/<int:limit>', methods=['GET'])
def get_scores(key, limit):
    # Authenticate the Game Client for leaderboard retrieval
    if key != VPS_PUBLIC_KEY:
        return "Unauthorized", 403

    dreamlo_url = f"http://dreamlo.com/lb/{DREAMLO_PUBLIC_KEY}/pipe/{limit}"
    
    try:
        response = requests.get(dreamlo_url, timeout=5)
        return response.text
    except Exception as e:
        return "Backend Error", 500

if __name__ == '__main__':
    # Run on all interfaces (0.0.0.0) to be accessible from the web
    app.run(host='0.0.0.0', port=80)
