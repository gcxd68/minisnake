#include "minisnake.h"

#ifndef ONLINE_BUILD

int		check_client_version(void) { return (0); }
int		server_sync_rules(t_data *d) { (void)d; return (0); }
int		start_session(t_data *d) { (void)d; return (0); }
void    notify_server(t_data *d, const char *action) { (void)d; (void)action; }
void    handle_leaderboard(t_data *d) { (void)d; }
void	net_wait_all(void) {}

#else

# define BUF_RESP_SUBMIT    512
# define BUF_RESP_SCORES    8192
# define BUF_READ           4096
# define BUF_REQ            512
# define BUF_PATH           256
# define BUF_ENTRY          128
# define BUF_TOKEN			33
# define NUM_RULES          10

# define HTTP_MIN_LEN       12
# define HTTP_VER_LEN       7
# define HTTP_STAT_OFFSET   8
# define HTTP_STAT_LEN      4

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

# define REQ_POOL_SIZE      10

typedef struct s_req {
	char	path[BUF_PATH];
	int		in_use;
	t_data	*d; /* Pointer back to game state for async updates */
}	t_req;

static t_req g_req_pool[REQ_POOL_SIZE];
static pthread_mutex_t g_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

static int server_connect(void) {
	struct sockaddr_in	addr;
	struct hostent		*he;
	int					fd;

	if (!(he = gethostbyname(HOST)))
		return (-1);
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return (-1);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(PORT)); 
	addr.sin_addr = *(struct in_addr *)he->h_addr;
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return (close(fd), -1);
	return (fd);
}

static int http_get(const char *path, char *out, int out_size) {
	char	req[BUF_REQ], buf[BUF_READ];
	int		fd, n, total = 0;

	if ((fd = server_connect()) < 0)
		return (-1);
		
	snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nX-Client-Version: %s\r\nConnection: close\r\n\r\n", path, HOST, CLIENT_VERSION);
	
	if (write(fd, req, strlen(req)) < 0)
		return (close(fd), -1);
	while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
		buf[n] = '\0';
		if (total + n < out_size - 1) {
			memcpy(out + total, buf, n);
			total += n;
		}
	}
	if (n < 0) return (close(fd), -1);
	out[total] = '\0';
	close(fd);
	if (total < HTTP_MIN_LEN || strncmp(out, "HTTP/1.", HTTP_VER_LEN) != 0 || !strstr(out, " 200 "))
		return (-1);
	return (0);
}

static char *skip_headers(char *response) {
	char *body = strstr(response, "\r\n\r\n");
	if (body) return (body + 4);
	body = strstr(response, "\n\n");
	if (body) return (body + 2);
	return (response);
}

static void *async_http_worker(void *arg) {
	t_req	*req = (t_req *)arg;
	char	resp[BUF_RESP_SUBMIT];
	
	if (http_get(req->path, resp, sizeof(resp)) == 0) {
		if (strncmp(req->path, "/eat", 4) == 0 && req->d) {
			char *body = skip_headers(resp);
			char *sep = strchr(body, '|');
			
			/* Custom safety check to avoid silent "0" on garbage data */
			if (sep && (isdigit(body[0]) || body[0] == '-') && (isdigit(*(sep + 1)) || *(sep + 1) == '-')) {
				*sep = '\0';
				int nx = atoi(body);
				int ny = atoi(sep + 1);
				
				/* Lock the mutex to update the pair atomically */
				pthread_mutex_lock(&req->d->fruit_mutex);
				req->d->fruit_x = nx;
				req->d->fruit_y = ny;
				pthread_mutex_unlock(&req->d->fruit_mutex);
			}
		}
	}

	pthread_mutex_lock(&g_pool_mutex);
	req->in_use = 0;
	pthread_mutex_unlock(&g_pool_mutex);
	
	/* WARNING: TECH DEBT
	   Do NOT add any code below this unlock. 
	   net_wait_all() relies on in_use == 0 to assume this thread is physically dead. */
	return (NULL);
}

static void fire_and_forget(const char *path, t_data *d) {
	pthread_t	tid;
	t_req		*req = NULL;
	int			i;
	
	pthread_mutex_lock(&g_pool_mutex);
	for (i = 0; i < REQ_POOL_SIZE; i++) {
		if (g_req_pool[i].in_use == 0) {
			req = &g_req_pool[i];
			req->in_use = 1;
			break;
		}
	}
	pthread_mutex_unlock(&g_pool_mutex);
	
	if (!req) return;
	
	strncpy(req->path, path, BUF_PATH - 1);
	req->path[BUF_PATH - 1] = '\0';
	req->d = d;
	
	if (pthread_create(&tid, NULL, async_http_worker, req) == 0)
		pthread_detach(tid);
	else {
		pthread_mutex_lock(&g_pool_mutex);
		req->in_use = 0;
		pthread_mutex_unlock(&g_pool_mutex);
	}
}

int check_client_version(void) {
	char	resp[BUF_RESP_SUBMIT];
	char	*body;

	if (http_get("/rules", resp, sizeof(resp)) != 0)
		return (0);

	body = skip_headers(resp);
	if (strncmp(body, "UPDATE", 6) == 0)
		return (-1);
		
	return (1);
}

