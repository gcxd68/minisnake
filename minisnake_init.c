#include "minisnake.h"

struct termios	g_savedTerm;
int				g_savedStdinFlags;

static void	setupIO() {
	struct termios	gameMode;

	tcgetattr(STDIN_FILENO, &g_savedTerm);
	gameMode = g_savedTerm;
	gameMode.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &gameMode);
	g_savedStdinFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, g_savedStdinFlags | O_NONBLOCK);
	printf(CLEAR_SCREEN CURSOR_HOME CURSOR_HIDE);
}

static void adjustDimensions(t_data *d) {
	struct winsize	ws;

	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	d->width = MIN(MIN(d->width, ws.ws_col - 2), MAX_WIDTH);
	d->height = MIN(MIN(d->height, ws.ws_row - 6), MAX_HEIGHT);
}

static void	initGame(t_data *d) {
	d->sSize = 1;
	d->delay = INITIAL_DELAY;
	memset(d->inputQ, EOF, sizeof(d->inputQ));
	srand(time(NULL));
	d->x[0] = (d->width >> 1) - (d->width % 2 ? 0 : rand() % 2);
	d->y[0] = (d->height >> 1) - (d->height % 2 ? 0 : rand() % 2);
	spawnFruit(d);
}

static void	setupDisplay(t_data *d) {
	for (int i = 0; i <= d->width; i++)
		printf("░");
	for (int i = 0; i < d->height * d->width; i++)
		printf("%s ", (!(i % d->width)) ? "░\n░" : "");
	for (int i = 0; i < d->width + 2; i++)
		printf("%s░", (!i) ? "░\n" : "");
	printf("\nScore:\nUse WASD to move, X to quit");
}

void	cleanup(void) {
	tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerm);
	fcntl(STDIN_FILENO, F_SETFL, g_savedStdinFlags);
	write(STDOUT_FILENO, CURSOR_SHOW, 6);
}

static void handleSig(int sig) {
	cleanup();
	exit(128 + sig);
}

void	initialize(t_data *d) {
	setupIO();
	adjustDimensions(d);
	initGame(d);
	setupDisplay(d);
	for (int i = 0; i < 3; i++)
		signal(((int[]){SIGINT, SIGQUIT, SIGTERM})[i], handleSig);
}
