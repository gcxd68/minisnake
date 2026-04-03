#include "minisnake.h"

#ifndef ONLINE_BUILD

/* STUBS: network functions are disabled in offline builds */
void    handle_leaderboard(t_data *d) { (void)d; }
void    start_session(t_data *d) { (void)d; }
void    net_fruit_eaten(t_data *d) { (void)d; }
void    net_notify_cheat(t_data *d) { (void)d; }

#else

/* BUFFER SIZES: Memory allocation limits for network I/O */
# define BUF_RESP_SUBMIT    512
# define BUF_RESP_SCORES    8192
# define BUF_READ           4096
# define BUF_REQ            512
# define BUF_PATH           256
# define BUF_ENTRY          128

/* HTTP PARSING: Offsets and string lengths for minimal HTTP/1.x parsing */
# define HTTP_MIN_LEN       12
# define HTTP_VER_LEN       7
# define HTTP_STAT_OFFSET   8
# define HTTP_STAT_LEN      4
# define HTTP_CRLF_LEN      4
# define HTTP_LF_LEN        2

/* UI LAYOUT: Terminal positioning for the leaderboard and prompts */
# define LB_TITLE          "--- LEADERBOARD ---"
# define LB_TITLE_ROW       1
# define UI_PROMPT_ROW_OFF  4
# define UI_PROMPT_COL      1
# define LB_MAX_SCORES      20
# define LB_START_ROW       3
# define LB_COL_OFFSET      2
# define MAX_NAME_LEN       8
# define UI_NAME_WIDTH      12
# define UI_SCORE_WIDTH     7

/* PREPROCESSOR CHECKS: Compile-time safety validation */
# if BUF_RESP_SUBMIT <= 0 || BUF_RESP_SCORES <= 0 || BUF_READ <= 0 || BUF_REQ <= 0 || BUF_PATH <= 0 || BUF_ENTRY <= 0
#  error "Buffer sizes must be strictly positive"
# endif
# if BUF_REQ < (BUF_PATH + 64)
#  error "BUF_REQ is too small to contain an HTTP request line and a path"
# endif
# if BUF_RESP_SCORES < (LB_MAX_SCORES * BUF_ENTRY)
#  error "BUF_RESP_SCORES is too small to hold all leaderboard entries"
# endif
# if HTTP_MIN_LEN <= 0 || HTTP_VER_LEN <= 0 || HTTP_STAT_OFFSET <= 0 || HTTP_STAT_LEN <= 0 || HTTP_CRLF_LEN <= 0 || HTTP_LF_LEN <= 0
#  error "HTTP parsing constants must be strictly positive"
# endif
# if HTTP_MIN_LEN < (HTTP_STAT_OFFSET + HTTP_STAT_LEN)
#  error "HTTP_MIN_LEN must be large enough to read the required status code offset and length"
# endif
# if LB_MAX_SCORES <= 0
#  error "LB_MAX_SCORES must be strictly positive"
# endif
# if LB_START_ROW < 0 || LB_COL_OFFSET < 0 || UI_PROMPT_ROW_OFF < 0 || UI_PROMPT_COL < 0
#  error "UI offsets must be positive or zero"
# endif
# if MAX_NAME_LEN <= 0
#  error "MAX_NAME_LEN must be strictly positive"
# endif
# if UI_NAME_WIDTH < MAX_NAME_LEN
#  error "UI_NAME_WIDTH must be >= MAX_NAME_LEN"
# endif
# if UI_SCORE_WIDTH <= 0
#  error "UI_SCORE_WIDTH must be strictly positive"
# endif

/* Structure used to pass data to the background thread */
typedef struct s_req {
	char path[BUF_PATH];
} t_req;

static int server_connect(void)
{
	struct sockaddr_in  addr;
	struct hostent      *he;
	int                 fd;

	if (!(he = gethostbyname(VPS_HOST)))
		return (-1);
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return (-1);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(VPS_PORT)); 
	addr.sin_addr = *(struct in_addr *)he->h_addr;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return (close(fd), -1);
	return (fd);
}

/* Synchronous HTTP GET request */
static int http_get(const char *path, char *out, int out_size)
{
	char    req[BUF_REQ], buf[BUF_READ];
	int     fd, n, total = 0;

	if ((fd = server_connect()) < 0)
		return (-1);
	snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, VPS_HOST);
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
	if (n < 0) return (close(fd), -1);
	out[total] = '\0';
	close(fd);
	
	if (total < HTTP_MIN_LEN || strncmp(out, "HTTP/1.", HTTP_VER_LEN) != 0 || strncmp(out + HTTP_STAT_OFFSET, " 200", HTTP_STAT_LEN) != 0)
		return (-1);
	return (0);
}

