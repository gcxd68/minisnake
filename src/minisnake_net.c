#include "minisnake.h"

#ifndef ONLINE_BUILD

int		check_client_version(void) { return (0); }
int		server_sync_rules(t_data *d) { (void)d; return (0); }
int		start_session(t_data *d) { (void)d; return (0); }
void	notify_server(t_data *d, const char *action, int fx, int fy) { (void)d; (void)action; (void)fx; (void)fy; }
void	handle_leaderboard(t_data *d) { (void)d; }
void	net_wait_all(void) {}

#else

/* Include(s) */
# include <netdb.h>
# include <netinet/in.h>
# include <sys/socket.h>

/* Network Constants */
# define BUF_READ			4096									/* Socket read chunk size */
# define BUF_GET_REQ		512										/* Sufficient for standard GET headers */
# define JSON_OVERHEAD		256										/* Margin for JSON syntax (keys, brackets) */
# define BUF_JSON_PAYLOAD	(MAX_SIZE + JSON_OVERHEAD)				/* Path string + JSON formatting */
# define HTTP_HDR_OVERHEAD	512										/* Margin for HTTP POST headers */
# define BUF_POST_REQ		(BUF_JSON_PAYLOAD + HTTP_HDR_OVERHEAD)	/* JSON + Headers */
# define BUF_RESP_SUBMIT	512										/* Small ACK responses */
# define BUF_RESP_SCORES	8192									/* Full leaderboard data */

# define BUF_PATH			256										/* Max URL path length */
# define BUF_ENTRY			128										/* Max length of a single leaderboard line */
# define BUF_TOKEN			33										/* 32 hex chars + null terminator */
# define NUM_RULES			10										/* Expected fields from /rules */

# define HTTP_MIN_LEN		12
# define HTTP_VER_LEN		7
# define HTTP_STAT_OFFSET	8
# define HTTP_STAT_LEN		4

/* UI and Leaderboard display settings */
# define LB_TITLE			"--- LEADERBOARD ---"
# define LB_TITLE_ROW		1
# define UI_PROMPT_ROW_OFF	4
# define UI_PROMPT_COL		1
# define LB_MAX_SCORES		20
# define LB_START_ROW		3
# define LB_COL_OFFSET		2
# define MAX_NAME_LEN		8
# define UI_NAME_WIDTH		12
# define UI_SCORE_WIDTH		7

/* Async request pool configuration */
# define REQ_POOL_SIZE		10
# define RETRY_DELAY_1		200000
# define RETRY_DELAY_2		500000
# define RETRY_DELAY_3		1500000
# define NET_WAIT_DELAY     10000

/* PREPROCESSOR CHECKS: Compile-time safety validation */
# if RETRY_DELAY_1 <= 0
#  error "First retry delay must be strictly positive"
# endif
# if RETRY_DELAY_2 <= RETRY_DELAY_1
#  error "Second retry delay must be greater than the first (exponential backoff)"
# endif
# if RETRY_DELAY_3 <= RETRY_DELAY_2
#  error "Third retry delay must be greater than the second (exponential backoff)"
# endif
# if BUF_RESP_SUBMIT <= 0 || BUF_RESP_SCORES <= 0 || BUF_READ <= 0 || BUF_GET_REQ <= 0 || BUF_PATH <= 0 || BUF_ENTRY <= 0
#  error "Buffer sizes must be strictly positive"
# endif
# if BUF_TOKEN < 33
#  error "BUF_TOKEN must be at least 33 to hold a 32-character hex token and a null terminator"
# endif
# if BUF_GET_REQ < (BUF_PATH + 64)
#  error "BUF_GET_REQ is too small to contain an HTTP request line and a path"
# endif
# if BUF_POST_REQ < (BUF_JSON_PAYLOAD + 256)
#  error "BUF_POST_REQ is too small to contain an HTTP request line, headers, and a JSON payload"
# endif
# if BUF_RESP_SCORES < (LB_MAX_SCORES * BUF_ENTRY)
#  error "BUF_RESP_SCORES is too small to hold all leaderboard entries"
# endif
# if HTTP_MIN_LEN <= 0 || HTTP_VER_LEN <= 0 || HTTP_STAT_OFFSET <= 0 || HTTP_STAT_LEN <= 0
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
# if REQ_POOL_SIZE <= 0
#  error "REQ_POOL_SIZE must be strictly positive"
# endif
# if NET_WAIT_DELAY <= 0
#  error "NET_WAIT_DELAY must be strictly positive"
# endif

