#include "minisnake.h"

struct termios	g_savedTerm;
int				g_savedStdinFlags;

static void	initTerminal() {
	struct termios	gameMode;

	tcgetattr(STDIN_FILENO, &g_savedTerm);
	gameMode = g_savedTerm;
	gameMode.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &gameMode);
}

static void	initInput() {
	g_savedStdinFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, g_savedStdinFlags | O_NONBLOCK);
}

static void	initGame(t_data *d, char **argv) {
	struct winsize	ws;

	*d = (t_data){
		.width = atoi(argv[1]),
		.height = atoi(argv[2]),
		.delay = INITIAL_DELAY,
		.sSize = 1
	};
	if (d->height < 2 || d->width < 2)
		fprintf(stderr, "Error: dimensions must be positive integers greater than one\n"), exit(2);
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	d->width = MIN(MIN(d->width, ws.ws_col - 2), MAX_WIDTH);
	d->height = MIN(MIN(d->height, ws.ws_row - 6), MAX_HEIGHT);
	srand(time(NULL));
	d->x[0] = (d->width >> 1) - (d->width % 2 ? 0 : rand() % 2);
	d->y[0] = (d->height >> 1) - (d->height % 2 ? 0 : rand() % 2);
	spawnFruit(d);
}

static void	initDisplay(t_data *d) {
	printf(CLEAR_SCREEN CURSOR_HOME CURSOR_HIDE);
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

void	init(t_data *d, char **argv) {
	initTerminal();
	initInput();
	initGame(d, argv);
	initDisplay(d);
	for (size_t i = 0; i < 3; i++)
		signal(((int[]){SIGINT, SIGQUIT, SIGTERM})[i], handleSig);
}
