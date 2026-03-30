#include "minisnake.h"

#ifndef ONLINE_BUILD

/* Stub: leaderboard is disabled in offline builds */
void	handle_leaderboard(t_data *d)
{
	(void)d;
}

#else

# define DREAMLO_HOST		"dreamlo.com"
# define DREAMLO_PORT		80

# define BUF_RESP_SUBMIT	512		/* Small buffer: submit response is just "OK" */
# define BUF_RESP_SCORES	8192	/* Large buffer: up to 20 leaderboard entries */
# define BUF_READ			4096	/* Internal read() chunk size in http_get */
# define BUF_REQ			512		/* HTTP request line */
# define BUF_PATH			256		/* Dreamlo API endpoint path */
# define BUF_ENTRY			128		/* One parsed leaderboard line */
# define BUF_KEY			128		/* Decoded Dreamlo key (public or private) */

# define LB_MAX_SCORES		20
# define LB_START_ROW		3		/* First row inside the game frame */
# define LB_COL_OFFSET		2		/* Left margin inside the frame */
# define MAX_NAME_LEN		8
# define UI_NAME_WIDTH		12		/* Column width for player name display */
# define UI_SCORE_WIDTH		7		/* Column width for score display */

# define LDB_TITLE			"--- LEADERBOARD ---"

static int	dreamlo_connect(void)
{
	struct sockaddr_in	addr;
	struct hostent		*he;
	int					fd;

	if (!(he = gethostbyname(DREAMLO_HOST)))
		return (-1);
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return (-1);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(DREAMLO_PORT);
	addr.sin_addr = *(struct in_addr *)he->h_addr;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return (close(fd), -1);
	return (fd);
}

/* Perform a simple HTTP/1.0 GET request and store the raw response in out.
   Returns 0 on success, -1 on any network error. */
static int	http_get(const char *path, char *out, int out_size)
{
	char	req[BUF_REQ], buf[BUF_READ];
	int		fd, n, total = 0;

	if ((fd = dreamlo_connect()) < 0)
		return (-1);
	snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: " DREAMLO_HOST "\r\nConnection: close\r\n\r\n", path);
	if (write(fd, req, strlen(req)) < 0)
		return (close(fd), -1);
	while ((n = read(fd, buf, sizeof(buf) - 1)) > 0)
	{
		buf[n] = '\0';
		if (total + n < out_size - 1)
		{
			memcpy(out + total, buf, n);
			total += n;
		}
	}
	/* n < 0 means read() failed (e.g. connection reset) */
	if (n < 0)
		return (close(fd), -1);
	out[total] = '\0';
	close(fd);
	return (0);
}

/* Skip HTTP response headers and return a pointer to the body.
   Handles both \r\n\r\n (standard) and \n\n (non-conforming servers). */
static char	*skip_headers(char *response)
{
	char *body = strstr(response, "\r\n\r\n");
	if (body)
		return (body + 4);
	body = strstr(response, "\n\n");
	if (body)
		return (body + 2);
	return (response);
}

/* Decode an obfuscated key using a three-part XOR+offset scheme.
   The actual key is reconstructed as: base_key = (PART_A ^ PART_B) + PART_C
   Each byte was encoded as: (plaintext + base_key + index) % 256 ^ SALT
   so decoding reverses: c ^ SALT - (base_key + i) */
static void get_real_key(const unsigned char *obfuscated, char *out, size_t len)
{
	volatile unsigned char part_a = KEY_PART_A;
	volatile unsigned char part_b = KEY_PART_B;
	volatile unsigned char part_c = KEY_PART_C;
	unsigned char base_key = (part_a ^ part_b) + part_c;
	for (size_t i = 0; i < len - 1; i++)
	{
		unsigned char c = obfuscated[i];
		c = c ^ KEY_SALT;
		c = c - (unsigned char)(base_key + i);
		out[i] = (char)c;
	}
	out[len - 1] = '\0';
}

/* Decode the key, build the full Dreamlo API path, then wipe the key from memory.
   Wiping prevents the key from lingering in stack memory after the call. */
