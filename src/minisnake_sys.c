#include "minisnake.h"

static int	parse_dimension(const char *str, int min, int max, int *out, const char *name) {
	char	*endptr;
	long	val = strtol(str, &endptr, 10);

	if (!*endptr && val >= min && val <= max) {
		*out = (int)val;
		return (0);
	}
	fprintf(stderr, "minisnake: %s must be an integer between %d and %d\n", name, min, max);
	return (2);
}

static int	read_char(void) {
	int c = getchar();
	if (c != '\n' && c != EOF)
		for (int next; (next = getchar()) != '\n' && next != EOF;);
	return (c);
}

static int	ask_confirm(const char *question) {
	printf("%s", question);
	fflush(stdout);
	int c = read_char();
	return (c == 'y' || c == 'Y' || c == '\n');
}

static int	parse_args(int argc, char **argv, t_data *d) {
	if (DEF_SPEEDUP_FACTOR < 0.0f || DEF_SPEEDUP_FACTOR >= 1.0f) {
		fprintf(stderr, "Error: SPEEDUP_FACTOR must be >= 0.0 and < 1.0\n");
		return(EXIT_FAILURE);
	}
	if (argc == 1 || (argc == 2 && !strcmp(argv[1], "online"))) {
		d->width = DEF_WIDTH;
		d->height = DEF_HEIGHT;

#ifdef ONLINE_BUILD
		int ver_status = check_client_version();
		if (ver_status == -1) {
			if (!getenv(ENV_VAR)) {
				fprintf(stderr, "WARNING: Your client version is outdated.\n"
					"Please download the latest release from: https://github.com/gcxd68/minisnake/releases\n");
				if (!ask_confirm("Would you like to play in Offline Mode instead? (y/n): "))
					return (EXIT_SUCCESS);
			}
		}
		if (ver_status == 1 && server_sync_rules(d))
			d->online = 1;
#else
		if (argc == 2) {
			fprintf(stderr, "minisnake: online mode not available in this build\n");
			return(EXIT_FAILURE);
		}
#endif

	} else if (argc == 3) {
		if (parse_dimension(argv[1], MIN_WIDTH, MAX_WIDTH, &d->width, "width")
			|| parse_dimension(argv[2], MIN_HEIGHT, MAX_HEIGHT, &d->height, "height"))
			return (2);
	} else {
		fprintf(stderr, "Usage: ./minisnake\n"
						"       ./minisnake online\n"
						"       ./minisnake WIDTH HEIGHT\n");
		return(2);
	}
	if ((d->width * d->height) % 2 != 0) {
		fprintf(stderr, "minisnake: board area (%dx%d = %d) must be even.\n", 
				d->width, d->height, d->width * d->height);
		return (2);
	}
	return (PARSE_OK);
}

static int	install_dependencies(void) {
	const int	has_xfce = (system("which xfce4-terminal > /dev/null 2>&1") == 0);
	const int	has_gnome = (system("which gnome-terminal > /dev/null 2>&1") == 0);
	const int	need_term = !has_xfce && !has_gnome;
	const int	need_font = system("dpkg -s fonts-noto-color-emoji > /dev/null 2>&1");

	if (!need_term && !need_font) return (LAUNCH_SPAWN);

	const int	has_apt = (system("command -v apt > /dev/null 2>&1") == 0);

	if (need_term || need_font) {
		printf("For the best visual experience, these packages are recommended:\n");
		if (need_term) printf("- xfce4-terminal\n");
		if (need_font) printf("- fonts-noto-color-emoji\n");
		if (has_apt) {
			if (ask_confirm("Would you like to install them now? (y/n): ")) {
				printf("Installing dependencies...\n");
				char cmd[BUF_CMD] = "sudo apt update && sudo apt install -y";
				if (need_term) strcat(cmd, " xfce4-terminal");
				if (need_font) strcat(cmd, " fonts-noto-color-emoji");
				if (system(cmd) == 0)
					return (LAUNCH_SPAWN);
				fprintf(stderr, "Installation failed.\n");
			} else
				printf("Installation skipped.\n");
		}
		else
			printf("You can install them manually.\n");
	}
	if (ask_confirm("Would you like to play in the current terminal instead? (y/n): "))
		return (LAUNCH_LOCAL);
	return (EXIT_SUCCESS);
}

