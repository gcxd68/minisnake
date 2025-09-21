#ifndef MINISNAKE_H
# define MINISNAKE_H

# include <ctype.h>
# include <errno.h>
# include <fcntl.h>
# include <signal.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <sys/ioctl.h>
# include <termios.h>
# include <time.h>
# include <unistd.h>

# define MIN_WIDTH 2
# define MIN_HEIGHT 2
# define MAX_WIDTH 200
# define MAX_HEIGHT 50
# define INITIAL_DELAY 250000
# define SPEEDUP_FACTOR 0.985f
# define INPUT_QUEUE_SIZE 2
# define KEYS "ADWS"

# if MIN_WIDTH < 2
#  error "MIN_WIDTH must be >= 2"
# endif
# if MIN_HEIGHT < 2
#  error "MIN_HEIGHT must be >= 2"
# endif
# if MAX_WIDTH < MIN_WIDTH
#  error "MAX_WIDTH must be >= MIN_WIDTH"
# endif
# if MAX_HEIGHT < MIN_HEIGHT
#  error "MAX_HEIGHT must be >= MIN_HEIGHT"
# endif
# if INITIAL_DELAY < 0
#  error "INITIAL_DELAY must be >= 0"
# endif
# if INPUT_QUEUE_SIZE <= 0
#  error "INPUT_QUEUE_SIZE must be > 0"
# endif

# define CLEAR_SCREEN "\033[2J"
# define CURSOR_HIDE "\033[?25l"
# define CURSOR_SHOW "\033[?25h"
# define CURSOR_POS "\033[%d;%dH"
# define COLOR_RED "\033[31m"
# define COLOR_GREEN "\033[32m"
# define COLOR_RESET "\033[0m"

# define MIN(a, b) (a < b ? a : b)

typedef enum e_dir {STOP, LEFT, RIGHT, UP, DOWN} t_dir;

typedef struct s_data {
	int		width, height, fruit_x, fruit_y, size, grow, score, game_over;
	int		x[10001], y[10001], input_q[INPUT_QUEUE_SIZE + 1];
	float	delay;
	t_dir	dir[2];
}	t_data;

void	initialize(t_data *d);
void	spawn_fruit(t_data *d);
void	clean_exit(int exit_code);

#endif
