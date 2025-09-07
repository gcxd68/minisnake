#include "minisnake.h"

struct termios	g_savedTerm;

static void	update_display(t_snake *s) {
	printf(CURSOR_POS " ", s->y[s->size] + 2, s->x[s->size] + 2);
	printf(CURSOR_POS "♦", s->fruitY + 2, s->fruitX + 2);
	printf(CURSOR_POS "●", s->y[0] + 2, s->x[0] + 2);
	if (s->size > 1) printf(CURSOR_POS "o", s->y[1] + 2, s->x[1] + 2);
	printf(CURSOR_POS "%d\n", s->height + 3, 8, s->score);
	fflush(stdout);
}

static int	kbhit(void) {
	int	ch, oldf;

	oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
	ch = getchar();
	fcntl(STDIN_FILENO, F_SETFL, oldf);
	ungetc(ch, stdin);
	return (ch != EOF) ? 1 : 0;
}

static void	handle_input(t_snake *s) {
	if (!kbhit())
		return;
	char input = getchar();
	if ((input == 'a' || input == 'A') && s->dir != RIGHT) s->dir = LEFT;
	else if ((input == 'd' || input == 'D') && s->dir != LEFT) s->dir = RIGHT;
	else if ((input == 'w' || input == 'W') && s->dir != DOWN) s->dir = UP;
	else if ((input == 's' || input == 'S') && s->dir != UP) s->dir = DOWN;
	else if (input == 'x' || input == 'X') s->gameOver = 1;
}

static void	handle_logic(t_snake *s) {
	if (s->grow && s->grow--)
		s->size++;
	for (int i = s->size; i > 0; i--) {
		s->x[i] = s->x[i - 1];
		s->y[i] = s->y[i - 1];
	}
	s->x[0] += (s->dir == RIGHT) - (s->dir == LEFT);
	s->y[0] += (s->dir == DOWN) - (s->dir == UP);
	if (s->x[0] < 0 || s->x[0] == s->width || s->y[0] < 0 || s->y[0] == s->height)
		s->gameOver = 1;
	for (int i = 1; i < s->size; i++)
		if (s->x[i] == s->x[0] && s->y[i] == s->y[0])
			s->gameOver = 1;
	if (s->x[0] != s->fruitX || s->y[0] != s->fruitY)
		return;
	spawn_fruit(s);
	s->grow++;
	if (!((s->score += 10) % 100) && s->score)
		s->delay *= 0.9f;
}

int	main(int argc, char **argv) {
	t_snake	s;

	if (argc != 3)
		return (fprintf(stderr, "Usage: ./snake width height\n"), 2);
	init_game(&s, argv);
	while (!s.gameOver) {
		update_display(&s);
		handle_input(&s);
		handle_logic(&s);
		usleep(s.delay);
	}
	printf(CURSOR_POS COLOR_RED "Game Over!\n" COLOR_RESET CURSOR_SHOW, s.height + 5, 1);
	tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerm);
	return 0;
}
