#ifndef MINISNAKE_H
# define MINISNAKE_H

# include <ctype.h>
# include <errno.h>
# include <fcntl.h>
# include <netdb.h>
# include <netinet/in.h>
# include <pthread.h>
# include <signal.h>
# include <stdarg.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <sys/ioctl.h>
# include <sys/socket.h>
# include <termios.h>
# include <time.h>
# include <unistd.h>

# ifdef __has_include
#  if __has_include("net.h")
#   include "net.h"
#   define ONLINE_BUILD		1
#  endif
# endif

# define MIN_WIDTH			2
# define MIN_HEIGHT			2
# define MAX_WIDTH			200
# define MAX_HEIGHT			50
# define INITIAL_DELAY		250000
# define SPEEDUP_FACTOR		0.985f
# define INPUT_Q_SIZE		2
# define POINTS_PER_FRUIT	10
# define CHEAT_TIMEOUT		5000
# define DEFAULT_WIDTH		25
# define DEFAULT_HEIGHT		20
# define BUF_GEOM			32
# define BUF_CMD			512

/* Security definitions removed (no more XOR macros) */

# define TERM_TITLE			"minisnake"
# define ENV_VAR			"MINISNAKE_LAUNCHED"
# define DEFAULT_EXE		"./minisnake"

# define MOVE_KEYS			"ADWS"
# define EXIT_KEY			"X"
# define INSTRUCTIONS		"Use " MOVE_KEYS " to move, " EXIT_KEY " to quit"
# define MSG_LOSS			"GAME OVER"
# define MSG_WIN			"YOU WON !"

# define CLEAR_SCREEN		"\033[2J\033[3J\033[H"
# define ERASE_LINE			"\033[2K"
# define CURSOR_HIDE		"\033[?25l"
# define CURSOR_SHOW		"\033[?25h"
# define CURSOR_POS			"\033[%d;%dH"
# define COLOR_RED			"\033[31m"
# define COLOR_GREEN		"\033[32m"
# define COLOR_YELLOW		"\033[33m"
# define COLOR_MAGENTA		"\033[35m"
# define COLOR_CYAN			"\033[36m"
# define COLOR_WHITE		"\033[37m"
# define STYLE_BOLD			"\033[1m"
# define STYLE_RESET		"\033[0m"

# define WALL_CHAR			"░"
# define WALL_COLOR			COLOR_WHITE
# define SNAKE_IDLE			"🭎"
# define SNAKE_HEADS		{ "🭨", "🭪", "🭩", "🭫" }
# define SNAKE_BODY			"▚"
# define SNAKE_BENDS		{ "▗", "▘" }
# define SNAKE_COLOR		COLOR_GREEN
# define FRUIT_CHAR			"@"
# define FRUIT_PALETTE		{ COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE }

# define ARR_SIZE(x)		(sizeof(x) / sizeof(x[0]))
# define MIN(a, b)			(a < b ? a : b)
# define MAX(a, b)			(a > b ? a : b)

typedef enum e_launch_result {
	LAUNCH_LOCAL = 3, LAUNCH_SPAWN = 4,
}	t_launch_result;

typedef enum e_dir
{
	STOP, LEFT, RIGHT, UP, DOWN
}	t_dir;

typedef struct s_data
{
    /* 'score_mask' removed, score is now stored plainly */
	int		width, height, fruit_x, fruit_y, size, grow, score, game_over, online, cheat;
	int		x[10001], y[10001], input_q[INPUT_Q_SIZE + 1];
	float	delay;
	t_dir	dir[2];
	char	token[33]; /* 32 hex chars + null terminator */
}	t_data;

void		enable_raw_mode(void);
void		disable_raw_mode(void);
void		game_loop(t_data *d);
void		spawn_fruit(t_data *d);
const char	*fruit_color(void);

/* Network functions */
void		handle_leaderboard(t_data *d);
void		vps_start_session(t_data *d);
void        vps_eat(t_data *d);
void        vps_cheat(t_data *d);

#endif
