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

static int	parse_args(int argc, char **argv, t_data *d) {
	/* Validate compile-time constant at runtime in case someone changes it incorrectly */
	if (DEF_SPEEDUP_FACTOR < 0.0f || DEF_SPEEDUP_FACTOR >= 1.0f) {
		fprintf(stderr, "Error: SPEEDUP_FACTOR must be >= 0.0 and < 1.0\n");
		return(EXIT_FAILURE);
	}
	/* Launch default mode if no arguments, or explicitly requested "online" */
	if (argc == 1 || (argc == 2 && !strcmp(argv[1], "online"))) {
		d->width = DEF_WIDTH;
		d->height = DEF_HEIGHT;

#ifdef ONLINE_BUILD
		/* Override local dimensions with server competitive rules before spawning the terminal GUI */
		d->online = 1;
		server_sync_rules(d);
#else

		/* If network is NOT compiled in, but the user explicitly typed "online", warn them */
		if (argc == 2) {
			fprintf(stderr, "minisnake: online mode not available in this build\n");
			return(EXIT_FAILURE);
		}
		/* If argc == 1, we silently drop to offline mode with default dimensions */
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
		fprintf(stderr, "A perfect game is mathematically impossible on odd grids.\n");
		return (2);
	}
	return (0);
}

/* Read a single character and flush the rest of the line */
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
	return (c == 'y' || c == 'Y');
}

static int	install_gnome_terminal(void) {
	if (ask_confirm("gnome-terminal is not installed.\n"
		"It is recommended for a better user experience, but not required.\n"
		"Would you like to install it? (y/n): ")) {
		printf("Installing gnome-terminal...\n");
		if (system("sudo apt update && sudo apt install -y gnome-terminal") == 0)
			return (LAUNCH_SPAWN);
		fprintf(stderr, "Installation failed. Please install it manually.\n");
	}
	else
		printf("Installation skipped.\n");

	/* Fall back to running in the current terminal if the user agrees */
	if (ask_confirm("Would you like to use the current terminal instead? (y/n): "))
		return (LAUNCH_LOCAL);
	return (EXIT_SUCCESS);
}

/* Try to spawn a dedicated gnome-terminal window.
   If successful (LAUNCH_SPAWN), the current process is replaced by execvp
   and never reaches the game loop. If LAUNCH_LOCAL, play in current terminal. */
static int	launch_terminal(int argc, char **argv, t_data *d) {
	char	geom[BUF_GEOM], cmd[BUF_CMD];
	char	*self, *tty;
	int		ret;

	/* ENV_VAR is set before execvp so the child process knows it's already in the dedicated terminal */
	if (getenv(ENV_VAR))
		return (LAUNCH_LOCAL);
	if (system("which gnome-terminal > /dev/null 2>&1") != 0)
		if ((ret = install_gnome_terminal()) != LAUNCH_SPAWN)
			return (ret);
	self = realpath("/proc/self/exe", NULL);
	const char *exe_path = self ? self : DEFAULT_EXE;

	/* Redirect stderr to the parent tty so error messages remain visible
	   even after the new gnome-terminal window takes over stdout */
	if (!(tty = ttyname(STDERR_FILENO)))
		tty = "/dev/null";
	int width = MAX(d->width + 2, (int)strlen(INSTRUCTIONS));
	snprintf(geom, sizeof(geom), "%dx%d", width, d->height + 4);
	snprintf(cmd, sizeof(cmd), "%s %s %s 2>%s", exe_path,
		(argc > 1) ? argv[1] : "", (argc > 2) ? argv[2] : "", tty);
	char *args[] = {"gnome-terminal", "--disable-factory", "--wait", "--hide-menubar",
		"--geometry", geom, "--zoom", "1.2", "--title", TERM_TITLE, "--", "bash", "-c", cmd, NULL};
	/* Dark theme for a better visual experience */
	setenv("GTK_THEME", "Adwaita:dark", 1);
	setenv(ENV_VAR, "1", 1);
	if (execvp(args[0], args) == -1) {
		/* execvp only returns on failure — offer to continue in the current terminal */
		perror("minisnake: execvp failed");
		fprintf(stderr, "Failed to open a new terminal window.\n");
		free(self);
		if (!ask_confirm("Would you like to use the current terminal instead? (y/n): "))
			return(EXIT_FAILURE);
	}

	/* Reached only if execvp failed and user chose current terminal */
	unsetenv(ENV_VAR);
	return (LAUNCH_LOCAL);
}

