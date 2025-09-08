#include "minisnake.h"

struct termios	g_savedTerm;

static void	handleInput(t_data *d) {
	int oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
	char keys[] = "aAwWdDsS", dirs[] = {LEFT,LEFT,UP,UP,RIGHT,RIGHT,DOWN,DOWN};
	char *pos;
	
	d->dir[1] = d->dir[0];
	fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
	int c = getchar();
	fcntl(STDIN_FILENO, F_SETFL, oldf);
	if (c == EOF) return;
	if (c == 'x' || c == 'X')
		d->gameOver = 1;
	else if ((pos = strchr(keys, c))) {
		if ((dirs[pos - keys] + 1) >> 1 != (d->dir[0] + 1) >> 1)
			d->dir[0] = dirs[pos - keys];
	}
}

void	spawnFruit(t_data *d) {
	int	i;

	do {
		d->fruitX = rand() % d->width;
		d->fruitY = rand() % d->height;
		for (i = 0; i < d->size && !(d->x[i] == d->fruitX && d->y[i] == d->fruitY); i++);
	} while (i < d->size);
}

static void	handleLogic(t_data *d) {
	if (d->grow && d->grow--)
		d->size++;
	for (int i = d->size; i; i--) {
		d->x[i] = d->x[i - 1];
		d->y[i] = d->y[i - 1];
	}
	d->x[0] += (d->dir[0] == RIGHT) - (d->dir[0] == LEFT);
	d->y[0] += (d->dir[0] == DOWN) - (d->dir[0] == UP);
	if (d->x[0] < 0 || d->x[0] == d->width || d->y[0] < 0 || d->y[0] == d->height)
		d->gameOver = 1;
	for (int i = 1; i < d->size; i++)
		if (d->x[i] == d->x[0] && d->y[i] == d->y[0])
			d->gameOver = 1;
	if (d->x[0] != d->fruitX || d->y[0] != d->fruitY)
		return;
	if (d->size < d->width * d->height)
		spawnFruit(d);
	d->grow++;
	d->score += 10;
	d->delay *= SPEEDUP_FACTOR;
}

static void	updateDisplay(t_data *d) {
	const char *head[] = {"🭎", "🭨", "🭪", "🭩", "🭫"};
	const char *corner[] = {"▗", "▘"};

	printf(CURSOR_POS " ", d->y[d->size] + 2, d->x[d->size] + 2);
	printf(CURSOR_POS "@", d->fruitY + 2, d->fruitX + 2);
	if (d->size > 1)
		printf(CURSOR_POS "%s", d->y[1] + 2, d->x[1] + 2,
			(d->dir[0] + d->dir[1] == 5) ? corner[(d->dir[0] % 2)] : "▚");
	printf(CURSOR_POS "%s", d->y[0] + 2, d->x[0] + 2, head[d->dir[0]]);
	printf(CURSOR_POS "%d\n\n\n", d->height + 3, 8, d->score);
	fflush(stdout);
}

int	main(int argc, char **argv) {
	t_data d;

	if (argc != 3)
		return (fprintf(stderr, "Usage: ./minisnake width height\n"), 2);
	initGame(&d, argv);
	while (!d.gameOver && d.size < d.width * d.height) {
		handleInput(&d);
		handleLogic(&d);
		updateDisplay(&d);
		usleep(d.delay);
	}
	printf(CURSOR_POS "%-32s\n\n" COLOR_RESET CURSOR_SHOW, d.height + 4, 1,
		d.gameOver ? COLOR_RED "GAME OVER" : COLOR_GREEN "CONGRATULATIONS! YOU WON!");
	tcsetattr(STDIN_FILENO, TCSANOW, &g_savedTerm);
	return 0;
}
