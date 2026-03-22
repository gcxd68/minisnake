#ifndef MINISNAKE_H
# define MINISNAKE_H

# include <ctype.h>
# include <errno.h>
# include <fcntl.h>
# include <netdb.h>
# include <netinet/in.h>
# include <signal.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <sys/ioctl.h>
# include <sys/socket.h>
# include <termios.h>
# include <time.h>
# include <unistd.h>

# ifdef __has_include
#  if __has_include("keys.h")
#   include "keys.h"
#   define ONLINE_BUILD 1
#  endif
# endif

# define ONLINE_WIDTH  25
# define ONLINE_HEIGHT 20

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

# define CLEAR_SCREEN	"\033[2J\033[3J\033[H"
# define ERASE_LINE		"\033[2K"
# define CURSOR_HIDE	"\033[?25l"
# define CURSOR_SHOW	"\033[?25h"
# define CURSOR_POS		"\033[%d;%dH"
# define COLOR_RED		"\033[31m"
# define COLOR_GREEN	"\033[32m"
# define COLOR_RESET	"\033[0m"

# define MIN(a, b) (a < b ? a : b)

typedef enum e_dir
{
	STOP, LEFT, RIGHT, UP, DOWN
}	t_dir;

typedef struct s_data
{
	int		width, height, fruit_x, fruit_y, size, grow, score, game_over;
	int		x[10001], y[10001], input_q[INPUT_QUEUE_SIZE + 1];
	float	delay;
	t_dir	dir[2];
	int		online;
}	t_data;

void	initialize(t_data *d);
void	spawn_fruit(t_data *d);
void	restore_terminal(void);
void	clean_exit(int exit_code);
void	ask_and_submit(t_data *d);

#endif
