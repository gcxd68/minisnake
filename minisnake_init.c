#include "minisnake.h"

void	spawnFruit(t_snake *s) {
	int	i;

	do {
		s->fruitX = rand() % s->width;
		s->fruitY = rand() % s->height;
		for (i = 0; i < s->size && !(s->x[i] == s->fruitX && s->y[i] == s->fruitY); i++);
	} while (i < s->size);
}

static void	initSnake(t_snake *s, char **argv) {
	struct winsize	ws;

	*s = (t_snake){
		.width = atoi(argv[1]),
		.height = atoi(argv[2]),
		.delay = DELAY,
		.size = 1
	};
	if (s->height < 2 || s->width < 2)
		fprintf(stderr, "Error: dimensions must be positive integers greater than one\n"), exit(2);
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	s->width = MIN(MIN(s->width, ws.ws_col - 2), MAX_WIDTH);
	s->height = MIN(MIN(s->height, ws.ws_row - 6), MAX_HEIGHT);
	s->x[0] = s->width >> 1;
	s->y[0] = s->height >> 1;
	srand(time(NULL));
	spawnFruit(s);
}

static void	initTerminal() {
	struct termios	gameMode;

	tcgetattr(STDIN_FILENO, &g_savedTerm);
	gameMode = g_savedTerm;
	gameMode.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &gameMode);
}

static void	initDisplay(t_snake *s) {
	printf(CLEAR_SCREEN CURSOR_HOME CURSOR_HIDE);
	for (int i = 0; i < s->width + 1; i++)
		printf("░");
	for (int i = 0; i < s->height * s->width; i++)
		printf("%s ", (!(i % s->width)) ? "░\n░" : "");
	for (int i = 0; i < s->width + 2; i++)
		printf("%s░", (!i) ? "░\n" : "");
	printf("\nScore:\nUse WASD to move, X to quit");
}

static void handleSig(int sig) {
	tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerm);
	write(STDOUT_FILENO, CURSOR_SHOW, 6);
	exit(128 + sig);
}

void	initGame(t_snake *s, char **argv) {
	initSnake(s, argv);
	initDisplay(s);
	initTerminal();
	for (size_t i = 0; i < 3; i++)
		signal(((int[]){SIGINT, SIGQUIT, SIGTERM})[i], handleSig);
}