static int	launch_terminal(int argc, char **argv, t_data *d) {
	char	geom[BUF_GEOM], cmd[BUF_CMD];
	char	*self, *tty;
	int		ret;

	if (getenv(ENV_VAR))
		return (LAUNCH_LOCAL);
	
	if ((ret = install_dependencies()) != LAUNCH_SPAWN)
		return (ret);
		
	self = realpath("/proc/self/exe", NULL);
	const char *exe_path = self ? self : DEFAULT_EXE;

	if (!(tty = ttyname(STDERR_FILENO)))
		tty = "/dev/null";
	int width = MAX(d->width + 2, (int)strlen(INSTRUCTIONS));
	snprintf(geom, sizeof(geom), "%dx%d", width, d->height + 4);
	snprintf(cmd, sizeof(cmd), "%s %s %s 2>%s", exe_path,
		(argc > 1) ? argv[1] : "", (argc > 2) ? argv[2] : "", tty);
	
	int has_xfce = (system("which xfce4-terminal > /dev/null 2>&1") == 0);
	char *args_xfce[] = {"xfce4-terminal", "--disable-server", "--hide-menubar", 
		"--hide-toolbar", "--hide-scrollbar", "--geometry", geom, "--zoom", TERM_ZOOM, 
		"--title", TERM_TITLE, "-x", "bash", "-c", cmd, NULL};
	char *args_gnome[] = {"gnome-terminal", "--hide-menubar",
		"--geometry", geom, "--zoom", TERM_ZOOM, "--title", TERM_TITLE, "--", "bash", "-c", cmd, NULL};
	char **args = has_xfce ? args_xfce : args_gnome;
	
	setenv("GTK_THEME", "Adwaita:dark", 1);
	setenv(ENV_VAR, "1", 1);
	if (execvp(args[0], args) == -1) {
		perror("minisnake: execvp failed");
		fprintf(stderr, "Failed to open a new terminal window.\n");
		free(self);
		if (!ask_confirm("Would you like to use the current terminal instead? (y/n): "))
			return(EXIT_FAILURE);
	}

	unsetenv(ENV_VAR);
	return (LAUNCH_LOCAL);
}

struct termios	g_saved_term;
int				g_saved_stdin_flags = -1;

static void	enable_raw_mode(void) {
	struct termios	raw;

	raw = g_saved_term;
	raw.c_lflag &= ~(ICANON | ECHO);
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1)
		perror("minisnake: tcsetattr failed"), exit(EXIT_FAILURE);
	printf(CURSOR_HIDE);
}

static void	disable_raw_mode(void) {
	tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_term);
	printf(SCROLL_RESET CURSOR_SHOW);
}

static void	restore_stdin_flags(void) {
	if (g_saved_stdin_flags != -1)
		fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags);
}

static void restore_terminal(void) {
	disable_raw_mode();
	restore_stdin_flags();
}

static void	clean_exit(int status) {
	restore_terminal();
	exit(status);
}

static void	setup_terminal(void) {
	if (tcgetattr(STDIN_FILENO, &g_saved_term) == -1)
		perror("minisnake: tcgetattr failed"), exit(EXIT_FAILURE);
	enable_raw_mode();

	if ((g_saved_stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0)) == -1
		|| fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags | O_NONBLOCK) == -1)
		perror("minisnake: fcntl failed"), clean_exit(EXIT_FAILURE);
}

static void	handle_sig(int sig) {
	clean_exit(SIG_FATAL_BASE + sig);
}

static void setup_sig(void) {
	static int			initialized = 0;

	if (initialized) return;

	const int			signals[] = {SIGINT, SIGQUIT, SIGTERM};
	struct sigaction	sa;

	sa.sa_handler = handle_sig;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	for (size_t i = 0; i < sizeof(signals) / sizeof(*signals); i++) {
		if (sigaction(signals[i], &sa, NULL) == -1) {
			perror("minisnake: sigaction failed");
			clean_exit(EXIT_FAILURE);
		}
	}
	initialized = 1;
}

void splash_screen(t_data *d) {
	if (!d->show_splash) return;

	const int	w = MAX(d->width + 2, (int)strlen(INSTRUCTIONS)), h = d->height + 4;
	const int	v_cx = (MAX_WIDTH / 2) + 1, v_cy = MAX(SPLASH_TITLE_MIN_ROW, (d->height / 2) + 1);
	const int	r_cx = (w / 2) + 1;

	for (int i = 0; i <= SPLASH_FRAMES; i++) {
		const int	lx = 1 + (v_cx - SPLASH_OFFSET_MINI - 1) * i / SPLASH_FRAMES;
		const int	rx = (MAX_WIDTH - SPLASH_WORD_LEN) + (v_cx + SPLASH_OFFSET_NAKE - (MAX_WIDTH - SPLASH_WORD_LEN)) * i / SPLASH_FRAMES;
		const int	ty = SPLASH_SNAKE_START_Y + (v_cy - SPLASH_SNAKE_START_Y) * i / SPLASH_FRAMES;
		const int	r_lx = lx - (v_cx - r_cx), r_rx = rx - (v_cx - r_cx);

		printf(CLEAR_SCREEN);
		if (r_lx > 0 && r_lx <= w - SPLASH_WORD_LEN + 1) 
			printf(CURSOR_POS STYLE_BOLD SPLASH_MINI_CHAR STYLE_RESET, v_cy, r_lx);
		if (r_rx > 0 && r_rx <= w - SPLASH_WORD_LEN + 1) 
			printf(CURSOR_POS STYLE_BOLD SPLASH_NAKE_CHAR STYLE_RESET, v_cy, r_rx);
		if (ty > 0 && ty <= h) 
			printf(CURSOR_POS SPLASH_SNAKE_CHAR, ty, r_cx - SPLASH_OFFSET_SNAKE);
		
		fflush(stdout);
		usleep(SPLASH_USLEEP);
	}

	const char	*msg = SPLASH_MSG_PROMPT;
	const int	m_x = MAX(1, r_cx - ((int)strlen(msg) / 2));
	const int	m_y = MIN(h - SPLASH_PROMPT_BOTTOM_MARGIN, MAX(v_cy + 1, v_cy + SPLASH_TITLE_TO_PROMPT_DIST));
	int			blink = 0;

	tcflush(STDIN_FILENO, TCIFLUSH);
	while (1) {
		if (getchar() != EOF) break;
		if (blink % SPLASH_BLINK_RATE == 0) {
			if ((blink / SPLASH_BLINK_RATE) % 2 == 0) 
				printf(CURSOR_POS "%s", m_y, m_x, msg);
			else 
				printf(CURSOR_POS "%*s", m_y, m_x, (int)strlen(msg), "");
			fflush(stdout);
		}
		blink++;
		usleep(SPLASH_USLEEP);
	}
	d->show_splash = 0;
}