typedef struct s_req {
	char	path[BUF_PATH];
	char	body[BUF_JSON_PAYLOAD];
	int		has_body;
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

/* Internal core: handles the established connection and I/O */
static int http_request(const char *req, char *out, int out_size) {
	int	fd = server_connect();

	if (fd < 0) return (-1);

	/* Send the pre-formatted request */
	if (write(fd, req, strlen(req)) < 0)
		return (close(fd), -1);

	/* Read the response */
	char buf[BUF_READ];
	int n, total = 0;
	while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
		if (total + n < out_size - 1) {
			memcpy(out + total, buf, n);
			total += n;
		}
	}
	out[total] = '\0';
	close(fd);

	/* Validation */
	if (total < HTTP_MIN_LEN || strncmp(out, "HTTP/1.", HTTP_VER_LEN) != 0 || !strstr(out, " 200 "))
		return (-1);
	return (0);
}

static int http_get(const char *path, char *out, int out_size) {
	char	req[BUF_GET_REQ];
	int		len = snprintf(req, sizeof(req), 
				"GET %s HTTP/1.0\r\nHost: %s\r\nX-Client-Version: %s\r\nConnection: close\r\n\r\n", 
				path, HOST, CLIENT_VERSION);
		
	if (len < 0 || (size_t)len >= sizeof(req)) return (-1);
	return http_request(req, out, out_size);
}

static int http_post(const char *path, const char *body, char *out, int out_size) {
	char	req[BUF_POST_REQ];
	int		len = snprintf(req, sizeof(req), 
				"POST %s HTTP/1.0\r\nHost: %s\r\nX-Client-Version: %s\r\n"
				"Content-Type: application/json\r\nContent-Length: %zu\r\n"
				"Connection: close\r\n\r\n%s", 
				path, HOST, CLIENT_VERSION, body ? strlen(body) : 0, body ? body : "");
		
	if (len < 0 || (size_t)len >= sizeof(req)) return (-1);
	return http_request(req, out, out_size);
}

static char *skip_headers(char *response) {
	char *body = strstr(response, "\r\n\r\n");
	if (body) return (body + 4);
	body = strstr(response, "\n\n");
	if (body) return (body + 2);
	return (response);
}

static void *async_http_worker(void *arg) {
	t_req   *req = (t_req *)arg;
	char    resp[BUF_RESP_SUBMIT];
	int     ret = -1;
	int     retries = 0;
	
	const int delays[] = {RETRY_DELAY_1, RETRY_DELAY_2, RETRY_DELAY_3};
	const int max_retries = sizeof(delays) / sizeof(delays[0]);

	/* 
	** Level 1 Fix: Exponential Backoff Retry.
	** Handles micro-cuts in the network connection smoothly.
	*/
	while (retries < max_retries) {
		ret = req->has_body ? http_post(req->path, req->body, resp, sizeof(resp)) 
							: http_get(req->path, resp, sizeof(resp));
		if (!ret) break; /* Success! Break out of the retry loop */
		
		usleep(delays[retries]);
		retries++;
	}

	if (!ret) {
		/* Parse coordinates for both /eat and /sync responses */
		if ((strncmp(req->path, "/eat", 4) == 0 || strncmp(req->path, "/sync", 5) == 0) && req->d) {
			char	*body = skip_headers(resp);
			char	*sep = strchr(body, '|');
			
			/* Custom safety check to avoid silent "0" on garbage data */
			if (sep && (isdigit(body[0]) || body[0] == '-') && (isdigit(*(sep + 1)) || *(sep + 1) == '-')) {
				*sep = '\0';
				int fruit_x = atoi(body);
				int fruit_y = atoi(sep + 1);
				
				/* Lock the mutex to safely update the fruit state atomically */
				write_fruit(req->d, fruit_x, fruit_y, fruit_color());
			}
		}
	}

	pthread_mutex_lock(&g_pool_mutex);
	req->has_body = 0; 
	req->in_use = 0;
	pthread_mutex_unlock(&g_pool_mutex);
	
	/* 
	** WARNING: TECH DEBT
	** Do NOT add any code below this unlock. 
	** net_wait_all() relies on in_use == 0 to assume this thread is physically dead. 
	*/
	return (NULL);
}

