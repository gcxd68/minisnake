# minisnake 🐍

A lightweight, terminal-based Snake game written in C with a dedicated online scoring backend server.

## Features

- 🎮 **Classic snake gameplay** directly in your terminal
- 🌐 **Online leaderboard support** via a robust backend validation server
- 🎨 **Clean aesthetics** featuring Unicode character graphics and a smooth linear-interpolation splash screen
- ⚡ **Progressive difficulty** as the game speeds up dynamically
- ⚙️ **Dynamic server rules configuration** without recompiling the backend
- 🛡️ **Advanced anti-cheat protection** via Server-Authoritative physics simulation (Spatial Hashing), Opaque Server-Side RNG, behavioral variance telemetry, and thread-safe background sync

## Requirements

### Client
- `make` and `gcc` (or any compatible C compiler)
- POSIX-compliant system with `pthread` support
- Terminal with Unicode support (GNOME Terminal recommended)

### Server (Online Mode)
- **Go Backend:** Go 1.22+ (Required for advanced HTTP routing)

## Usage

### Offline Mode
Play locally without leaderboard features. Compiling and running without arguments will open a default 25x20 board.

```bash
make
./minisnake
```

You can also specify custom dimensions (Constraints: Width 2-200, Height 2-50):
```bash
./minisnake 40 20
```

### Online Mode
To compete on the leaderboard, a backend validation server must be accessible. Play sessions are locked to the server's actively configured board dimensions to ensure fairness.

1. Ensure the backend server is running (see [Server Setup](#server-setup)).
2. Create a `net` file in the repository root (use `net.example` as a template):
   ```text
   HOST=YOUR_SERVER_IP
   PORT=YOUR_SERVER_PORT
   ```
3. Compile and play:
   ```bash
   make
   ./minisnake online
   ```
After the game, you will be prompted to enter your name for the global leaderboard!

## Controls & Gameplay

| Key | Action |
|-----|--------|
| `W` / `↑` | Move Up |
| `A` / `←` | Move Left |
| `S` / `↓` | Move Down |
| `D` / `→` | Move Right |
| `X` | Quit |

**Rules:**
1. Guide the snake to eat the fruit (`@`) to earn points.
2. Each fruit extends your snake and speeds up the game!
3. Avoid hitting the boundary walls or yourself to survive.
4. Win by filling the entire game board! (You win!)

---

## Server Setup

The online leaderboard functions via a secure Server-Side Scoring Architecture proxy. The C client never submits its score directly; instead, it sends an asynchronous HTTP ping per fruit. The Go backend securely enforces several critical validations:

- **Time Corridor & Anti-Lag Smoothing:** A dynamic time window is calculated based on game speed and path length. It uses a sliding window (smoothing algorithm) to absorb network jitter safely, differentiating between genuine network lag spikes and speedhacks/pauses, while accommodating natural idle periods like the starting splash screen.
- **Maximum Score Authorization:** The server strictly validates that no submitted score exceeds the theoretical maximum capacity (`Width * Height * PointsPerFruit`). 
- **Client Version Integrity:** Client requests are tagged with a version header. Outdated clients are gracefully blocked with an update notice, either at launch (prompting a fallback to Offline Mode) or hijacked directly at the leaderboard screen.
- **Active Turing Tests:** The server analyzes behavioral patterns (Manhattan distance efficiency and detour variance) specifically to detect programmatic bots playing with inhuman consistency.
- **DoS Protection & Payload Limits:** Strict memory barriers (`MaxBytesReader`) prevent memory exhaustion attacks from oversized JSON payloads.
- **Improved Audit Logs:** Clearer log output combining IP and short-token prefixes for fast debug reading while preserving player privacy mechanisms.
- **Authoritative Physics Simulation:** The server accurately recreates the game state on every ping by replaying every player move via an O(1) Spatial Hashing grid and a highly optimized O(1) Ring Buffer for full body tracking. It instantly detects and shadowbans any wall-phasing or self-colliding cheats without O(N) CPU penalties.
- **Active Memory Management:** The server automatically tracks and sweeps stale IPs and abandoned "ghost" sessions, gracefully logging memory reclamation without spamming stdout.
- **Pathing and Behavioral Telemetry:** The server strictly generates the target fruit (Opaque RNG) rather than relying on a shared PRNG, fundamentally blocking teleporting or PRNG reversal. It also tracks the statistical variance of the player's detours (Shadow Mode telemetry) to flag bots moving with unnatural perfection, and runs a strict integral calculation upon score submission to guarantee the total game time rigorously respects the theoretical minimum limits of a dynamically accelerating game.
- **Anti-Lag Fruit Spawning:** To elegantly mask network latency, the server intelligently enforces a minimum Manhattan distance between the snake's head and the newly spawned fruit. This constraint gracefully degrades on highly crowded boards to guarantee a valid spawn without late-game deadlocks.

### Server Limits & Anti-DDoS
The Go backend is hardcoded to manage memory and traffic securely:
- **Maximum Concurrent Sessions:** 5000 players globally.
- **Rate Limiting:** 20 requests per second per IP strictly enforced.
- **Ghost Sessions:** Automatically swept after 15 minutes of inactivity.

### 1. Running the Go Backend

The backend is built in Go. It is high-performance and compiled as a single static binary.

1. Make sure Go is installed on your machine.
2. Build the server binary by running `make server` from the project root.
3. Start the server from the root directory: `./bin/server`.

### 2. Dynamic Server Configuration
You can now dynamically adjust the game constraints (points, speed, grid boundaries) directly from the server without having to rebuild the proxy.

1. Rename `server/rules.json.example` to `rules.json` and place it in the same directory where your active backend server is executed.
*Note: The Go backend intrinsically enforces valid JSON structures via Go structs.*

2. Override any desired rules:
```json
{
  "GameWidth": 25,
  "GameHeight": 20,
  "InitialDelay": 250000.0,
  "SpeedupFactor": 0.985,
  "PointsPerFruit": 10,
  "PenaltyInterval": 10,
  "PenaltyAmount": 1,
  "CheatTimeout": 5000,
  "SpawnFruitMaxAttempts": 10000,
  "MinFruitDist": 2
}
```
*Note: Any omitted keys will seamlessly fall back to the native server defaults. The server securely computes score caps based on these parsed values dynamically to prevent abuse.*

---

## Technical Configuration (Client)

You can customize graphical settings and input buffers purely for the client by adapting constants directly inside `include/minisnake.h` before compiling (`make re`).

- **Controls & Strings:** `MOVE_KEYS`, `EXIT_KEY`, `TERM_TITLE`, `MSG_LOSS`, `MSG_WIN`
- **Aesthetics Elements:** `WALL_CHAR`, `SNAKE_COLOR`, `SNAKE_HEADS`, `FRUIT_CHAR`, `FRUIT_PALETTE`
*(In online mode, mechanics and speed constants defined in the header will be gracefully ignored in favor of the active server's configurations).*

## Makefile Targets

| Target | Description |
|--------|-------------|
| `make` / `make all` | Compile the main C client |
| `make clean` | Remove client object files |
| `make fclean` | Remove client object files and binaries |
| `make re` | Rebuild client from scratch |
| `make server` | Build the standalone high-performance Go backend |
| `make clean-server` | Clean Go backend build cache |
| `make fclean-server`| Remove Go backend cache and binary |
| `make re-server` | Rebuild Go backend from scratch |
| `make re-all` | Full wipe and rebuild of both the client and Go server |

## License

Free to use and modify.

---

**Enjoy the game!** 🎮🐍