/* The background worker function for asynchronous requests */
static void *async_http_worker(void *arg)
{
	t_req   *req = (t_req *)arg;
	char    resp[BUF_RESP_SUBMIT];
	
	http_get(req->path, resp, sizeof(resp)); 
	free(req); /* Clean up allocated memory after the request finishes */
	return (NULL);
}

/* * Fire and Forget: Spawns a detached thread to execute an HTTP request.
 * This prevents the game from freezing when communicating with the VPS.
 */
static void fire_and_forget(const char *path)
{
	pthread_t   tid;
	t_req       *req = malloc(sizeof(t_req));
	
	if (!req) return;
	strncpy(req->path, path, BUF_PATH - 1);
	req->path[BUF_PATH - 1] = '\0';
	
	/* Create thread and detach it immediately */
	if (pthread_create(&tid, NULL, async_http_worker, req) == 0)
		pthread_detach(tid);
	else
		free(req);
}

static char *skip_headers(char *response)
{
	char    *body = strstr(response, "\r\n\r\n");
	if (body) return (body + HTTP_CRLF_LEN);
	body = strstr(response, "\n\n");
	if (body) return (body + HTTP_LF_LEN);
	return (response);
}

/* Initialization: Fetch the token (Synchronous, done before gameplay starts) */
void net_start_session(t_data *d)
{
	char    resp[BUF_RESP_SUBMIT];

	if (!d->online) return;
	if (http_get("/start", resp, sizeof(resp)) == 0)
	{
		strncpy(d->token, skip_headers(resp), 32);
		d->token[32] = '\0';
	}
	else
		d->token[0] = '\0';
}

/* Asynchronous Ping: Called when a fruit is eaten */
void net_fruit_eaten(t_data *d)
{
	char path[BUF_PATH];
	if (!d->online || !d->token[0]) return;
	snprintf(path, sizeof(path), "/eat/%s", d->token);
	fire_and_forget(path);
}

/* Asynchronous Ping: Called if the local anticheat detects tampering */
void net_notify_cheat(t_data *d)
{
	d->cheat = 1;
	if (!d->online || !d->token[0]) return;
	char path[BUF_PATH];
	snprintf(path, sizeof(path), "/cheat/%s", d->token);
	fire_and_forget(path);
}

/* Submit the final score (The client no longer provides the score value) */
static int net_submit(t_data *d, const char *name)
{
	char    path[BUF_PATH], resp[BUF_RESP_SUBMIT];

	printf(CLEAR_SCREEN "Submitting...");
	fflush(stdout);
	if (!d->token[0]) return (-1);
	
	/* We only send the Token and the Name. The VPS already knows the score. */
	snprintf(path, sizeof(path), "/submit/%s/%s", d->token, name);
	return (http_get(path, resp, sizeof(resp)));
}

static int net_show(t_data *d)
{
	const char  title[] = LB_TITLE;
	const int   title_col = LB_COL_OFFSET + ((d->width - sizeof(title) + 1) >> 1);
	char        *body, *line, *saveptr;
	char        path[BUF_PATH], resp[BUF_RESP_SCORES];
	int         rank = 1, row = LB_START_ROW;

	snprintf(path, sizeof(path), "/scores/%d", LB_MAX_SCORES);
	if (http_get(path, resp, sizeof(resp)) < 0)
		return (-1);
	printf(ERASE_LINE CURSOR_POS COLOR_MAGENTA STYLE_BOLD "%s" STYLE_RESET, LB_TITLE_ROW, title_col, title);
	body = skip_headers(resp);
	
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

static int read_name(char *name, size_t size)
{
	disable_raw_mode();
	tcflush(STDIN_FILENO, TCIFLUSH);
	if (!fgets(name, size, stdin) || name[0] == '\n')
		name[0] = '\0';
	else if (!strchr(name, '\n'))
		for (int c; (c = getchar()) != '\n' && c != EOF;);
	for (size_t i = strlen(name); i && isspace((unsigned char)name[i - 1]); name[--i] = '\0');
	enable_raw_mode();
	return (name[0]);
}

void handle_leaderboard(t_data *d)
{
	if (!d->online) return;
	char name[MAX_NAME_LEN + 1];
	printf(CURSOR_POS ERASE_LINE "Name: ", d->height + UI_PROMPT_ROW_OFF, UI_PROMPT_COL);
	fflush(stdout);
	if (!read_name(name, sizeof(name))) return ;
	if (net_submit(d, name) < 0 || net_show(d) < 0)
		printf(CLEAR_SCREEN "Network error");
}

#endif
