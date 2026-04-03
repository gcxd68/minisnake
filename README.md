# minisnake 🐍

A lightweight, terminal-based Snake game written in C with minimal dependencies.

## Features

- 🎮 Classic Snake gameplay in your terminal
- 🌐 Online leaderboard (pre-built release only)
- 🎨 Unicode character graphics
- ⚡ Progressive difficulty (speeds up as you grow)
- 📏 Customizable game board dimensions
- 🎯 Score tracking

## Requirements

- Make
- GCC or any C compiler
- POSIX-compliant system with POSIX threads support (`pthread`)
- Terminal with Unicode support (gnome-terminal recommended)

## Usage

### Online mode (pre-built release only)

Download the binary from the [latest release](../../releases/latest). Before running it, ensure it has execution permissions:

```bash
chmod +x minisnake
./minisnake online
```

Launches a dedicated gnome-terminal window with a fixed 25x20 board. After the game, enter your login to submit your score to the global leaderboard.

> **Note:** Online mode is only available in pre-built releases. If you compile from source without `net.h`, online mode will not be available.

### Custom mode

```bash
./minisnake
./minisnake <width> <height>
```

**Example:**
```bash
./minisnake 40 20
```

Custom mode is offline only. Playing without arguments defaults to 25x20. The game window opens automatically in gnome-terminal.

### Constraints

- **Width:** Between 2 and 200
- **Height:** Between 2 and 50

## Controls

| Key | Action |
|-----|--------|
| `W` | Move Up |
| `A` | Move Left |
| `S` | Move Down |
| `D` | Move Right |
| `X` | Quit |

## Gameplay

1. Guide the snake to eat the fruit (`@`)
2. Each fruit increases your score by 10 points
3. The snake grows longer with each fruit eaten
4. The game speeds up progressively
5. Avoid hitting walls or yourself
6. Win by filling the entire board!

## Game Over Conditions

- 💥 Hitting the boundary walls
- 💥 Running into yourself
- 🏆 Filling the entire game board (You win!)

## Compilation (offline mode)

```bash
make
./minisnake
```

### Building with online mode

Direct connection to Dreamlo from the client is no longer possible for security reasons. Minisnake now features an advanced **Server-Side Scoring Architecture**. The C client never sends its score directly; instead, it communicates with a proxy server via asynchronous HTTP pings every time a fruit is eaten. The server calculates the score securely while enforcing physical limits and timing constraints to prevent hacking.

#### 1. Server Setup (VPS)
1. Go to the `server/` directory and create an `.env` file based on `.env.example` (or create one). Provide your Dreamlo keys and the desired port for the Python proxy:
   ```text
   VPS_PORT=8000
   DREAMLO_PRIVATE_KEY=YOUR_DREAMLO_PRIVATE_KEY
   DREAMLO_PUBLIC_KEY=YOUR_DREAMLO_PUBLIC_KEY
   ```
   > **Note:** The deployment script binds to port `8000` by default, so ensure `VPS_PORT` matches or is configured accordingly.
2. Run the automated deployment script. It will detect your package manager, install dependencies, set up a Python virtual environment, configure the firewall (if using UFW), and start the server in the background using Gunicorn:
   ```bash
   chmod +x deploy.sh
   ./deploy.sh
   ```

#### 2. Client Setup (Game)
1. Create a `net` file in the root directory (you can use `net.example` as a template) and add the **VPS hostname/IP** and **port** you defined earlier:
   ```text
   VPS_HOST=YOUR_VPS_IP
   VPS_PORT=YOUR_VPS_PORT
   ```
2. Compile:
   ```bash
   make
   ./minisnake online
   ```

> **Architecture Note:** The client utilizes POSIX threads (`pthread`) to implement a "fire-and-forget" static resource pool for non-blocking asynchronous HTTP requests. It securely links against the server-side validator without stalling the game loop.

## Makefile targets

| Target | Description |
|--------|-------------|
| `make` | Compile the project |
| `make clean` | Remove object files |
| `make fclean` | Remove object files and executable |
| `make re` | Rebuild everything from scratch |

## Configuration

You can modify these constants in `minisnake.h`:

**Gameplay & Board:**
- `INITIAL_DELAY` — Starting game speed in microseconds (default: 250000)
- `SPEEDUP_FACTOR` — Speed multiplier per fruit (default: 0.985f)
- `INPUT_Q_SIZE` — Number of inputs buffered (default: 2)
- `MIN_WIDTH` / `MAX_WIDTH` — Allowed board width range (default: 2 to 200)
- `MIN_HEIGHT` / `MAX_HEIGHT` — Allowed board height range (default: 2 to 50)
- `DEFAULT_WIDTH` / `DEFAULT_HEIGHT` — Default/Online mode board dimensions (default: 25x20)

**Controls & Text:**
- `MOVE_KEYS` — Movement controls (default: "ADWS")
- `EXIT_KEY` — Key to exit the game (default: "X")
- `TERM_TITLE` — Terminal window title (default: "minisnake")
- `MSG_LOSS` / `MSG_WIN` — End-game messages

**Visuals & Aesthetics:**
- `WALL_CHAR` / `WALL_COLOR` — Wall appearance
- `SNAKE_COLOR` / `SNAKE_BODY` / `SNAKE_HEADS` / `SNAKE_BENDS` — Snake appearance
- `FRUIT_CHAR` / `FRUIT_PALETTE` — Fruit character and color variations

## License

Free to use and modify.

---

**Enjoy the game!** 🎮🐍