/* Saved terminal state — restored on exit to leave the shell intact */
struct termios	g_saved_term;
int				g_saved_stdin_flags = -1;

/* Raw mode disables line buffering (ICANON) and echo so keypresses are instantly read without waiting for Enter */
void	enable_raw_mode(void) {
	struct termios	raw;

	raw = g_saved_term;
	raw.c_lflag &= ~(ICANON | ECHO);
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1)
		perror("minisnake: tcsetattr failed"), exit(EXIT_FAILURE);
	printf(CURSOR_HIDE);
}

/* Restore the terminal to its original state and show the cursor */
void	disable_raw_mode(void) {
	tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_term);
	printf(CURSOR_SHOW);
}

/* Restore the O_NONBLOCK flag so fgets and getchar wait for input. */
static void	restore_stdin_flags(void) {
	if (g_saved_stdin_flags != -1)
		fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags);
}

static void restore_io(void) {
	disable_raw_mode();
	restore_stdin_flags();
}

static void	clean_exit(int status) {
	restore_io();
	exit(status);
}

static void	setup_io(void) {
	if (tcgetattr(STDIN_FILENO, &g_saved_term) == -1)
		perror("minisnake: tcgetattr failed"), exit(EXIT_FAILURE);
	enable_raw_mode();

	/* O_NONBLOCK on stdin prevents getchar() from blocking during the game loop */
	if ((g_saved_stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0)) == -1
		|| fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags | O_NONBLOCK) == -1)
		perror("minisnake: fcntl failed"), clean_exit(EXIT_FAILURE);
	printf(CLEAR_SCREEN);
}

static void	init_game(t_data *d) {
	static t_data	save_state;

	if (!save_state.size)
		save_state = *d;
	else
		*d = save_state;
	memset(d->input_q, EOF, sizeof(d->input_q));
	srand(time(NULL));
	d->x[0] = (d->width >> 1) - (d->width % 2 ? 0 : rand() % 2);
	d->y[0] = (d->height >> 1) - (d->height % 2 ? 0 : rand() % 2);
	spawn_fruit(d);
}

static void	setup_display(t_data *d) {
	printf(CURSOR_POS "%s" STYLE_BOLD FRUIT_CHAR STYLE_RESET, 
	   d->fruit_y + 2, d->fruit_x + 2, fruit_color());
	printf(CURSOR_POS SNAKE_COLOR SNAKE_IDLE WALL_COLOR, d->y[0] + 2, d->x[0] + 2);
	for (int y = 2; y <= d->height + 1; y++)
		printf(CURSOR_POS WALL_CHAR CURSOR_POS WALL_CHAR, y, 1, y, d->width + 2);
	for (int x = 1; x <= d->width + 2; x++)
		printf(CURSOR_POS WALL_CHAR CURSOR_POS WALL_CHAR, 1, x, d->height + 2, x);
	printf(STYLE_RESET CURSOR_POS "Score: 0" CURSOR_POS INSTRUCTIONS, d->height + 3, 1, d->height + 4, 1);
}

static void	handle_sig(int sig) {
	clean_exit(128 + sig);
}

static void	setup_sig(void) {
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
}

static void	initialize(t_data *d) {
	setup_io();
	init_game(d);
	setup_display(d);
	setup_sig();
}

static void	finalize(t_data *d) {
	const char	*outcome = d->game_over ? MSG_LOSS : MSG_WIN;
	const int	col = MAX(d->width - (int)strlen(outcome) + 3,
					(int)strlen(INSTRUCTIONS) - (int)strlen(outcome) + 1);

	printf(CURSOR_POS "%s%s" STYLE_RESET, d->height + 3, col, d->game_over
		? COLOR_RED : COLOR_GREEN, outcome);
	/* Restore blocking stdin before any user-facing read operations */
	restore_io();
	tcflush(STDIN_FILENO, TCIFLUSH);
	handle_leaderboard(d);
	printf(CURSOR_POS ERASE_LINE, d->height + 4, 1);
}

int	main(int argc, char **argv) {
	t_data	d = DEFAULT_RULES;
	int		ret;

	if ((ret = parse_args(argc, argv, &d))) return (ret);
	if ((ret = launch_terminal(argc, argv, &d)) != LAUNCH_LOCAL) return (ret);

	do {
		initialize(&d);
		start_session(&d);
		game_loop(&d);
		finalize(&d);
	} while (ask_confirm("Play again? (y/n): "));
	printf("\n");
	return (EXIT_SUCCESS);
}
