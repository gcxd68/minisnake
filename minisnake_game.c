#include "minisnake.h"

#ifndef ONLINE_BUILD

/* Stubs for offline builds */
static void	anticheat(t_data *d) { (void)d; }

#else

static long get_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void anticheat(t_data *d) {
	static int	counter = 0;
	static int	last_score = 0;
	static long	last_frame = 0;
	const long	now = get_ms();

	if (d->cheat) return;

	/* Reset local static state at the start of a new game */
	if (d->score == 0 && d->size == DEF_INITIAL_SIZE) {
		last_score = 0;
		last_frame = now;
		counter = 0;
	}

	/* Validate score progression dynamically */
	if ((d->score != last_score && d->score != last_score + d->points_per_fruit)
		|| (d->points_per_fruit && d->score % d->points_per_fruit)
		|| d->score > (d->width * d->height * d->points_per_fruit)
		|| now - last_frame > d->cheat_timeout) {
		d->cheat = 1;
		notify_server(d, "cheat"); /* Alert the server immediately in the background */
		return;
	}
	last_score = d->score;
	last_frame = now;

	/* External Debugger Detection */
	if (++counter > 10) {
		FILE    *f = fopen("/proc/self/status", "r");
		char    buf[256];

		counter = 0;
		if (!f) return;
		while (fgets(buf, sizeof(buf), f)) {
			if (!strncmp(buf, "TracerPid:", 10) && atoi(buf + 10) != 0) {
				d->cheat = 1;
				notify_server(d, "cheat"); /* Alert the server */
				break;
			}
		}
		fclose(f);
	}
}

#endif

static void	process_input(t_data *d) {
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
		if ((pos - keys + 2) >> 1 != (d->dir[0] + 1) >> 1)
			d->dir[0] = pos - keys + 1;
	for (i = 0; i < INPUT_Q_SIZE; i++)
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
		return ;
	if (d->size < d->width * d->height)
		spawn_fruit(d);
	d->grow = 1;
	d->score += d->points_per_fruit;
	d->delay *= d->speedup_factor;
	notify_server(d, "eat");
}

const char *fruit_color(void) {
	static const char *palette[] = FRUIT_PALETTE;
	return palette[rand() % ARR_SIZE(palette)];
}

static void	render(t_data *d) {
	static const char	*heads[] = SNAKE_HEADS;
	static const char	*bends[] = SNAKE_BENDS;

	if (!d->dir[0]) return ;
	if (d->x[d->size] != d->fruit_x || d->y[d->size] != d->fruit_y)
		printf(CURSOR_POS " ", d->y[d->size] + 2, d->x[d->size] + 2);
	if (d->size > 1)
		printf(SNAKE_COLOR CURSOR_POS "%s", d->y[1] + 2, d->x[1] + 2,
			(d->dir[0] + d->dir[1] == 5) ? bends[(d->dir[0] % 2)] : SNAKE_BODY);
	if (d->grow)
		printf(CURSOR_POS "%s" STYLE_BOLD FRUIT_CHAR STYLE_RESET CURSOR_POS "%d",
			d->fruit_y + 2, d->fruit_x + 2, fruit_color(), d->height + 3, 8, d->score);
	printf(SNAKE_COLOR CURSOR_POS "%s", d->y[0] + 2, d->x[0] + 2, heads[d->dir[0] - 1]);
	printf(STYLE_RESET CURSOR_POS "\n", d->height + 3, 1);
}

void	game_loop(t_data *d) {
	while (!d->game_over && d->size < d->width * d->height) {
		anticheat(d);
		process_input(d);
		update_game(d);
		render(d);
		usleep(d->delay);
	}
}
