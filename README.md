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
- POSIX-compliant system (Linux)
- gnome-terminal
- Terminal with Unicode support

## Usage

### Online mode (pre-built release only)

Download the binary from the [latest release](../../releases/latest) and run:

```bash
./minisnake online
```

Launches a dedicated gnome-terminal window with a fixed 25x20 board. After the game, enter your login to submit your score to the global leaderboard.

> **Note:** Online mode is only available in pre-built releases. If you compile from source without `keys.h`, online mode will not be available.

### Custom mode

```bash
./minisnake <width> <height>
```

**Example:**
```bash
./minisnake 40 20
```

Custom mode is offline only. The game window opens automatically in gnome-terminal.

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
./minisnake <width> <height>
```

### Building with online mode

Copy `keys.h.example` to `keys.h`, fill in your Dreamlo keys, then:

```bash
make
./minisnake online
```

## Makefile targets

| Target | Description |
|--------|-------------|
| `make` | Compile the project |
| `make clean` | Remove object files |
| `make fclean` | Remove object files and executable |
| `make re` | Rebuild everything from scratch |

## Configuration

You can modify these constants in `minisnake.h`:

- `INITIAL_DELAY` — Starting game speed (microseconds)
- `SPEEDUP_FACTOR` — Speed multiplier per fruit (default: 0.985)
- `INPUT_QUEUE_SIZE` — Number of inputs buffered (default: 2)
- `ONLINE_WIDTH` / `ONLINE_HEIGHT` — Online mode board dimensions (default: 25x20)

## License

Free to use and modify.

---

**Enjoy the game!** 🎮🐍
