#include "minisnake.h"

void spawn_fruit(t_snake *s) {
    while (1) {
        s->fruitX = rand() % s->width;
        s->fruitY = rand() % s->height;
        int i;
        for (i = 0; i < s->size; i++)
            if (s->x[i] == s->fruitX && s->y[i] == s->fruitY) break;
        if (i == s->size) break;
    }
}

static void	init_snake(t_snake *s, char **argv) {
	struct winsize ws;
	*s = (t_snake){.size = 1, .width = atoi(argv[1]), .height = atoi(argv[2]), .delay = DELAY};
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
	s->width = (s->width > ws.ws_col - 2) ? ws.ws_col - 2 : s->width;
	s->height = (s->height > ws.ws_row - 6) ? ws.ws_row - 6 : s->height;
	s->width = (s->width > MAX_WIDTH) ? MAX_WIDTH : s->width;
	s->height = (s->height > MAX_HEIGHT) ? MAX_HEIGHT : s->height;
	s->x[0] = s->width >> 1;
	s->y[0] = s->height >> 1;
	srand(time(NULL));
	spawn_fruit(s);
}

static void	init_terminal(t_snake *s) {
	struct termios gameMode;
	tcgetattr(STDIN_FILENO, &s->savedTerm);
	gameMode = s->savedTerm;
	gameMode.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &gameMode);
}

static void	init_display(t_snake *s) {
	printf("\033[2J\033[H\033[?25l");
	for (int i = 0; i < s->width + 1; i++)
		printf("█");
	for (int i = 0; i < s->height; i++)
		for (int j = 0; j < s->width; j++)
			printf("%s ", (!j) ? "█\n█" : "");
	for (int i = 0; i < s->width + 2; i++)
		printf("%s█", (!i) ? "█\n" : "");
	printf("\nScore:\nUse WASD to move, X to quit");
}

void	init_game(t_snake *s, char **argv) {
	init_snake(s, argv);
	init_display(s);
	init_terminal(s);
}
