#include "minisnake.h"

void	spawnFruit(t_data *d) {
	int	i;

	do {
		d->fruitX = rand() % d->width;
		d->fruitY = rand() % d->height;
		for (i = 0; i < d->size && !(d->x[i] == d->fruitX && d->y[i] == d->fruitY); i++);
	} while (i < d->size);
}

static void	initSnake(t_data *d, char **argv) {
	struct winsize	ws;

	*d = (t_data){
		.width = atoi(argv[1]),
		.height = atoi(argv[2]),
		.delay = DELAY,
		.size = 1
	};
	if (d->height < 2 || d->width < 2)
		fprintf(stderr, "Error: dimensions must be positive integers greater than one\n"), exit(2);
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	d->width = MIN(MIN(d->width, ws.ws_col - 2), MAX_WIDTH);
	d->height = MIN(MIN(d->height, ws.ws_row - 6), MAX_HEIGHT);
	d->x[0] = d->width >> 1;
	d->y[0] = d->height >> 1;
	srand(time(NULL));
	spawnFruit(d);
}

static void	initTerminal() {
	struct termios	gameMode;

	tcgetattr(STDIN_FILENO, &g_savedTerm);
	gameMode = g_savedTerm;
	gameMode.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &gameMode);
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

static void handleSig(int sig) {
	tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerm);
	write(STDOUT_FILENO, CURSOR_SHOW, 6);
	exit(128 + sig);
}

void	initGame(t_data *d, char **argv) {
	initSnake(d, argv);
	initDisplay(d);
	initTerminal();
	for (size_t i = 0; i < 3; i++)
		signal(((int[]){SIGINT, SIGQUIT, SIGTERM})[i], handleSig);
}
