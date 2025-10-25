# minisnake 🐍

A lightweight, terminal-based Snake game written in C with minimal dependencies.

## Features

- 🎮 Classic Snake gameplay in your terminal
- 🎨 Unicode character graphics
- ⚡ Progressive difficulty (speeds up as you grow)
- 📏 Customizable game board dimensions
- 🎯 Score tracking

## Requirements

- Make
- GCC or any C compiler
- POSIX-compliant system (Linux, macOS, BSD)
- Terminal with Unicode support

## Compilation

Build the project using Make:

```bash
make
```

This will compile all source files and create the `minisnake` executable.

## Usage

Run the game with width and height arguments:

```bash
./minisnake <width> <height>
```

**Example:**
```bash
./minisnake 20 15
```

### Constraints

- **Width:** Between 2 and 200
- **Height:** Between 2 and 50
- The actual game area will be adjusted to fit your terminal size

## Controls

- **W** - Move Up
- **A** - Move Left  
- **S** - Move Down
- **D** - Move Right
- **X** - Quit game

## Gameplay

1. Guide the snake to eat the fruit (@)
2. Each fruit increases your score by 10 points
3. The snake grows longer with each fruit eaten
4. The game speeds up progressively
5. Avoid hitting walls or yourself
6. Win by filling the entire board!

## Cleaning Up

Remove object files:

```bash
make clean
```

Remove object files and executable:

```bash
make fclean
```

Rebuild everything from scratch:

```bash
make re
```

## Game Over Conditions

- 💥 Hitting the boundary walls
- 💥 Running into yourself
- 🏆 Filling the entire game board (You win!)

## Configuration

You can modify these constants in `minisnake.h`:

- `INITIAL_DELAY` - Starting game speed (microseconds)
- `SPEEDUP_FACTOR` - Speed multiplier per fruit (default: 0.985)
- `INPUT_QUEUE_SIZE` - Number of inputs buffered (default: 2)

## License

Free to use and modify.

---

**Enjoy the game!** 🎮🐍
