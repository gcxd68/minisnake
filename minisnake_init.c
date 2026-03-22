#include "minisnake.h"

struct termios	g_saved_term;
int				g_saved_stdin_flags = -1;

static void	setup_io() {
	struct termios	game_mode;

	if (tcgetattr(STDIN_FILENO, &g_saved_term) == -1)
		perror("minisnake: tcgetattr failed"), exit(EXIT_FAILURE);
	game_mode = g_saved_term;
	game_mode.c_lflag &= ~(ICANON | ECHO);
	if (tcsetattr(STDIN_FILENO, TCSANOW, &game_mode) == -1)
		perror("minisnake: tcsetattr failed"), exit(EXIT_FAILURE);
	if ((g_saved_stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0)) == -1
		|| fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags | O_NONBLOCK) == -1)
		perror("minisnake: fcntl failed"), clean_exit(EXIT_FAILURE);
	printf(CLEAR_SCREEN CURSOR_HIDE);
}

static void	init_game(t_data *d) {
	d->size = 1;
	d->delay = INITIAL_DELAY;
	memset(d->input_q, EOF, sizeof(d->input_q));
	srand(time(NULL));
	d->x[0] = (d->width >> 1) - (d->width % 2 ? 0 : rand() % 2);
	d->y[0] = (d->height >> 1) - (d->height % 2 ? 0 : rand() % 2);
	spawn_fruit(d);
}

static void	setup_display(t_data *d) {
	printf(CURSOR_POS "@", d->fruit_y + 2, d->fruit_x + 2);
	printf(CURSOR_POS "🭎", d->y[0] + 2, d->x[0] + 2);
	for (int y = 2; y <= d->height + 1; y++)
		printf(CURSOR_POS "░" CURSOR_POS "░", y, 1, y, d->width + 2);
	for (int x = 1; x <= d->width + 2; x++)
		printf(CURSOR_POS "░" CURSOR_POS "░", 1, x, d->height + 2, x);
	printf(CURSOR_POS "Score: 0" CURSOR_POS INSTRUCTIONS, d->height + 3, 1, d->height + 4, 1);
}

void	restore_terminal(void) {
	tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_term);
	if (g_saved_stdin_flags != -1)
		fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags);
	printf(COLOR_RESET CURSOR_SHOW "\n");
}

void	clean_exit(int status) {
	restore_terminal();
	exit(status);
}

static void handle_sig(int sig) {
	clean_exit(128 + sig);
}

static void setup_sig() {
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

void	initialize(t_data *d) {
	setup_io();
	init_game(d);
	setup_display(d);
	setup_sig();
}
