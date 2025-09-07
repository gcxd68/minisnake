#include "minisnake.h"

void	spawn_fruit(t_snake *s) {
	int	i;

	do {
		s->fruitX = rand() % s->width;
		s->fruitY = rand() % s->height;
		for (i = 0; i < s->size && !(s->x[i] == s->fruitX && s->y[i] == s->fruitY); i++);
	} while (i < s->size);
}

static void	init_snake(t_snake *s, char **argv) {
	struct winsize	ws;

	*s = (t_snake){
		.width = atoi(argv[1]),
		.height = atoi(argv[2]),
		.delay = DELAY,
		.size = 1
	};
	if (s->height <= 0 || s->width <= 0)
		fprintf(stderr, "Error: width and height must be positive integers\n"), exit(2);
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	s->width = MIN(MIN(s->width, ws.ws_col - 2), MAX_WIDTH);
	s->height = MIN(MIN(s->height, ws.ws_row - 6), MAX_HEIGHT);
	s->x[0] = s->width >> 1;
	s->y[0] = s->height >> 1;
	srand(time(NULL));
	spawn_fruit(s);
}

static void	init_terminal() {
	struct termios	gameMode;

	tcgetattr(STDIN_FILENO, &g_savedTerm);
	gameMode = g_savedTerm;
	gameMode.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &gameMode);
}

static void	init_display(t_snake *s) {
	printf(CLEAR_SCREEN CURSOR_HOME CURSOR_HIDE);
	for (int i = 0; i < s->width + 1; i++)
		printf("█");
	for (int i = 0; i < s->height * s->width; i++)
		printf("%s ", (!(i % s->width)) ? "█\n█" : "");
	for (int i = 0; i < s->width + 2; i++)
		printf("%s█", (!i) ? "█\n" : "");
	printf("\nScore:\nUse WASD to move, X to quit");
}

static void handle_sig(int sig) {
	tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerm);
	write(STDOUT_FILENO, CURSOR_SHOW, 6);
	exit(128 + sig);
}

void	init_game(t_snake *s, char **argv) {
	init_snake(s, argv);
	init_display(s);
	init_terminal();
	for (size_t i = 0; i < 3; i++)
		signal(((int[]){SIGINT, SIGQUIT, SIGTERM})[i], handle_sig);
}
