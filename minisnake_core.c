#include "minisnake.h"

struct termios	g_savedTerm;

static int	keyboardHit(void) {
	int	ch, oldf;

	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
	ch = getchar();
	fcntl(STDIN_FILENO, F_SETFL, oldf);
	ungetc(ch, stdin);
	return (ch != EOF) ? 1 : 0;
}

static void	handleInput(t_snake *s) {
	s->dir[1] = s->dir[0];
	if (!keyboardHit())
		return;
	char input = getchar();
	if ((input == 'a' || input == 'A') && s->dir[0] != RIGHT) s->dir[0] = LEFT;
	else if ((input == 'd' || input == 'D') && s->dir[0] != LEFT) s->dir[0] = RIGHT;
	else if ((input == 'w' || input == 'W') && s->dir[0] != DOWN) s->dir[0] = UP;
	else if ((input == 's' || input == 'S') && s->dir[0] != UP) s->dir[0] = DOWN;
	else if (input == 'x' || input == 'X') s->gameOver = 1;
}

static void	handleLogic(t_snake *s) {
	if (s->grow && s->grow--)
		s->size++;
	for (int i = s->size; i > 0; i--) {
		s->x[i] = s->x[i - 1];
		s->y[i] = s->y[i - 1];
	}
	s->x[0] += (s->dir[0] == RIGHT) - (s->dir[0] == LEFT);
	s->y[0] += (s->dir[0] == DOWN) - (s->dir[0] == UP);
	if (s->x[0] < 0 || s->x[0] == s->width || s->y[0] < 0 || s->y[0] == s->height)
		s->gameOver = 1;
	for (int i = 1; i < s->size; i++)
		if (s->x[i] == s->x[0] && s->y[i] == s->y[0])
			s->gameOver = 1;
	if (s->x[0] != s->fruitX || s->y[0] != s->fruitY)
		return;
	if (s->size < s->width * s->height)
		spawnFruit(s);
	s->grow++;
	s->score += 10;
	s->delay *= SPEEDUP_FACTOR;
}

static void	updateDisplay(t_snake *s) {
	const char *head[] = {"🭎", "🭨", "🭪", "🭩", "🭫"};
	const char *corner[] = {"▗", "▘"};

	printf(CURSOR_POS " ", s->y[s->size] + 2, s->x[s->size] + 2);
	printf(CURSOR_POS "@", s->fruitY + 2, s->fruitX + 2);
	printf(CURSOR_POS "%s", s->y[0] + 2, s->x[0] + 2, head[s->dir[0]]);
	if (s->size > 1) printf(CURSOR_POS "%s", s->y[1] + 2, s->x[1] + 2,
		(s->dir[0] + s->dir[1] == 5) ? corner[(s->dir[0] % 2)] : "▚");
	printf(CURSOR_POS "%d\n\n\n", s->height + 3, 8, s->score);
	fflush(stdout);
}

int	main(int argc, char **argv) {
	t_snake	s;

	if (argc != 3)
		return (fprintf(stderr, "Usage: ./snake width height\n"), 2);
	initGame(&s, argv);
	while (!s.gameOver && s.size < s.width * s.height) {
		handleInput(&s);
		handleLogic(&s);
		updateDisplay(&s);
		usleep(s.delay);
	}
	printf(CURSOR_POS "%-32s\n\n" COLOR_RESET CURSOR_SHOW, s.height + 4, 1,
		s.gameOver ? COLOR_RED "GAME OVER" : COLOR_GREEN "CONGRATULATIONS! YOU WON!");
	tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerm);
	return 0;
}
