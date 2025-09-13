#include "minisnake.h"

static void	handleInput(t_data *d) {
	const char	keys[] = "adws";
	char		*pos;
	int			c, i;

	d->dir[1] = d->dir[0];
	for (i = 0; d->inputQ[i] != EOF; i++);
	while ((c = getchar()) != EOF)
		if (i < INPUT_QUEUE_SIZE)
			d->inputQ[i++] = c;
	if (tolower(d->inputQ[0]) == 'x')
		d->gameOver = 1;
	else if ((pos = strchr(keys, tolower(d->inputQ[0]))))
		if ((pos - keys + 2) >> 1 != (d->dir[0] + 1) >> 1)
			d->dir[0] = pos - keys + 1;
	for (i = 0; i < INPUT_QUEUE_SIZE; i++)
		d->inputQ[i] = d->inputQ[i + 1];
}

void	spawnFruit(t_data *d) {
	int	i;

	do {
		d->fruitX = rand() % d->width;
		d->fruitY = rand() % d->height;
		for (i = 0; i < d->sSize && !(d->x[i] == d->fruitX && d->y[i] == d->fruitY); i++);
	} while (i < d->sSize);
}

static void	handleLogic(t_data *d) {
	static int	grow = 0;

	if (grow && grow--)
		d->sSize++;
	for (int i = d->sSize; i; i--) {
		d->x[i] = d->x[i - 1];
		d->y[i] = d->y[i - 1];
	}
	d->x[0] += (d->dir[0] == RIGHT) - (d->dir[0] == LEFT);
	d->y[0] += (d->dir[0] == DOWN) - (d->dir[0] == UP);
	if (d->x[0] < 0 || d->x[0] == d->width || d->y[0] < 0 || d->y[0] == d->height)
		d->gameOver = 1;
	for (int i = 1; i < d->sSize; i++)
		if (d->x[i] == d->x[0] && d->y[i] == d->y[0])
			d->gameOver = 1;
	if (d->x[0] != d->fruitX || d->y[0] != d->fruitY)
		return;
	if (d->sSize < d->width * d->height)
		spawnFruit(d);
	grow++;
	d->score += 10;
	d->delay *= SPEEDUP_FACTOR;
}

static void	updateDisplay(t_data *d) {
	const char *head[] = {"🭎", "🭨", "🭪", "🭩", "🭫"};
	const char *corner[] = {"▗", "▘"};

	printf(CURSOR_POS " ", d->y[d->sSize] + 2, d->x[d->sSize] + 2);
	printf(CURSOR_POS "@", d->fruitY + 2, d->fruitX + 2);
	if (d->sSize > 1)
		printf(CURSOR_POS "%s", d->y[1] + 2, d->x[1] + 2,
			(d->dir[0] + d->dir[1] == 5) ? corner[(d->dir[0] % 2)] : "▚");
	printf(CURSOR_POS "%s", d->y[0] + 2, d->x[0] + 2, head[d->dir[0]]);
	printf(CURSOR_POS "%d\n\n\n", d->height + 3, 8, d->score);
	fflush(stdout);
}

int	main(int argc, char **argv) {
	t_data	d;

	if (argc != 3)
		return (fprintf(stderr, "Usage: ./minisnake width height\n"), 2);
	d = (t_data){.width = atoi(argv[1]), .height = atoi(argv[2])};
	if (d.height < 2 || d.width < 2)
		return(fprintf(stderr, "Error: dimensions must be positive integers greater than 1\n"), 2);
	init(&d);
	while (!d.gameOver && d.sSize < d.width * d.height) {
		handleInput(&d);
		handleLogic(&d);
		updateDisplay(&d);
		usleep(d.delay);
	}
	printf(CURSOR_POS "%-32s\n\n" COLOR_RESET, d.height + 4, 1,
		d.gameOver ? COLOR_RED "GAME OVER" : COLOR_GREEN "CONGRATULATIONS! YOU WON!");
		cleanup();
	return 0;
}
