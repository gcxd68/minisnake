#include "minisnake.h"

static void	processInput(t_data *d) {
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
		for (i = 0; i < d->size && !(d->x[i] == d->fruitX && d->y[i] == d->fruitY); i++);
	} while (i < d->size);
}

static void	updateGame(t_data *d) {
	if (d->grow && d->grow--)
		d->size++;
	memmove(d->x + 1, d->x, d->size * sizeof(*d->x));
	memmove(d->y + 1, d->y, d->size * sizeof(*d->y));
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
	if (!d->dir[0])
		return;
	d->grow = 1;
	d->score += 10;
	d->delay *= SPEEDUP_FACTOR;
}

static void	render(t_data *d) {
	const char *head[] = {"🭨", "🭪", "🭩", "🭫"};
	const char *corner[] = {"▗", "▘"};

	if (!d->dir[0])
		return;
	if (d->x[d->size] != d->fruitX || d->y[d->size] != d->fruitY)
		printf(CURSOR_POS " ", d->y[d->size] + 2, d->x[d->size] + 2);
	if (d->size > 1)
		printf(CURSOR_POS "%s", d->y[1] + 2, d->x[1] + 2,
			(d->dir[0] + d->dir[1] == 5) ? corner[(d->dir[0] % 2)] : "▚");
	if (d->grow)
		printf(CURSOR_POS "@" CURSOR_POS "%d",
			d->fruitY + 2, d->fruitX + 2, d->height + 3, 8, d->score);
	printf(CURSOR_POS "%s", d->y[0] + 2, d->x[0] + 2, head[d->dir[0] - 1]);
	printf(CURSOR_POS "\n", d->height + 3, 1);
}

int	main(int argc, char **argv) {
	char	*endptr;
	int		error = 0;

	if (SPEEDUP_FACTOR < 0.0f || SPEEDUP_FACTOR >= 1.0f)
		return (fprintf(stderr, "Error: SPEEDUP_FACTOR must be >= 0.0 and < 1.0\n"), EXIT_FAILURE);
	if (argc != 3)
		return (fprintf(stderr, "Usage: ./minisnake width height\n"), 2);
	long width = strtol(argv[1], &endptr, 10);
	if (*endptr || width < 2 || width < MIN_WIDTH || width > MAX_WIDTH)
		error += fprintf(stderr, "minisnake: width must be an integer between %d and %d\n", MIN_WIDTH, MAX_WIDTH);
	long height = strtol(argv[2], &endptr, 10);
	if (*endptr || height < 2 || height < MIN_HEIGHT || height > MAX_HEIGHT)
		error += fprintf(stderr, "minisnake: height must be an integer between %d and %d\n", MIN_HEIGHT, MAX_HEIGHT);
	if (error) return 2;
	t_data d = (t_data){.width = (int)width, .height = (int)height};
	initialize(&d);
	while (!d.gameOver && d.size < d.width * d.height) {
		processInput(&d);
		updateGame(&d);
		render(&d);
		usleep(d.delay);
	}
	printf("%-32s", d.gameOver ? COLOR_RED "GAME OVER" : COLOR_GREEN "CONGRATULATIONS! YOU WON!");
	cleanup();
	return EXIT_SUCCESS;
}
