#ifndef MINISNAKE_H
# define MINISNAKE_H

# include <ctype.h>
# include <errno.h>
# include <fcntl.h>
# include <netdb.h>
# include <netinet/in.h>
# include <pthread.h>
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
#  if __has_include("net.h")
#   include "net.h"
#   define ONLINE_BUILD			1
#  endif
# endif

/* GAME CONFIGURATION: Dimensions, speeds, and rules */
# define MIN_WIDTH				2
# define MIN_HEIGHT				2
# define MAX_WIDTH				200
# define MAX_HEIGHT				50
# define DEF_WIDTH				25
# define DEF_HEIGHT				20
# define DEF_INITIAL_DELAY		250000
# define DEF_SPEEDUP_FACTOR		0.985f
# define INPUT_Q_SIZE			2
# define DEF_POINTS_PER_FRUIT	10
# define DEF_CHEAT_TIMEOUT		5000

/* SYSTEM & I/O: Buffers, paths, and environment */
# define BUF_GEOM				32
# define BUF_CMD				512

# define TERM_TITLE				"minisnake"
# define ENV_VAR				"MINISNAKE_LAUNCHED"
# define DEFAULT_EXE			"./minisnake"

/* PREPROCESSOR CHECKS: Compile-time safety validation */
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
# if DEF_INITIAL_DELAY < 0
#  error "DEF_INITIAL_DELAY must be >= 0"
# endif
# if INPUT_Q_SIZE <= 0
#  error "INPUT_Q_SIZE must be > 0"
# endif
# if DEF_POINTS_PER_FRUIT <= 0
#  error "DEF_POINTS_PER_FRUIT must be > 0"
# endif
# if DEF_CHEAT_TIMEOUT < 0
#  error "DEF_CHEAT_TIMEOUT must be >= 0"
# endif
# if MIN_WIDTH * MIN_HEIGHT > 10000
#  error "Minimum board size exceeds maximum snake capacity (10000)"
# endif
# if MAX_WIDTH * MAX_HEIGHT > 10000
#  error "Maximum board size exceeds maximum snake capacity (10000)"
# endif
# if DEF_WIDTH < MIN_WIDTH || DEF_WIDTH > MAX_WIDTH
#  error "DEF_WIDTH must be between MIN_WIDTH and MAX_WIDTH"
# endif
# if DEF_HEIGHT < MIN_HEIGHT || DEF_HEIGHT > MAX_HEIGHT
#  error "DEF_HEIGHT must be between MIN_HEIGHT and MAX_HEIGHT"
# endif
# if BUF_GEOM <= 0 || BUF_CMD <= 0
#  error "Buffer sizes must be strictly positive"
# endif

/* UI & TEXT: Messages and terminal interaction */
# define MOVE_KEYS				"ADWS"
# define EXIT_KEY				"X"
# define INSTRUCTIONS			"Use " MOVE_KEYS " to move, " EXIT_KEY " to quit"
# define MSG_LOSS				"GAME OVER"
# define MSG_WIN				"YOU WON !"

/* GRAPHICS: ANSI escape codes, colors, and unicode characters */
# define CLEAR_SCREEN			"\033[2J\033[3J\033[H"
# define ERASE_LINE				"\033[2K"
# define CURSOR_HIDE			"\033[?25l"
# define CURSOR_SHOW			"\033[?25h"
# define CURSOR_POS				"\033[%d;%dH"
# define COLOR_RED				"\033[31m"
# define COLOR_GREEN			"\033[32m"
# define COLOR_YELLOW			"\033[33m"
# define COLOR_MAGENTA			"\033[35m"
# define COLOR_CYAN				"\033[36m"
# define COLOR_WHITE			"\033[37m"
# define STYLE_BOLD				"\033[1m"
# define STYLE_RESET			"\033[0m"

# define WALL_CHAR				"░"
# define WALL_COLOR				COLOR_WHITE
# define SNAKE_IDLE				"🭎"
# define SNAKE_HEADS			{ "🭨", "🭪", "🭩", "🭫" }
# define SNAKE_BODY				"▚"
# define SNAKE_BENDS			{ "▗", "▘" }
# define SNAKE_COLOR			COLOR_GREEN
# define FRUIT_CHAR				"@"
# define FRUIT_PALETTE			{ COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE }

/* MACROS & TYPES: Generic macros and structure definitions */
# define ARR_SIZE(x)			(sizeof(x) / sizeof(x[0]))
# define MIN(a, b)				(a < b ? a : b)
# define MAX(a, b)				(a > b ? a : b)
# define IS_SESSION_ACTIVE(d)	(d->online && d->token[0] != '\0')

/* Default initializer for t_data */
# define DEFAULT_RULES (t_data){ \
    .width = DEF_WIDTH, \
    .height = DEF_HEIGHT, \
    .points_per_fruit = DEF_POINTS_PER_FRUIT, \
    .cheat_timeout = DEF_CHEAT_TIMEOUT, \
    .delay = DEF_INITIAL_DELAY, \
    .speedup_factor = DEF_SPEEDUP_FACTOR \
}

enum { LAUNCH_LOCAL = 3, LAUNCH_SPAWN = 4 };

typedef enum e_dir
{
	STOP, LEFT, RIGHT, UP, DOWN
}	t_dir;

typedef struct s_data
{
	int		width, height, points_per_fruit, cheat_timeout;
	float	delay, speedup_factor;
	int		fruit_x, fruit_y, size, grow, score, game_over, online, cheat;
	int		x[10001], y[10001], input_q[INPUT_Q_SIZE + 1];
	t_dir	dir[2];
	char	token[33]; 
}	t_data;

void		enable_raw_mode(void);
void		disable_raw_mode(void);
void		game_loop(t_data *d);
void		spawn_fruit(t_data *d);
const char	*fruit_color(void);

/* Network functions */
void		handle_leaderboard(t_data *d);
void		start_session(t_data *d);
void		notify_server(t_data *d, const char *action);

#endif
