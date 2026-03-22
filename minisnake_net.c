#include "minisnake.h"

#ifdef ONLINE_BUILD

# define DREAMLO_HOST "dreamlo.com"
# define DREAMLO_PORT 80

static int	dreamlo_connect(void)
{
	struct sockaddr_in	addr;
	struct hostent		*he;
	int					fd;

	he = gethostbyname(DREAMLO_HOST);
	if (!he)
		return (-1);
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return (-1);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(DREAMLO_PORT);
	addr.sin_addr = *(struct in_addr *)he->h_addr;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return (close(fd), -1);
	return (fd);
}

static int	http_get(const char *path, char *out, int out_size)
{
	char	req[512];
	char	buf[4096];
	int		fd, n, total;

	fd = dreamlo_connect();
	if (fd < 0)
		return (-1);
	snprintf(req, sizeof(req),
		"GET %s HTTP/1.0\r\nHost: " DREAMLO_HOST "\r\nConnection: close\r\n\r\n",
		path);
	if (write(fd, req, strlen(req)) < 0)
		return (close(fd), -1);
	total = 0;
	while ((n = read(fd, buf, sizeof(buf) - 1)) > 0)
	{
		buf[n] = '\0';
		if (total + n < out_size - 1)
		{
			memcpy(out + total, buf, n);
			total += n;
		}
	}
	out[total] = '\0';
	close(fd);
	return (total);
}

static char	*skip_headers(char *response)
{
	char *body = strstr(response, "\r\n\r\n");
	if (!body)
		body = strstr(response, "\n\n");
	return (body ? body + (body[0] == '\r' ? 4 : 2) : response);
}

static void get_real_key(const unsigned char *obfuscated, char *out)
{
	int i = 0;
	while (obfuscated[i] != 0x00)
	{
		out[i] = obfuscated[i] ^ XOR_KEY;
		i++;
	}
	out[i] = '\0';
}

static void	dreamlo_submit(t_data *d, const char *name)
{
	const unsigned char	obs_priv[] = OBS_PRIV_KEY;
	char				priv_key[64];
	char				path[256];
	char				resp[4096];

	get_real_key(obs_priv, priv_key);
	snprintf(path, sizeof(path), "/lb/%s/add/%s/%d", priv_key, name, d->score);
	if (http_get(path, resp, sizeof(resp)) < 0)
		fprintf(stderr, "Failed to submit score (network error)\n");
	memset(priv_key, 0, sizeof(priv_key));
}

static void	dreamlo_show(t_data *d)
{
	const unsigned char	obs_pub[] = OBS_PUB_KEY;
	const char			title[] = "LEADERBOARD";
	const int			title_col = 2 + ((d->width - (int)sizeof(title) + 1) >> 1);
	char				*body, *line, *saveptr;
	char				pub_key[64], path[256], resp[8192];
	int					rank = 1, row = 3;

	get_real_key(obs_pub, pub_key);
	snprintf(path, sizeof(path), "/lb/%s/pipe/20", pub_key);
	if (http_get(path, resp, sizeof(resp)) < 0)
	{
		fprintf(stderr, "Failed to fetch leaderboard (network error)\n");
		return ;
	}
	printf(ERASE_LINE CURSOR_POS "%s", 1, title_col, title);
	body = skip_headers(resp);
	line = strtok_r(body, "\n", &saveptr);
	while (line && rank <= 20)
	{
		char	entry[256];
		char	*p_name, *p_score, *p_save;

		strncpy(entry, line, sizeof(entry) - 1);
		entry[sizeof(entry) - 1] = '\0';
		p_name = strtok_r(entry, "|", &p_save);
		p_score = strtok_r(NULL, "|", &p_save);
		if (p_name && p_score)
			printf(CURSOR_POS "%2d. %-12s %8s", row++, 2, rank++, p_name, p_score);
		line = strtok_r(NULL, "\n", &saveptr);
	}
	printf(CURSOR_POS, d->height + 4, 1);
}

void	ask_and_submit(t_data *d)
{
	char	name[9];
	int		i;

	printf(CURSOR_POS ERASE_LINE "Name: ", d->height + 4, 1);
	fflush(stdout);
	tcflush(STDIN_FILENO, TCIFLUSH);
	if (!fgets(name, sizeof(name), stdin) || name[0] == '\n')
		return ;
	if (!strchr(name, '\n'))
		while ((i = getchar()) != '\n' && i != EOF);
	for (i = strlen(name) - 1; i >= 0 && isspace((unsigned char)name[i]); i--);
	name[i + 1] = '\0';
	if (!name[0])
		return ;
	printf(CLEAR_SCREEN "Submitting...");
	fflush(stdout);
	dreamlo_submit(d, name);
	dreamlo_show(d);
	printf("Press Enter to close...");
	fflush(stdout);
	while ((i = getchar()) != '\n' && i != EOF);
}

# else

void	ask_and_submit(t_data *d)
{
	(void)d;
}

#endif
