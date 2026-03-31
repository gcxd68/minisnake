import os
import time
from flask import Flask
import requests
from dotenv import load_dotenv

load_dotenv()

app = Flask(__name__)

VPS_PORT = int(os.environ.get("VPS_PORT", 80))
VPS_PRIVATE_KEY = os.environ.get("VPS_PRIVATE_KEY")
VPS_PUBLIC_KEY  = os.environ.get("VPS_PUBLIC_KEY")
DREAMLO_PRIVATE_KEY = os.environ.get("DREAMLO_PRIVATE_KEY")
DREAMLO_PUBLIC_KEY  = os.environ.get("DREAMLO_PUBLIC_KEY")

seen_requests = {}  # signature -> timestamp

def check_signature(sig, name, score, ts):
    now = int(time.time())
    
    # 1. Interdit si le timestamp est vieux de plus de 5 minutes (ou dans le futur)
    if abs(now - ts) > 300:
        return False
        
    # 2. Interdit si on a déjà traité cette signature exacte (Replay pur)
    if sig in seen_requests:
        return False
        
    # 3. Nettoie la mémoire serveur des vieilles signatures
    expired = [k for k, v in seen_requests.items() if (now - v) > 300]
    for k in expired:
        del seen_requests[k]
        
    # 4. Vérification de l'empreinte djb2 (reproduit la moulinette du client C)
    text = f"{VPS_PRIVATE_KEY}{name}{score}{ts}"
    h = 5381
    for c in text:
        h = (((h << 5) + h) + ord(c)) & 0xFFFFFFFF
        
    expected_sig = f"{h:08x}"
    return sig == expected_sig

@app.route('/lb/<sig>/add/<name>/<int:score>/<int:ts>', methods=['GET'])
def add_score(sig, name, score, ts):
    if not check_signature(sig, name, score, ts):
        print(f"ALERTE SÉCURITÉ : Tentative de triche/replay bloquée pour '{name}'")
        return "Unauthorized", 403

    # On enregistre qu'on a vu cette requête pour empêcher le Replay
    seen_requests[sig] = ts
    print(f"Score valide (djb2 hash reçu). Transfert à Dreamlo : {name} -> {score}")
    
    dreamlo_url = f"http://dreamlo.com/lb/{DREAMLO_PRIVATE_KEY}/add/{name}/{score}"
    try:
        response = requests.get(dreamlo_url, timeout=5)
        return response.text
    except Exception as e:
        print(f"Erreur de communication avec Dreamlo : {e}")
        return "Backend Error", 500

@app.route('/lb/<key>/pipe/<int:limit>', methods=['GET'])
def get_scores(key, limit):
    if key != VPS_PUBLIC_KEY:
        return "Unauthorized", 403

    dreamlo_url = f"http://dreamlo.com/lb/{DREAMLO_PUBLIC_KEY}/pipe/{limit}"
    try:
        response = requests.get(dreamlo_url, timeout=5)
        return response.text
    except Exception as e:
        return "Backend Error", 500

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=VPS_PORT)
