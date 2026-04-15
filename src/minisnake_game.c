#include "minisnake.h"

#ifndef ONLINE_BUILD

static void	anticheat(t_data *d) { (void)d; }

#else

# define DEBUG_CHECK_FREQ	10
# define PROC_STATUS_PATH	"/proc/self/status"
# define TRACER_LEN			10
# define PROC_BUF_SIZE		256

/* PREPROCESSOR CHECKS: Compile-time safety validation */
# if DEBUG_CHECK_FREQ <= 0
#  error "DEBUG_CHECK_FREQ must be strictly positive"
# endif
# if TRACER_LEN != 10
#  error "TRACER_LEN must match the length of 'TracerPid:' (10)"
# endif
# if PROC_BUF_SIZE <= TRACER_LEN
#  error "PROC_BUF_SIZE must be strictly greater than TRACER_LEN"
# endif

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

	if (!d->dir[0]) {
		last_frame = now;
		counter = 0;
		return;
	}

	int current_penalty = (d->penalty_interval > 0) ? (d->steps / d->penalty_interval) * d->penalty_amount : 0;
	if (d->score < 0
		|| d->score > d->width * d->height * d->points_per_fruit - current_penalty
		|| now - last_frame > d->cheat_timeout) {
		d->cheat = 1;
		notify_server(d, "cheat", 0, 0);
		return;
	}
	last_frame = now;

	if (++counter > 10) {
		FILE    *f = fopen(PROC_STATUS_PATH, "r");
		char    buf[PROC_BUF_SIZE];

		counter = 0;
		if (!f) return;
		while (fgets(buf, sizeof(buf), f)) {
			if (!strncmp(buf, "TracerPid:", TRACER_LEN) && atoi(buf + 10) != 0) {
				d->cheat = 1;
				notify_server(d, "cheat", 0, 0);
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
	int cand_x, cand_y;

	do {
		cand_x = (lcg_rand(&d->seed) >> 16) % d->width;
		cand_y = (lcg_rand(&d->seed) >> 16) % d->height;
		for (i = 0; i < d->size && !(d->x[i] == cand_x && d->y[i] == cand_y); i++);
		if (++attempts > d->spawn_fruit_max_attempts) break;
	} while (i < d->size);

	/* Safe Write for local generation */
	pthread_mutex_lock(&d->fruit_mutex);
	d->fruit_x = cand_x;
	d->fruit_y = cand_y;
	d->fruit_col = fruit_color();
	pthread_mutex_unlock(&d->fruit_mutex);
}

static void	update_game(t_data *d) {
	if (!d->dir[0]) return;

	const char *moves = " LRUD";
	if (d->path_steps < 10000) {
		d->path[d->path_steps] = moves[d->dir[0]];
	}
	d->path_steps++;

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

	/* Safe Read */
	pthread_mutex_lock(&d->fruit_mutex);
	int fx = d->fruit_x;
	int fy = d->fruit_y;
	pthread_mutex_unlock(&d->fruit_mutex);

	if (d->x[0] != fx || d->y[0] != fy)
		return ;

	d->grow = 1;
	d->score += d->points_per_fruit;
	d->delay *= d->speedup_factor;

	/* Notifier avec les vraies coordonnées avant de les écraser */
	d->seq++;
	notify_server(d, "eat", fx, fy);

	d->path_steps = 0;
	memset(d->path, 0, sizeof(d->path));

	/* Safe Hide */
	pthread_mutex_lock(&d->fruit_mutex);
	d->fruit_x = -1;
	d->fruit_y = -1;
	pthread_mutex_unlock(&d->fruit_mutex);

	if (d->size >= d->width * d->height)
		return;
		
	if (!d->online) {
		spawn_fruit(d);
	}
}

const char *fruit_color(void) {
	static const char *palette[] = FRUIT_PALETTE;
	return palette[sys_rand() % ARR_SIZE(palette)];
}

static void	render(t_data *d) {
	static const char	*heads[] = SNAKE_HEADS;
	static const char	*bends[] = SNAKE_BENDS;
	int fx, fy;
	int fruit_hidden = 0;

	if (!d->dir[0]) return;

	/* Safe Read */
	pthread_mutex_lock(&d->fruit_mutex);
	fx = d->fruit_x;
	fy = d->fruit_y;
	pthread_mutex_unlock(&d->fruit_mutex);

	if ((d->x[d->size] != fx || d->y[d->size] != fy) &&
		(d->x[d->size] != d->x[d->size - 1] || d->y[d->size] != d->y[d->size - 1]))
		printf(CURSOR_POS " ", d->y[d->size] + 2, d->x[d->size] + 2);
		
	if (d->size > 1)
		printf(SNAKE_COLOR CURSOR_POS "%s", d->y[1] + 2, d->x[1] + 2,
			(d->dir[0] + d->dir[1] == 5) ? bends[(d->dir[0] % 2)] : SNAKE_BODY);
			
	if (fx >= 0 && fy >= 0) {
		for (int i = 0; i < d->size; i++) {
			if (d->x[i] == fx && d->y[i] == fy) {
				fruit_hidden = 1;
				break;
			}
		}
	}
			
	if (fx >= 0 && fy >= 0 && !fruit_hidden)
		printf(CURSOR_POS "%s" STYLE_BOLD FRUIT_CHAR STYLE_RESET,
			fy + 2, fx + 2, d->fruit_col ? d->fruit_col : COLOR_RED);
			
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