static void build_path(char *out, size_t size, const unsigned char *obs_key,
	size_t key_len, const char *fmt, ...)
{
	char    real_key[BUF_KEY], action[BUF_PATH];
	va_list args;

	get_real_key(obs_key, real_key, key_len);
	va_start(args, fmt);
	vsnprintf(action, sizeof(action), fmt, args);
	va_end(args);
	snprintf(out, size, "/lb/%s/%s", real_key, action);
	/* Wipe the decoded key so it doesn't linger in stack memory */
	memset(real_key, 0, sizeof(real_key));
}

static int	dreamlo_submit(t_data *d, const char *name)
{
	const unsigned char	obs_priv[] = OBS_PRIV_KEY;
	char				path[BUF_PATH], resp[BUF_RESP_SUBMIT];

	printf(CLEAR_SCREEN "Submitting...");
	fflush(stdout);
	build_path(path, sizeof(path), obs_priv, sizeof(obs_priv), "add/%s/%d",
		name, d->cheat ? XOR_SCORE(0) : REAL_SCORE);
	return (http_get(path, resp, sizeof(resp)));
}

static int	dreamlo_show(t_data *d)
{
	const unsigned char	obs_pub[] = OBS_PUB_KEY;
	const char			title[] = LDB_TITLE;
	/* Center the title within the game frame */
	const int			title_col = LB_COL_OFFSET + ((d->width - sizeof(title) + 1) >> 1);
	char				*body, *line, *saveptr;
	char				path[BUF_PATH], resp[BUF_RESP_SCORES];
	int					rank = 1, row = LB_START_ROW;

	build_path(path, sizeof(path), obs_pub, sizeof(obs_pub), "pipe/%d", LB_MAX_SCORES);
	if (http_get(path, resp, sizeof(resp)) < 0)
		return (-1);
	printf(ERASE_LINE CURSOR_POS COLOR_MAGENTA STYLE_BOLD "%s" STYLE_RESET, 1, title_col, title);
	body = skip_headers(resp);
	/* Dreamlo pipe format: name|score|seconds|extras\n per entry */
	line = strtok_r(body, "\n", &saveptr);
	while (line && rank <= LB_MAX_SCORES)
	{
		char entry[BUF_ENTRY], *p_name, *p_score, *p_save;
		strncpy(entry, line, sizeof(entry) - 1);
		entry[sizeof(entry) - 1] = '\0';
		p_name = strtok_r(entry, "|", &p_save);
		p_score = strtok_r(NULL, "|", &p_save);
		if (p_name && p_score)
			printf(CURSOR_POS "%2d. %-*s %*s", row++, LB_COL_OFFSET, rank++,
				   UI_NAME_WIDTH, p_name, UI_SCORE_WIDTH, p_score);
		line = strtok_r(NULL, "\n", &saveptr);
	}
	return (0);
}

/* Read up to (size - 1) chars from stdin in canonical mode.
   tcflush() discards any keys pressed during gameplay that are still buffered.
   enable_raw_mode() is always called before returning so the caller doesn't
   need to worry about the terminal state. */
static int	read_name(char *name, size_t size)
{
	disable_raw_mode();
	tcflush(STDIN_FILENO, TCIFLUSH);
	if (!fgets(name, size, stdin) || name[0] == '\n')
		name[0] = '\0';
	else if (!strchr(name, '\n'))
		/* fgets filled the buffer without hitting '\n': drain the rest of the line */
		for (int c; (c = getchar()) != '\n' && c != EOF;);
	/* Trim trailing whitespace in-place */
	for (size_t i = strlen(name); i && isspace((unsigned char)name[i - 1]); name[--i] = '\0');
	enable_raw_mode();
	return (name[0]);
}

void	handle_leaderboard(t_data *d)
{
	if (!d->online) return;
	char name[MAX_NAME_LEN + 1];
	printf(CURSOR_POS ERASE_LINE "Name: ", d->height + 4, 1);
	fflush(stdout);
	if (!read_name(name, sizeof(name)))
		return ;
	if (dreamlo_submit(d, name) < 0 || dreamlo_show(d) < 0)
		printf(CLEAR_SCREEN "Network error");
}

#endif
