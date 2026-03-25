#include "minisnake.h"

static int	parse_dimension(const char *str, int min, int max, int *out, const char *name)
{
	char	*endptr;
	long	val = strtol(str, &endptr, 10);

	if (!*endptr && val >= min && val <= max) {
		*out = (int)val;
		return (0);
	}
	fprintf(stderr, "minisnake: %s must be an integer between %d and %d\n", name, min, max);
	return (2);
}

static int	parse_args(int argc, char **argv, t_data *d)
{
	if (SPEEDUP_FACTOR < 0.0f || SPEEDUP_FACTOR >= 1.0f) {
		fprintf(stderr, "Error: SPEEDUP_FACTOR must be >= 0.0 and < 1.0\n");
		return(EXIT_FAILURE);
	}
	if (argc == 2 && !strcmp(argv[1], "online"))
	{
#ifndef ONLINE_BUILD
		fprintf(stderr, "minisnake: online mode not available in this build\n");
		return(EXIT_FAILURE);
#endif
		d->width = ONLINE_WIDTH;
		d->height = ONLINE_HEIGHT;
		d->online = 1;
		return (0);
	}
	else if (argc == 3)
	{
		if (parse_dimension(argv[1], MIN_WIDTH, MAX_WIDTH, &d->width, "width")
			|| parse_dimension(argv[2], MIN_HEIGHT, MAX_HEIGHT, &d->height, "height"))
			return (2);
		return (0);
	}
	fprintf(stderr, "Usage: ./minisnake online\n"
					"       ./minisnake WIDTH HEIGHT\n");
	return(2);
}

static int	read_char(void)
{
	int c = getchar();
	if (c != '\n' && c != EOF)
		for (int next; (next = getchar()) != '\n' && next != EOF;);
	return (c);
}

static int  ask_confirm(const char *question)
{
	printf("%s (y/n): ", question);
	fflush(stdout);
	int c = read_char();
	return (c == 'y' || c == 'Y');
}

static int	install_gnome_terminal(void)
{
	if (ask_confirm("gnome-terminal is not installed.\n"
		"It is recommended for a better user experience, but not required.\n"
		"Would you like to install it?"))
	{
		printf("Installing gnome-terminal...\n");
		if (system("sudo apt update && sudo apt install -y gnome-terminal") == 0)
			return (LAUNCH_SPAWN);
		fprintf(stderr, "Installation failed. Please install it manually.\n");
	}
	else
		printf("Installation skipped.\n");
	if (ask_confirm("Would you like to use the current terminal instead?"))
		return (LAUNCH_LOCAL);
	return (EXIT_SUCCESS);
	
}

static int	launch_terminal(int argc, char **argv, t_data *d)
{
	char	geom[BUF_GEOM], cmd[BUF_CMD];
	char	*self, *tty;
	int		ret;

	if (getenv(ENV_VAR))
		return (LAUNCH_LOCAL);
	if (system("which gnome-terminal > /dev/null 2>&1") != 0)
		if ((ret = install_gnome_terminal()) != LAUNCH_SPAWN)
			return (ret);
	self = realpath("/proc/self/exe", NULL);
	const char *exe_path = self ? self : DEFAULT_EXE;
	if (!(tty = ttyname(STDERR_FILENO)))
		tty = "/dev/null";
	snprintf(geom, sizeof(geom), "%dx%d", d->width + 2, d->height + 4);
	snprintf(cmd, sizeof(cmd), "%s %s %s 2>%s", exe_path, 
			 (argc > 1) ? argv[1] : "", (argc > 2) ? argv[2] : "", tty);
	char *args[] = {"gnome-terminal", "--wait", "--geometry", geom,
		"--title", TERM_TITLE, "--", "bash", "-c", cmd, NULL};
	setenv(ENV_VAR, "1", 1);
	if (execvp(args[0], args) == -1)
	{
		perror("minisnake: execvp failed");
		fprintf(stderr, "Failed to open a new terminal window.\n");
		free(self);
		if (!ask_confirm("Would you like to use the current terminal instead?"))
			return(EXIT_FAILURE);
	}
	unsetenv(ENV_VAR);
	return (LAUNCH_LOCAL);
}

static void	process_input(t_data *d)
{
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

void	spawn_fruit(t_data *d)
{
	int	i;

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
	d->score += 10;
	d->delay *= SPEEDUP_FACTOR;
}

static void	render(t_data *d)
{
	const char	*head[] = {"🭨", "🭪", "🭩", "🭫"};
	const char	*corner[] = {"▗", "▘"};

	if (!d->dir[0])
		return ;
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

int	main(int argc, char **argv)
{
	t_data d = {0};
	int	ret = parse_args(argc, argv, &d);
	if (ret) return (ret);
	ret = launch_terminal(argc, argv, &d);
	if (ret != LAUNCH_LOCAL) return ret;
	initialize(&d);
	while (!d.game_over && d.size < d.width * d.height)
	{
		process_input(&d);
		update_game(&d);
		render(&d);
		usleep(d.delay);
	}
	finalize(&d);
	return (EXIT_SUCCESS);
}
