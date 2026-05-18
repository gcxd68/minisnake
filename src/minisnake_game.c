#include "minisnake.h"

#ifndef ONLINE_BUILD

static void	anticheat(t_data *d) { (void)d; }
static void	sync_fruit_state(t_data *d) { (void)d; }

#else

# define MAX_MISSING_FRUIT_FRAMES	20
# define DEBUG_CHECK_FREQ			10
# define PROC_STATUS_PATH			"/proc/self/status"
# define TRACER_LEN					10
# define PROC_BUF_SIZE				256

/* PREPROCESSOR CHECKS: Compile-time safety validation */
# if MAX_MISSING_FRUIT_FRAMES < 0
#  error "MAX_MISSING_FRUIT_FRAMES cannot be negative"
# endif
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

	/* 1. Skip if offline or already flagged */
	if (!d->online || d->cheat) return;

	if (!d->dir[0]) {
		last_frame = now;
		counter = 0;
		return;
	}

	/* 2. Calculate potential penalties and validate strict sequence bounds */
	int current_penalty = (d->penalty_interval > 0) ? (d->steps / d->penalty_interval) * d->penalty_amount : 0;
	if (d->score < 0
		|| d->score > d->width * d->height * d->points_per_fruit - current_penalty
		|| now - last_frame > d->cheat_timeout) {
		d->cheat = 1;
		notify_server(d, "cheat", 0, 0);
		return;
	}
	last_frame = now;

	/* 3. Periodically poll process status mapping for attached memory debuggers */
	if (++counter > DEBUG_CHECK_FREQ) {
		FILE	*f = fopen(PROC_STATUS_PATH, "r");
		char	buf[PROC_BUF_SIZE];

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

static void sync_fruit_state(t_data *d) {
	if (!d->online) return;

	int	fruit_x, fruit_y;

	get_fruit_state(d, &fruit_x, &fruit_y, NULL);
	if (fruit_x == -1 || fruit_y == -1) {
		d->missing_fruit_frames++;

		/* 1. Start the wall-clock timer on the first missing frame */
		if (d->missing_fruit_frames == 1) {
			struct timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			d->fruit_hidden_at_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
		}

		/* 2. Hard UX timeout: signal workers to abort backoff immediately */
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		long now_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
		if (now_ms - d->fruit_hidden_at_ms > OFFLINE_FALLBACK_MS)
			d->ux_offline_requested = 1;

		/* 3. Keep polling the server every MAX_MISSING_FRUIT_FRAMES */
		if (d->missing_fruit_frames % MAX_MISSING_FRUIT_FRAMES == 0)
			notify_server(d, "sync", 0, 0);
	} else {
		d->missing_fruit_frames = 0;
		d->fruit_hidden_at_ms = 0;
		d->ux_offline_requested = 0;
	}
}

#endif

static void process_input(t_data *d) {
	static const char	move_keys[] = MOVE_KEYS;
	static const char	arrow_keys[] = ARROW_KEYS;
	const char			*pos;
	int					c, i;

	/* 1. Save previous direction */
	d->dir[1] = d->dir[0];
	for (i = 0; d->input_q[i] != EOF; i++);

	/* 2. Read all available inputs into the queue */
	while ((c = getchar()) != EOF) {
		c = (c == '\033' && getchar() == '[') ? getchar() + EXT_KEY_OFFSET : toupper(c);
		if (i < INPUT_Q_SIZE) d->input_q[i++] = c;
	}
	if ((c = d->input_q[0]) == *EXIT_KEY)
		d->game_over = 1;
	
	/* 3. Process the next input in the queue */
	const char *base = (c > 255) ? arrow_keys : move_keys;
	pos = strchr(base, (c > 255) ? c - EXT_KEY_OFFSET : c);
	if (pos && (pos - base + 2) >> 1 != (d->dir[0] + 1) >> 1)
		d->dir[0] = pos - base + 1;
	for (i = 0; i < INPUT_Q_SIZE; i++)
		d->input_q[i] = d->input_q[i + 1];
}

void	set_fruit_state(t_data *d, int x, int y, const char *color) {
	pthread_mutex_lock(&d->fruit_mutex);
	d->fruit_x = x;
	d->fruit_y = y;
	if (color) d->fruit_color = color;
	pthread_mutex_unlock(&d->fruit_mutex);
}

void	get_fruit_state(t_data *d, int *x, int *y, const char **color) {
	pthread_mutex_lock(&d->fruit_mutex);
	if (x) *x = d->fruit_x;
	if (y) *y = d->fruit_y;
	if (color) *color = d->fruit_color;
	pthread_mutex_unlock(&d->fruit_mutex);
}

void spawn_fruit(t_data *d) {
	int	i, fruit_x, fruit_y, attempts = 0;

	do {
		fruit_x = (lcg_rand(&d->seed) >> 16) % d->width;
		fruit_y = (lcg_rand(&d->seed) >> 16) % d->height;
		for (i = 0; i < d->size && !(d->body_x[i] == fruit_x && d->body_y[i] == fruit_y); i++);
		if (++attempts > d->spawn_fruit_max_attempts) break;
	} while (i < d->size);
	set_fruit_state(d, fruit_x, fruit_y, fruit_color(d));
}

static void	update_game(t_data *d) {
	if (!d->dir[0]) return;

	/* 1. Track movement history for server validation and apply penalties */
	const char *moves = " LRUD";
	if (d->path_steps < MAX_SIZE)
		d->path[d->path_steps] = moves[d->dir[0]];
	d->path_steps++;

	d->steps++;
	if (d->penalty_interval > 0 && d->steps % d->penalty_interval == 0)
		d->score -= d->penalty_amount;
	d->score = MAX(d->score, 0);

	/* 2. Apply movement mechanics, shift body segments, and consume growth charges */
	if (d->grow && d->grow--)
		d->size++;
	memmove(d->body_x + 1, d->body_x, d->size * sizeof(*d->body_x));
	memmove(d->body_y + 1, d->body_y, d->size * sizeof(*d->body_y));
	d->body_x[0] += (d->dir[0] == RIGHT) - (d->dir[0] == LEFT);
	d->body_y[0] += (d->dir[0] == DOWN) - (d->dir[0] == UP);

	/* 3. Handle boundary collisions and self-intersections */
	if (d->body_x[0] < 0 || d->body_x[0] == d->width || d->body_y[0] < 0 || d->body_y[0] == d->height)
		d->game_over = 1;
	for (int i = 1; i < d->size; i++)
		if (d->body_x[i] == d->body_x[0] && d->body_y[i] == d->body_y[0])
			d->game_over = 1;

	/* 4. Process fruit consumption and apply game rule progression */
	int fruit_x, fruit_y;
	get_fruit_state(d, &fruit_x, &fruit_y, NULL);

	if (!d->online && fruit_x == -1) {
		spawn_fruit(d);
		get_fruit_state(d, &fruit_x, &fruit_y, NULL);
	}

	if (d->body_x[0] != fruit_x || d->body_y[0] != fruit_y) return;

	d->grow = 1;
	d->score += d->points_per_fruit;
	d->delay *= d->speedup_factor;

	/* 5. Synchronize state with network backend and clear buffer */
	d->seq++;
	notify_server(d, "eat", fruit_x, fruit_y);

	d->path_steps = 0;
	memset(d->path, 0, sizeof(d->path));

	/* 6. Hide eaten fruit instantly to mask latency and prevent phantom fruit glitches */
	set_fruit_state(d, -1, -1, NULL);

	if (d->size >= d->width * d->height) return;
	if (!d->online) spawn_fruit(d);
}

const char *fruit_color(t_data *d) {
	static const int colors[] = { C_RED, C_GREEN, C_YELLOW, C_MAGENTA, C_CYAN, C_WHITE };
	return d->theme[colors[sys_rand() % ARR_SIZE(colors)]];
}

static void	render(t_data *d) {
	static const char	*heads[] = SNAKE_HEADS;
	static const char	*bends[] = SNAKE_BENDS;
	int					fruit_hidden = 0;

	if (!d->dir[0]) return;

	/* 1. Get current fruit position */
	int fruit_x, fruit_y;
	get_fruit_state(d, &fruit_x, &fruit_y, NULL);

	/* 2. Erase tail */
	if ((d->body_x[d->size] != fruit_x || d->body_y[d->size] != fruit_y) &&
		(d->body_x[d->size] != d->body_x[d->size - 1] || d->body_y[d->size] != d->body_y[d->size - 1]))
		printf(CURSOR_POS " ", d->body_y[d->size] + 2, d->body_x[d->size] + 2);
		
	/* 3. Draw snake body and bends */
	if (d->size > 1)
		printf("%s" CURSOR_POS "%s", d->theme[C_GREEN], d->body_y[1] + 2, d->body_x[1] + 2,
			(d->dir[0] + d->dir[1] == BEND_TURN_SUM) ? bends[(d->dir[0] % 2)] : SNAKE_BODY);		

	/* 4. Check if fruit is covered by the snake */
	if (fruit_x >= 0 && fruit_y >= 0) {
		for (int i = 0; i < d->size; i++) {
			if (d->body_x[i] == fruit_x && d->body_y[i] == fruit_y) {
				fruit_hidden = 1;
				break;
			}
		}
	}

	/* 5. Draw fruit */
	if (fruit_x >= 0 && fruit_y >= 0 && !fruit_hidden)
		printf(CURSOR_POS "%s" STYLE_BOLD FRUIT_CHAR STYLE_NO_BOLD "%s",
			fruit_y + 2, fruit_x + 2, d->fruit_color ? d->fruit_color : d->theme[C_RED], d->theme[C_WHITE]);
			
	/* 6. Draw snake head and update score */
	printf("%s" CURSOR_POS "%s", d->theme[C_GREEN], d->body_y[0] + 2, d->body_x[0] + 2, heads[d->dir[0] - 1]);
	printf("%s" CURSOR_POS "%d \n", d->theme[C_WHITE], d->height + 3, 8, d->score);
}

void	game_loop(t_data *d) {
	while (!d->game_over && d->size < d->width * d->height) {
		anticheat(d);
		process_input(d);
		update_game(d);
		sync_fruit_state(d);
		render(d);
		usleep(d->delay);
	}
}
