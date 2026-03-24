#include "minisnake.h"

struct termios	g_saved_term;
int				g_saved_stdin_flags = -1;

void	enable_raw_mode(void)
{
	struct termios	raw;

	raw = g_saved_term;
	raw.c_lflag &= ~(ICANON | ECHO);
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1)
		perror("minisnake: tcsetattr failed"), exit(EXIT_FAILURE);
	printf(CURSOR_HIDE);
}

void	disable_raw_mode(void) {
	tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_term);
	printf(CURSOR_SHOW);
}

static void	restore_stdin_flags(void) {
	if (g_saved_stdin_flags != -1)
		fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags);
}

static void	clean_exit(int status) {
	disable_raw_mode();
	restore_stdin_flags();
	exit(status);
}

static void	setup_io() {

	if (tcgetattr(STDIN_FILENO, &g_saved_term) == -1)
		perror("minisnake: tcgetattr failed"), exit(EXIT_FAILURE);
	enable_raw_mode();
	if ((g_saved_stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0)) == -1
		|| fcntl(STDIN_FILENO, F_SETFL, g_saved_stdin_flags | O_NONBLOCK) == -1)
		perror("minisnake: fcntl failed"), clean_exit(EXIT_FAILURE);
	printf(CLEAR_SCREEN);
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

static void	handle_sig(int sig) {
	clean_exit(128 + sig);
}

static void	setup_sig() {
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

void	finalize(t_data *d)
{
	const char	*outcome = d->game_over ? MSG_LOSS : MSG_WIN;
	const int col = MAX(d->width - (int)strlen(outcome) + 3, (int)strlen(INSTRUCTIONS) - (int)strlen(outcome) + 1);

	printf(CURSOR_POS "%s%s" COLOR_RESET, d->height + 3, col, d->game_over
		? COLOR_RED : COLOR_GREEN, outcome);
	restore_stdin_flags();
	handle_leaderboard(d);
	printf(CURSOR_POS ERASE_LINE "Press Enter to close...\n", d->height + 4, 1);
	fflush(stdout);
	for (int c; (c = getchar()) != '\n' && c != EOF;);
	disable_raw_mode();
}
