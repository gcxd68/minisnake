#include "minisnake.h"

static void	process_input(t_data *d) {
	const char	keys[] = KEYS;
	char		*pos;
	int			c, i;

	d->dir[1] = d->dir[0];
	for (i = 0; d->input_q[i] != EOF; i++);
	while ((c = getchar()) != EOF)
		if (i < INPUT_QUEUE_SIZE)
			d->input_q[i++] = c;
	if (toupper(d->input_q[0]) == 'X')
		d->game_over = 1;
	else if ((pos = strchr(keys, toupper(d->input_q[0]))))
		if ((pos - keys + 2) >> 1 != (d->dir[0] + 1) >> 1)
			d->dir[0] = pos - keys + 1;
	for (i = 0; i < INPUT_QUEUE_SIZE; i++)
		d->input_q[i] = d->input_q[i + 1];
}

void	spawn_fruit(t_data *d) {
	int	i;

	do {
		d->fruit_x = rand() % d->width;
		d->fruit_y = rand() % d->height;
		for (i = 0; i < d->size && !(d->x[i] == d->fruit_x && d->y[i] == d->fruit_y); i++);
	} while (i < d->size);
}

static void	update_game(t_data *d) {
	if (d->grow && d->grow--)
		d->size++;
	memmove(d->x + 1, d->x, d->size * sizeof(*d->x));
	memmove(d->y + 1, d->y, d->size * sizeof(*d->y));
	d->x[0] += (d->dir[0] == RIGHT) - (d->dir[0] == LEFT);
	d->y[0] += (d->dir[0] == DOWN) - (d->dir[0] == UP);
	if (d->x[0] < 0 || d->x[0] == d->width || d->y[0] < 0 || d->y[0] == d->height)
		d->game_over = 1;
	for (int i = 1; i < d->size; i++)
		if (d->x[i] == d->x[0] && d->y[i] == d->y[0])
			d->game_over = 1;
	if (d->x[0] != d->fruit_x || d->y[0] != d->fruit_y)
		return;
	if (d->size < d->width * d->height)
		spawn_fruit(d);
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
	if (d->x[d->size] != d->fruit_x || d->y[d->size] != d->fruit_y)
		printf(CURSOR_POS " ", d->y[d->size] + 2, d->x[d->size] + 2);
	if (d->size > 1)
		printf(CURSOR_POS "%s", d->y[1] + 2, d->x[1] + 2,
			(d->dir[0] + d->dir[1] == 5) ? corner[(d->dir[0] % 2)] : "▚");
	if (d->grow)
		printf(CURSOR_POS "@" CURSOR_POS "%d",
			d->fruit_y + 2, d->fruit_x + 2, d->height + 3, 8, d->score);
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
	if (*endptr || width < MIN_WIDTH || width > MAX_WIDTH)
		error += fprintf(stderr, "minisnake: width must be an integer between %d and %d\n", MIN_WIDTH, MAX_WIDTH);
	long height = strtol(argv[2], &endptr, 10);
	if (*endptr || height < MIN_HEIGHT || height > MAX_HEIGHT)
		error += fprintf(stderr, "minisnake: height must be an integer between %d and %d\n", MIN_HEIGHT, MAX_HEIGHT);
	if (error) return 2;
	t_data d = (t_data){.width = (int)width, .height = (int)height};
	initialize(&d);
	while (!d.game_over && d.size < d.width * d.height) {
		process_input(&d);
		update_game(&d);
		render(&d);
		usleep(d.delay);
	}
	printf("%-32s", d.game_over ? COLOR_RED "GAME OVER" : COLOR_GREEN "CONGRATULATIONS! YOU WON!");
	clean_exit(EXIT_SUCCESS);
}
