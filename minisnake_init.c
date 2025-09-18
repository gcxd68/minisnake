#include "minisnake.h"

static void adjustDimensions(t_data *d) {
	struct winsize	ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1)
		perror("minisnake: ioctl failed"), exit(EXIT_FAILURE);
	d->width = MIN(d->width, ws.ws_col - 2);
	d->height = MIN(d->height, ws.ws_row - 6);
}

struct termios	g_savedTerm;
int				g_savedStdinFlags = -1;

static void	setupIO() {
	struct termios	gameMode;

	if (tcgetattr(STDIN_FILENO, &g_savedTerm) == -1)
		perror("minisnake: tcgetattr failed"), exit(EXIT_FAILURE);
	gameMode = g_savedTerm;
	gameMode.c_lflag &= ~(ICANON | ECHO);
	if (tcsetattr(STDIN_FILENO, TCSANOW, &gameMode) == -1)
		perror("minisnake: tcsetattr failed"), exit(EXIT_FAILURE);
	if ((g_savedStdinFlags = fcntl(STDIN_FILENO, F_GETFL, 0)) == -1
		|| fcntl(STDIN_FILENO, F_SETFL, g_savedStdinFlags | O_NONBLOCK) == -1)
		perror("minisnake: fcntl failed"), cleanup(), exit(EXIT_FAILURE);
	printf(CLEAR_SCREEN CURSOR_HIDE);
}

static void	initGame(t_data *d) {
	d->size = 1;
	d->delay = INITIAL_DELAY;
	memset(d->inputQ, EOF, sizeof(d->inputQ));
	srand(time(NULL));
	d->x[0] = (d->width >> 1) - (d->width % 2 ? 0 : rand() % 2);
	d->y[0] = (d->height >> 1) - (d->height % 2 ? 0 : rand() % 2);
	spawnFruit(d);
}

static void	setupDisplay(t_data *d) {
	printf(CURSOR_POS "@", d->fruitY + 2, d->fruitX + 2);
	printf(CURSOR_POS "🭎", d->y[0] + 2, d->x[0] + 2);
	for (int y = 2; y <= d->height + 1; y++)
		printf(CURSOR_POS "░" CURSOR_POS "░", y, 1, y, d->width + 2);
	for (int x = 1; x <= d->width + 2; x++)
		printf(CURSOR_POS "░" CURSOR_POS "░", 1, x, d->height + 2, x);
	printf("\nScore: 0\nUse WASD to move, X to quit\r");
}

void	cleanup(void) {
	tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerm);
	if (g_savedStdinFlags != -1)
		fcntl(STDIN_FILENO, F_SETFL, g_savedStdinFlags);
	printf(COLOR_RESET CURSOR_SHOW "\n");
}

static void handleSig(int sig) {
	cleanup();
	exit(128 + sig);
}

static void setupSig() {
	const int			signals[] = {SIGINT, SIGQUIT, SIGTERM};
	struct sigaction	sa;

	sa.sa_handler = handleSig;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	for (size_t i = 0; i < sizeof(signals) / sizeof(*signals); i++) {
		if (sigaction(signals[i], &sa, NULL) == -1) {
			perror("minisnake: sigaction failed");
			cleanup();
			exit(EXIT_FAILURE);
		}
	}
}

void	initialize(t_data *d) {
	adjustDimensions(d);
	setupIO();
	initGame(d);
	setupDisplay(d);
	setupSig();
}
