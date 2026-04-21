#ifndef MINISNAKE_H
# define MINISNAKE_H

/* Include(s) */
# include <ctype.h>
# include <fcntl.h>
# include <pthread.h>
# include <signal.h>
# include <stdint.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <termios.h>
# include <time.h>
# include <unistd.h>

# ifdef __has_include
#  if __has_include("net.h")
#   include "net.h"
#   define ONLINE_BUILD					1
#  endif
# endif

/* Client software version for backend synchronization and update enforcement */
# define CLIENT_VERSION					"6"

/* GAME CONFIGURATION: Dimensions, speeds, and rules */
# define MIN_WIDTH						2
# define MIN_HEIGHT						2
# define MAX_WIDTH						200
# define MAX_HEIGHT						50
# define MAX_SIZE    					(MAX_WIDTH * MAX_HEIGHT)
# define DEF_WIDTH						25
# define DEF_HEIGHT						20
# define DEF_INITIAL_DELAY				250000
# define DEF_SPEEDUP_FACTOR				0.985f
# define DEF_INITIAL_SIZE				3
# define INPUT_Q_SIZE					2
# define DEF_POINTS_PER_FRUIT			10
# define DEF_SPAWN_FRUIT_MAX_ATTEMPTS	10000
# define DEF_CHEAT_TIMEOUT				5000
# define DEF_PENALTY_INTERVAL			10
# define DEF_PENALTY_AMOUNT				1

/* SPLASH SCREEN & ANIMATION */
# define SPLASH_TITLE_MIN_ROW			3
# define SPLASH_FRAMES					30
# define SPLASH_OFFSET_MINI				5
# define SPLASH_OFFSET_NAKE				1
# define SPLASH_OFFSET_SNAKE			1
# define SPLASH_WORD_LEN				4
# define SPLASH_SNAKE_START_Y			(-MAX_HEIGHT)
# define SPLASH_MSG_PROMPT				"Press ENTER to start"
# define SPLASH_MINI_CHAR				"mini"
# define SPLASH_NAKE_CHAR				"nake"
# define SPLASH_SNAKE_CHAR				"🐍"
# define SPLASH_USLEEP					30000
# define SPLASH_PROMPT_BOTTOM_MARGIN	1
# define SPLASH_TITLE_TO_PROMPT_DIST	3
# define SPLASH_BLINK_RATE				15

/* SYSTEM & I/O: Buffers, paths, and environment */
# define BUF_GEOM						32
# define BUF_CMD						512
# define TERM_TITLE						"minisnake"
# define ENV_VAR						"MINISNAKE_LAUNCHED"
# define DEFAULT_EXE					"./minisnake"

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
# if MAX_SIZE > 50000
#  error "MAX_SIZE = MAX_WIDTH * MAX_HEIGHT is too large and may cause a Stack Overflow."
# endif
# if DEF_INITIAL_DELAY < 0
#  error "DEF_INITIAL_DELAY must be >= 0"
# endif
# if DEF_INITIAL_SIZE <= 0
#  error "DEF_INITIAL_SIZE must be strictly positive"
# endif
# if INPUT_Q_SIZE <= 0
#  error "INPUT_Q_SIZE must be > 0"
# endif
# if DEF_POINTS_PER_FRUIT <= 0
#  error "DEF_POINTS_PER_FRUIT must be > 0"
# endif
# if DEF_SPAWN_FRUIT_MAX_ATTEMPTS <= 0
#  error "DEF_SPAWN_FRUIT_MAX_ATTEMPTS must be > 0"
# endif
# if DEF_CHEAT_TIMEOUT < 0
#  error "DEF_CHEAT_TIMEOUT must be >= 0"
# endif
# if DEF_PENALTY_INTERVAL < 0
#  error "DEF_PENALTY_INTERVAL must be >= 0 (0 to disable penalty)"
# endif
# if DEF_PENALTY_AMOUNT < 0
#  error "DEF_PENALTY_AMOUNT must be >= 0"
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
# if SPLASH_TITLE_MIN_ROW < 1
#  error "SPLASH_TITLE_MIN_ROW must be at least 1"
# endif
# if SPLASH_FRAMES <= 0
#  error "SPLASH_FRAMES must be greater than 0 to avoid division by zero"
# endif
# if SPLASH_OFFSET_MINI < 0 || SPLASH_OFFSET_NAKE < 0 || SPLASH_OFFSET_SNAKE < 0
#  error "Splash offsets cannot be negative"
# endif
# if SPLASH_WORD_LEN <= 0
#  error "SPLASH_WORD_LEN must be positive"
# endif
# if SPLASH_USLEEP < 0
#  error "SPLASH_USLEEP cannot be negative"
# endif
# if SPLASH_PROMPT_BOTTOM_MARGIN < 0
#  error "SPLASH_PROMPT_BOTTOM_MARGIN cannot be negative"
# endif
# if SPLASH_TITLE_TO_PROMPT_DIST < 0
#  error "SPLASH_TITLE_TO_PROMPT_DIST cannot be negative"
# endif
# if SPLASH_BLINK_RATE <= 0
#  error "SPLASH_BLINK_RATE must be greater than 0"
# endif
# if BUF_GEOM <= 0 || BUF_CMD <= 0
#  error "Buffer sizes must be strictly positive"
# endif

