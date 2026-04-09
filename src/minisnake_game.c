#include "minisnake.h"

#ifndef ONLINE_BUILD

/* Stubs for offline builds */
static void	anticheat(t_data *d) { (void)d; }

#else

# define DEBUG_CHECK_FREQ	10
# define PROC_STATUS_PATH	"/proc/self/status"
# define TRACER_LEN			10
# define PROC_BUF_SIZE		256

static long get_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static void anticheat(t_data *d) {
	static int	counter = 0;
	static long	last_frame = 0;
	const long	now = get_ms();

	if (d->cheat) return;

	/* Reset local static state at the start of a new game */
	if (!d->dir[0]) {
		last_frame = now;
		counter = 0;
		return;
	}

	/* Validate score progression dynamically */
	int current_penalty = (d->penalty_interval > 0) ? (d->steps / d->penalty_interval) * d->penalty_amount : 0;
	if (d->score < 0
		|| d->score > d->width * d->height * d->points_per_fruit - current_penalty
		|| now - last_frame > d->cheat_timeout) {
		d->cheat = 1;
		notify_server(d, "cheat");
		return;
	}
	last_frame = now;

	/* External Debugger Detection */
	if (++counter > 10) {
		FILE    *f = fopen(PROC_STATUS_PATH, "r");
		char    buf[PROC_BUF_SIZE];

		counter = 0;
		if (!f) return;
		while (fgets(buf, sizeof(buf), f)) {
			if (!strncmp(buf, "TracerPid:", TRACER_LEN) && atoi(buf + 10) != 0) {
				d->cheat = 1;
				notify_server(d, "cheat");
				break;
			}
		}
		fclose(f);
	}
}

#endif

static void process_input(t_data *d) {
	static const char	move_keys[] = MOVE_KEYS;
	static const char	arrow_keys[] = ARROW_KEYS;
	char				*pos;
	int					c, i;

	d->dir[1] = d->dir[0];
	for (i = 0; d->input_q[i] != EOF; i++);
	while ((c = getchar()) != EOF) {
		c = (c == '\033' && getchar() == '[') ? getchar() + 256 : toupper(c);
		if (i < INPUT_Q_SIZE) d->input_q[i++] = c;
	}
	if ((c = d->input_q[0]) == *EXIT_KEY)
		d->game_over = 1;
	const char *base = (c > 255) ? arrow_keys : move_keys;
	pos = strchr(base, (c > 255) ? c - 256 : c);
	if (pos && (pos - base + 2) >> 1 != (d->dir[0] + 1) >> 1)
		d->dir[0] = pos - base + 1;
	for (i = 0; i < INPUT_Q_SIZE; i++)
		d->input_q[i] = d->input_q[i + 1];
}

void spawn_fruit(t_data *d) {
	int i, attempts = 0;

	do {
		d->fruit_x = (lcg_rand(&d->seed) >> 16) % d->width;
		d->fruit_y = (lcg_rand(&d->seed) >> 16) % d->height;
		for (i = 0; i < d->size && !(d->x[i] == d->fruit_x && d->y[i] == d->fruit_y); i++);
		if (++attempts > d->spawn_fruit_max_attempts) break;
	} while (i < d->size);
}

static void	update_game(t_data *d) {
	if (!d->dir[0]) return;
	d->steps++;
	if (d->penalty_interval > 0 && d->steps % d->penalty_interval == 0)
		d->score -= d->penalty_amount;
	d->score = MAX(d->score, 0);
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
	d->grow = 1;
	d->score += d->points_per_fruit;
	d->delay *= d->speedup_factor;
	notify_server(d, "eat");
	if (d->size >= d->width * d->height)
		return;
	spawn_fruit(d);
}

const char *fruit_color(void) {
	static const char *palette[] = FRUIT_PALETTE;
	return palette[sys_rand() % ARR_SIZE(palette)];
}

static void	render(t_data *d) {
	static const char	*heads[] = SNAKE_HEADS;
	static const char	*bends[] = SNAKE_BENDS;

	if (!d->dir[0]) return;
	if ((d->x[d->size] != d->fruit_x || d->y[d->size] != d->fruit_y) &&
		(d->x[d->size] != d->x[d->size - 1] || d->y[d->size] != d->y[d->size - 1]))
		printf(CURSOR_POS " ", d->y[d->size] + 2, d->x[d->size] + 2);
	if (d->size > 1)
		printf(SNAKE_COLOR CURSOR_POS "%s", d->y[1] + 2, d->x[1] + 2,
			(d->dir[0] + d->dir[1] == 5) ? bends[(d->dir[0] % 2)] : SNAKE_BODY);
	if (d->grow)
		printf(CURSOR_POS "%s" STYLE_BOLD FRUIT_CHAR STYLE_RESET,
			d->fruit_y + 2, d->fruit_x + 2, fruit_color());
	printf(SNAKE_COLOR CURSOR_POS "%s", d->y[0] + 2, d->x[0] + 2, heads[d->dir[0] - 1]);
	printf(STYLE_RESET CURSOR_POS "%d \n", d->height + 3, 8, d->score);
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
