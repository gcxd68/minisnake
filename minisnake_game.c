#include "minisnake.h"

#ifndef ONLINE_BUILD

/* Stub: anticheat is disabled in offline builds */
static void	anticheat(t_data *d)
{
	(void)d;
}

#else

static void	anticheat(t_data *d)
{
	const time_t	now = time(NULL);

	if (now - d->last_frame > 2)
		d->cheat = 1;
	d->last_frame = now;
}

static void	process_input(t_data *d)
{
	static const char	keys[] = MOVE_KEYS;
	char				*pos;
	int					c, i;

	d->dir[1] = d->dir[0];
	for (i = 0; d->input_q[i] != EOF; i++);
	while ((c = getchar()) != EOF)
		if (i < INPUT_Q_SIZE)
			d->input_q[i++] = c;
	if (toupper(d->input_q[0]) == *EXIT_KEY)
		d->game_over = 1;
	else if ((pos = strchr(keys, toupper(d->input_q[0]))))
		/* Bitshift trick: group directions into pairs (LEFT/RIGHT = 1, UP/DOWN = 2)
		   to prevent reversing direction (e.g. LEFT while going RIGHT) */
		if ((pos - keys + 2) >> 1 != (d->dir[0] + 1) >> 1)
			d->dir[0] = pos - keys + 1;
	for (i = 0; i < INPUT_Q_SIZE; i++)
		d->input_q[i] = d->input_q[i + 1];
}

void	spawn_fruit(t_data *d)
{
	int	i;

	/* Keep re-rolling until the fruit lands on an empty cell */
	do {
		d->fruit_x = rand() % d->width;
		d->fruit_y = rand() % d->height;
		for (i = 0;
			i < d->size && !(d->x[i] == d->fruit_x && d->y[i] == d->fruit_y);
			i++);
	} while (i < d->size);
}

static void	update_game(t_data *d)
{
	if (d->grow && d->grow--)
		d->size++;
	/* Shift the body segments forward to make room for the new head position */
	memmove(d->x + 1, d->x, d->size * sizeof(*d->x));
	memmove(d->y + 1, d->y, d->size * sizeof(*d->y));
	d->x[0] += (d->dir[0] == RIGHT) - (d->dir[0] == LEFT);
	d->y[0] += (d->dir[0] == DOWN) - (d->dir[0] == UP);
	if (d->x[0] < 0 || d->x[0] == d->width
		|| d->y[0] < 0 || d->y[0] == d->height)
		d->game_over = 1;
	for (int i = 1; i < d->size; i++)
		if (d->x[i] == d->x[0] && d->y[i] == d->y[0])
			d->game_over = 1;
	if (d->x[0] != d->fruit_x || d->y[0] != d->fruit_y)
		return ;
	if (d->size < d->width * d->height)
		spawn_fruit(d);
	d->grow = 1;
	/* Decode, increment, re-encode score in one expression */
	d->score = MASK_SCORE((REAL_SCORE + 10));
	d->delay *= SPEEDUP_FACTOR;
}

void draw_fruit(t_data *d)
{
	static const char	*fruit_palette[] = FRUIT_PALETTE;
	const char			*fruit_color = fruit_palette[rand() % ARR_SIZE(fruit_palette)];

	printf(CURSOR_POS "%s" STYLE_BOLD FRUIT_CHAR STYLE_RESET,
		   d->fruit_y + 2, d->fruit_x + 2, fruit_color);
}

static void	render(t_data *d)
{
	static const char	*heads[] = SNAKE_HEADS;
	static const char	*bends[] = SNAKE_BENDS;

	if (!d->dir[0])
		return ;
	/* Erase the tail cell unless the fruit happens to be there */
	if (d->x[d->size] != d->fruit_x || d->y[d->size] != d->fruit_y)
		printf(CURSOR_POS " ", d->y[d->size] + 2, d->x[d->size] + 2);
	/* Draw the second segment as a bend or straight body piece */
	if (d->size > 1)
		printf(SNAKE_COLOR CURSOR_POS "%s", d->y[1] + 2, d->x[1] + 2,
			(d->dir[0] + d->dir[1] == 5) ? bends[(d->dir[0] % 2)] : SNAKE_BODY);
	/* Redraw fruit and score only when the snake just ate (grow flag set) */
	if (d->grow) {
		draw_fruit(d);
		printf(CURSOR_POS "%d", d->height + 3, 8, REAL_SCORE);
	}
	printf(SNAKE_COLOR CURSOR_POS "%s", d->y[0] + 2, d->x[0] + 2, heads[d->dir[0] - 1]);
	printf(STYLE_RESET CURSOR_POS "\n", d->height + 3, 1);
}

void	game_loop(t_data *d)
{
	while (!d->game_over && d->size < d->width * d->height)
	{
		anticheat(d);
		process_input(d);
		update_game(d);
		render(d);
		usleep(d->delay);
	}
}

#endif