/* UI & TEXT: Messages and terminal interaction */
# define MOVE_KEYS				"ADWS"
# define ARROW_KEYS				"DCAB"
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
# define SCROLL_REGION			"\033[%d;%dr"
# define SCROLL_RESET			"\033[r"
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

# define DEFAULT_RULES (t_data){ \
	.show_splash = 1, \
	.size = 1, \
	.grow = DEF_INITIAL_SIZE - 1, \
	.points_per_fruit = DEF_POINTS_PER_FRUIT, \
	.spawn_fruit_max_attempts = DEF_SPAWN_FRUIT_MAX_ATTEMPTS, \
	.cheat_timeout = DEF_CHEAT_TIMEOUT, \
	.penalty_interval = DEF_PENALTY_INTERVAL, \
	.penalty_amount = DEF_PENALTY_AMOUNT, \
	.delay = DEF_INITIAL_DELAY, \
	.speedup_factor = DEF_SPEEDUP_FACTOR \
}

/* Enum(s) */
enum { PARSE_OK = 3, LAUNCH_LOCAL = 4, LAUNCH_SPAWN = 5 };

typedef enum e_dir {
	STOP, LEFT, RIGHT, UP, DOWN
}	t_dir;

/* Main structure */
typedef struct s_data {
	/* Configuration & rules - keep this block first for default rules initialization */
	int				show_splash, size, grow, points_per_fruit, spawn_fruit_max_attempts,
						cheat_timeout, penalty_interval, penalty_amount;
	float			delay, speedup_factor;

	/* Game state */
	int				width, height, score, fruit_x, fruit_y, steps, game_over, online, cheat;
	uint32_t		seed;
	t_dir			dir[2]; /* dir[0]: current, dir[1]: previous */
	int				x[MAX_SIZE + 1], y[MAX_SIZE + 1]; /* Snake body */
	int				input_q[INPUT_Q_SIZE + 1];
	const char		*fruit_color;
	pthread_mutex_t	fruit_mutex;

	/* Network & telemetry */
	char			token[33], path[MAX_SIZE + 1];
	int				seq, path_steps;
}	t_data;

/* minisnake_game - Gameplay functions */
void				spawn_fruit(t_data *d);
const char			*fruit_color(void);
void				game_loop(t_data *d);

/* minisnake_sys - System functions */
void				splash_screen(t_data *d);
void				show_loading(void);
uint32_t			sys_rand(void);
uint32_t			lcg_rand(uint32_t *seed);

/* minisnake_net.c - Network functions */
int					check_client_version(void);
int					server_sync_rules(t_data *d);
int					start_session(t_data *d);
void				notify_server(t_data *d, const char *action, int fx, int fy);
void				handle_leaderboard(t_data *d);
void				net_wait_all(void);

#endif