int server_sync_rules(t_data *d) {
	char	resp[BUF_RESP_SUBMIT];
	char	*saveptr, *body;
	char	*fields[NUM_RULES];

	if (http_get("/rules", resp, sizeof(resp)) != 0)
		return (0);

	body = skip_headers(resp);
	if (strncmp(body, "UPDATE", 6) == 0) {
		fprintf(stderr, "Notice: Client version outdated. Falling back to Offline Mode.\n");
		return (0);
	}

	fields[0] = strtok_r(body, "|", &saveptr);
	if (!fields[0]) return (0);

	for (int i = 1; i < NUM_RULES; i++) {
		fields[i] = strtok_r(NULL, "|", &saveptr);
		if (!fields[i]) return (0);
	}

	d->width = MIN(MAX_WIDTH, MAX(MIN_WIDTH, atoi(fields[0])));
	d->height = MIN(MAX_HEIGHT, MAX(MIN_HEIGHT, atoi(fields[1])));
	d->delay = atof(fields[2]);
	d->speedup_factor = atof(fields[3]);
	d->points_per_fruit = atoi(fields[4]);
	d->cheat_timeout = atoi(fields[5]);
	d->grow = atoi(fields[6]) - 1;
	d->penalty_interval = atoi(fields[7]);
	d->penalty_amount = atoi(fields[8]);
	d->spawn_fruit_max_attempts = atoi(fields[9]);

	return (1);
}

int start_session(t_data *d) {
	if (!d->online) return (0);

	char    resp[BUF_RESP_SUBMIT];
	char    *body, *saveptr, *token_str, *x_str, *y_str;

	d->token[0] = '\0';
	if (http_get("/token", resp, sizeof(resp)) != 0)
		return (0);
	body = skip_headers(resp);
	
	token_str = strtok_r(body, "|", &saveptr);
	if (!token_str) return (0);

	strncpy(d->token, token_str, BUF_TOKEN - 1);
	d->token[BUF_TOKEN - 1] = '\0';
	
	/* The server directly provides the first fruit coordinates */
	x_str = strtok_r(NULL, "|", &saveptr);
	y_str = strtok_r(NULL, "|", &saveptr);
	if (x_str && y_str) {
		d->fruit_x = atoi(x_str);
		d->fruit_y = atoi(y_str);
	}
	return (1);
}

void notify_server(t_data *d, const char *action) {
	if (!IS_SESSION_ACTIVE(d)) return;

	char path[BUF_PATH];
	int fx, fy;

	/* Safe read for the URL payload */
	pthread_mutex_lock(&d->fruit_mutex);
	fx = d->fruit_x;
	fy = d->fruit_y;
	pthread_mutex_unlock(&d->fruit_mutex);

	if (strcmp(action, "eat") == 0)
		snprintf(path, sizeof(path), "/eat/%s/%d/%d/%d", d->token, d->steps, fx, fy); 
	else
		snprintf(path, sizeof(path), "/%s/%s", action, d->token);
	
	fire_and_forget(path, d);
}

static int end_session(t_data *d, const char *name) {
	if (!d->token[0]) return (-1);

	char    path[BUF_PATH], resp[BUF_RESP_SUBMIT];

	if (*name)
		snprintf(path, sizeof(path), "/submit/%s/%s/%d", d->token, name, d->steps);
	else
		snprintf(path, sizeof(path), "/quit/%s", d->token);
	return (http_get(path, resp, sizeof(resp)));
}

static int show_leaderboard(t_data *d) {
	const char  title[] = LB_TITLE;
	const int   title_col = LB_COL_OFFSET + ((d->width - sizeof(title) + 1) >> 1);
	char        *body, *line, *saveptr;
	char        path[BUF_PATH], resp[BUF_RESP_SCORES];
	int         rank = 1, row = LB_START_ROW;

	snprintf(path, sizeof(path), "/scores/%d", LB_MAX_SCORES);
	if (http_get(path, resp, sizeof(resp)) < 0)
		return (-1);
	printf(CLEAR_SCREEN CURSOR_POS COLOR_MAGENTA STYLE_BOLD "%s" STYLE_RESET, LB_TITLE_ROW, title_col, title);
	body = skip_headers(resp);

	line = strtok_r(body, "\n", &saveptr);
	while (line && rank <= LB_MAX_SCORES) {
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

static int read_name(char *name, size_t size) {
	if (!fgets(name, size, stdin) || name[0] == '\n')
		name[0] = '\0';
	else if (!strchr(name, '\n'))
		for (int c; (c = getchar()) != '\n' && c != EOF;);
	for (size_t i = strlen(name); i && isspace((unsigned char)name[i - 1]); name[--i] = '\0');
	return (name[0]);
}

void handle_leaderboard(t_data *d) {
	if (!d->online) return;

	char	name[MAX_NAME_LEN + 1] = {0};

	printf(CURSOR_POS ERASE_LINE "Name: ", d->height + UI_PROMPT_ROW_OFF, UI_PROMPT_COL);
	fflush(stdout);
	read_name(name, sizeof(name));
	show_loading();

	if (end_session(d, name) < 0 || show_leaderboard(d) < 0)
		printf(CLEAR_SCREEN "Network error");
}

/* Wait for all async requests to finish before tearing down the game */
void net_wait_all(void) {
	int pending;
	do {
		pending = 0;
		pthread_mutex_lock(&g_pool_mutex);
		for (int i = 0; i < REQ_POOL_SIZE; i++) {
			if (g_req_pool[i].in_use) {
				pending = 1;
				break;
			}
		}
		pthread_mutex_unlock(&g_pool_mutex);
		
		if (pending)
			usleep(10000); 
	} while (pending);
}

#endif
