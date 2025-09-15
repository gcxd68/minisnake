#ifndef MINISNAKE_H
# define MINISNAKE_H

# include <ctype.h>
# include <fcntl.h>
# include <signal.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <sys/ioctl.h>
# include <termios.h>
# include <time.h>
# include <unistd.h>

# define MAX_WIDTH 200
# define MAX_HEIGHT 50
# define INITIAL_DELAY 250000
# define SPEEDUP_FACTOR 0.985f
# define INPUT_QUEUE_SIZE 2

# define CLEAR_SCREEN "\033[2J"
# define CURSOR_HIDE "\033[?25l"
# define CURSOR_SHOW "\033[?25h"
# define CURSOR_POS "\033[%d;%dH"
# define COLOR_RED "\033[31m"
# define COLOR_GREEN "\033[32m"
# define COLOR_RESET "\033[0m"

# define MIN(a, b) (a < b ? a : b)

typedef enum e_dir {
	STOP,
	LEFT,
	RIGHT,
	UP,
	DOWN
}	t_dir;

typedef struct s_data {
	int		width, height, fruitX, fruitY, size, grow, score, gameOver;
	int		x[10001], y[10001], inputQ[INPUT_QUEUE_SIZE + 1];
	float	delay;
	t_dir	dir[2];
}	t_data;

void	initialize(t_data *d);
void	spawnFruit(t_data *d);
void	cleanup(void);

#endif