static void fire_and_forget(const char *path, const char *body, t_data *d) {
	pthread_t	tid;
	t_req		*req = NULL;
	
	pthread_mutex_lock(&g_pool_mutex);
	for (int i = 0; i < REQ_POOL_SIZE; i++) {
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
	
	if (body) {
		strncpy(req->body, body, BUF_JSON_PAYLOAD - 1);
		req->body[BUF_JSON_PAYLOAD - 1] = '\0';
		req->has_body = 1;
	} else {
		req->has_body = 0;
	}
	
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

	if (http_get("/rules", resp, sizeof(resp)) != 0)
		return (0);

	char *body = skip_headers(resp);
	if (strncmp(body, "UPDATE", 6) == 0)
		return (-1);

	return (1);
}

int server_sync_rules(t_data *d) {
	char	resp[BUF_RESP_SUBMIT];

	if (http_get("/rules", resp, sizeof(resp)) != 0)
		return (0);

	char *body = skip_headers(resp);
	if (strncmp(body, "UPDATE", 6) == 0) {
		fprintf(stderr, "Notice: Client version outdated. Falling back to Offline Mode.\n");
		return (0);
	}

	char *fields[NUM_RULES], *saveptr;
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

	char	resp[BUF_RESP_SUBMIT];
	
	d->token[0] = '\0';
	if (http_get("/token", resp, sizeof(resp)) != 0)
		return (0);
	char *body = skip_headers(resp);
	
	char *saveptr, *token_str = strtok_r(body, "|", &saveptr);
	if (!token_str) return (0);

	strncpy(d->token, token_str, BUF_TOKEN - 1);
	d->token[BUF_TOKEN - 1] = '\0';
	
	/* Server Authority: The server directly provides the starting head coordinates AND the first fruit coordinates */
	char *hx_str = strtok_r(NULL, "|", &saveptr);
	char *hy_str = strtok_r(NULL, "|", &saveptr);
	char *fx_str = strtok_r(NULL, "|", &saveptr);
	char *fy_str = strtok_r(NULL, "|", &saveptr);
	
	if (hx_str && hy_str && fx_str && fy_str) {
		d->x[0] = atoi(hx_str);
		d->y[0] = atoi(hy_str);
		d->fruit_x = atoi(fx_str);
		d->fruit_y = atoi(fy_str);
		d->fruit_color = fruit_color();
	}
	return (1);
}

void notify_server(t_data *d, const char *action, int fx, int fy) {
	if (!IS_SESSION_ACTIVE(d)) return;

	char path[BUF_PATH];
	char body[BUF_JSON_PAYLOAD];

	if (strcmp(action, "eat") == 0) {
		snprintf(path, sizeof(path), "/eat/%s", d->token); 
		snprintf(body, sizeof(body), "{\"seq\":%d,\"steps\":%d,\"fx\":%d,\"fy\":%d,\"path\":\"%s\"}", d->seq, d->steps, fx, fy, d->path);
		fire_and_forget(path, body, d);
	} else {
		snprintf(path, sizeof(path), "/%s/%s", action, d->token);
		fire_and_forget(path, NULL, d);
	}}

static int end_session(t_data *d, const char *name) {
	if (!IS_SESSION_ACTIVE(d)) return (-1);

	char	path[BUF_PATH], resp[BUF_RESP_SUBMIT];

	if (*name)
		snprintf(path, sizeof(path), "/submit/%s/%s/%d", d->token, name, d->steps);
	else
		snprintf(path, sizeof(path), "/quit/%s", d->token);

	return (http_get(path, resp, sizeof(resp)));
}

static int show_leaderboard(t_data *d) {
	char	path[BUF_PATH], resp[BUF_RESP_SCORES];

	snprintf(path, sizeof(path), "/scores/%d", LB_MAX_SCORES);
	if (http_get(path, resp, sizeof(resp)) < 0)
		return (-1);

	const char	title[] = LB_TITLE;
	const int	title_col = LB_COL_OFFSET + ((d->width - sizeof(title) + 1) >> 1);

	printf(CLEAR_SCREEN CURSOR_POS COLOR_MAGENTA STYLE_BOLD "%s" STYLE_RESET, LB_TITLE_ROW, title_col, title);
	char *body = skip_headers(resp);

	int			rank = 1, row = LB_START_ROW;
	char		*saveptr, *line = strtok_r(body, "\n", &saveptr);

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

/* Prompts the user for an alphanumeric name, loops until valid or EOF */
static void get_player_name(t_data *d, char *name, size_t size) {
	printf(SCROLL_REGION, d->height + UI_PROMPT_ROW_OFF, d->height + UI_PROMPT_ROW_OFF + 1);
	while (printf(CURSOR_POS ERASE_LINE "Name: ", d->height + UI_PROMPT_ROW_OFF, UI_PROMPT_COL),
		fflush(stdout),
		!name[0] && fgets(name, size, stdin)) {
		if (!strchr(name, '\n'))
			for (int c; (c = getchar()) != '\n' && c != EOF;);
		for (size_t i = strlen(name); i && isspace((unsigned char)name[i - 1]); name[--i] = '\0');
		if (!name[0])
			break;
		for (size_t i = strlen(name); i; i--) {
			if (!isalnum((unsigned char)name[i - 1])) {
				name[0] = '\0';
				break;
			}
		}
	}
	printf(SCROLL_RESET);
}

void handle_leaderboard(t_data *d) {
	if (!d->online) return;

	char	name[MAX_NAME_LEN + 1] = {0}; // 8 chars + 1 null terminator

	get_player_name(d, name, sizeof(name));
	show_loading();
	if (end_session(d, name) < 0 || show_leaderboard(d) < 0)
		printf(CLEAR_SCREEN "Network error\n");
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
			usleep(NET_WAIT_DELAY); 
	} while (pending);
}

#endif