void show_loading(void) {
	printf(CLEAR_SCREEN "Loading...");
	fflush(stdout);
}

uint32_t sys_rand(void) {
	static int	initialized = 0;

	if (!initialized) {
		srand((unsigned int)time(NULL));
		initialized = 1;
	}
	return ((uint32_t)rand());
}

uint32_t lcg_rand(uint32_t *seed) {
	*seed = (*seed * 1103515245 + 12345) & 0x7fffffff;
	return *seed;
}

static void	init_game(t_data *d) {
	static t_data	save_state;

	if (!save_state.size)
		save_state = *d;
	if (!d->online && save_state.online)
		save_state.online = 0;
	*d = save_state;

	/* 1. Init MUTEX AFTER copying save_state to avoid corruption */
	pthread_mutex_init(&d->fruit_mutex, NULL);

	if (d->online && !start_session(d))
		d->online = 0;
	memset(d->input_q, EOF, sizeof(d->input_q));
	if (!d->online) {
		d->seed = sys_rand();
		d->x[0] = (d->width >> 1) - (d->width % 2 ? 0 : (int)((lcg_rand(&d->seed) >> 16) % 2));
		d->y[0] = (d->height >> 1) - (d->height % 2 ? 0 : (int)((lcg_rand(&d->seed) >> 16) % 2));
	}
	for (int i = 1; i <= d->size + d->grow; i++) {
		d->x[i] = d->x[0];
		d->y[i] = d->y[0];
	}
	if (!d->online) {
		spawn_fruit(d);
	}
}

static void	setup_display(t_data *d) {
	printf(CLEAR_SCREEN);

	int fx, fy;
	read_fruit(d, &fx, &fy, NULL);

	if (fx >= 0 && fy >= 0)
		printf(CURSOR_POS "%s" STYLE_BOLD FRUIT_CHAR STYLE_RESET, 
			fy + 2, fx + 2, d->fruit_color ? d->fruit_color : COLOR_RED);

	printf(CURSOR_POS SNAKE_COLOR SNAKE_IDLE WALL_COLOR, d->y[0] + 2, d->x[0] + 2);
	for (int y = 2; y <= d->height + 1; y++)
		printf(CURSOR_POS WALL_CHAR CURSOR_POS WALL_CHAR, y, 1, y, d->width + 2);
	for (int x = 1; x <= d->width + 2; x++)
		printf(CURSOR_POS WALL_CHAR CURSOR_POS WALL_CHAR, 1, x, d->height + 2, x);
	printf(STYLE_RESET CURSOR_POS "Score: 0" CURSOR_POS INSTRUCTIONS, d->height + 3, 1, d->height + 4, 1);
}

static void	initialize(t_data *d) {
	setup_terminal();
	setup_sig();
	splash_screen(d);
	show_loading();
	init_game(d);
	setup_display(d);
}

static void	finalize(t_data *d) {
	const char	*outcome = d->game_over ? MSG_LOSS : MSG_WIN;
	const int	col = MAX(d->width - (int)strlen(outcome) + 3,
					(int)strlen(INSTRUCTIONS) - (int)strlen(outcome) + 1);

	printf(CURSOR_POS "%s%s" STYLE_RESET, d->height + 3, col, d->game_over
		? COLOR_RED : COLOR_GREEN, outcome);
		
	restore_terminal();
	tcflush(STDIN_FILENO, TCIFLUSH);
	handle_leaderboard(d);
	printf(CURSOR_POS ERASE_LINE, d->height + 4, 1);

	/* 2. Wait for async workers and gracefully destroy MUTEX */
	net_wait_all();
	pthread_mutex_destroy(&d->fruit_mutex);
}

int	main(int argc, char **argv) {
	t_data	d = DEFAULT_RULES;
	int		ret;

	if ((ret = parse_args(argc, argv, &d)) != PARSE_OK) return (ret);
	if ((ret = launch_terminal(argc, argv, &d)) != LAUNCH_LOCAL) return (ret);

	do {
		initialize(&d);
		game_loop(&d);
		finalize(&d);
	} while (ask_confirm("Play again? (y/n): "));
	printf("\n");
	return (EXIT_SUCCESS);
}
