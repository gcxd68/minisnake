#include "minisnake.h"

#ifndef ONLINE_BUILD

void	refresh_network_status(t_data *d) { (void)d; }
int		check_client_version(void) { return (0); }
int		fetch_server_rules(t_data *d) { (void)d; return (0); }
int		check_rules_sync(t_data *d) { (void)d; return (0); }
int		start_session(t_data *d) { (void)d; return (0); }
int		notify_server(t_data *d, const char *action, int fx, int fy) { (void)d; (void)action; (void)fx; (void)fy; return (0); }
void	handle_leaderboard(t_data *d) { (void)d; }
void	net_wait_all(void) {}

#else

/* Include(s) */
# include <netdb.h>
# include <netinet/in.h>
# include <sys/socket.h>
# include <sys/time.h>

/* Network Constants */
# define BUF_READ			4096
# define BUF_GET_REQ		512
# define JSON_OVERHEAD		256
# define BUF_JSON_PAYLOAD	(MAX_SIZE + JSON_OVERHEAD)
# define HTTP_HDR_OVERHEAD	512
# define BUF_POST_REQ		(BUF_JSON_PAYLOAD + HTTP_HDR_OVERHEAD)
# define BUF_RESP_SUBMIT	512
# define BUF_RESP_SCORES	8192

# define BUF_PATH			256
# define BUF_ENTRY			128
# define BUF_TOKEN			33
# define NUM_RULES			10

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
# define REQ_POOL_SIZE			10
# define BACKOFF_MIN_DELAY		100000
# define BACKOFF_MAX_DELAY		30000000
# define BACKOFF_MAX_RETRIES	15
# define NET_WAIT_DELAY			10000
# define NET_POLL_INTERVAL		10000

/* PREPROCESSOR CHECKS: Compile-time safety validation */
# if BACKOFF_MAX_DELAY <= 0
#  error "BACKOFF_MAX_DELAY must be strictly positive"
# endif
# if BACKOFF_MAX_RETRIES <= 0
#  error "BACKOFF_MAX_RETRIES must be strictly positive"
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
	t_data	*d;
}	t_req;

static t_req g_req_pool[REQ_POOL_SIZE];
static pthread_mutex_t g_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_shutting_down = 0; /* Signals workers to abort their backoff loops on teardown */

static int server_connect(void) {
	struct sockaddr_in	addr;
	struct hostent		*he;
	int					fd;
	struct timeval		tv;

	if (!(he = gethostbyname(HOST))) return (-1);
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) return (-1);

	/* Configure socket timeouts to prevent dead network blocking */
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(PORT)); 
	addr.sin_addr = *(struct in_addr *)he->h_addr;
	
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		return (close(fd), -1);
	return (fd);
}

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

void	refresh_network_status(t_data *d) {
	const char	*net_msg = d->online ? "ONLINE" : "OFFLINE";
	const char	*net_col = d->online ? d->theme[C_GREEN] : d->theme[C_RED];
	const int	net_x = MAX(15, (d->width + 2) - (int)strlen(net_msg) + 1);

	printf(CURSOR_POS "       ", d->height + 3, net_x); 
	printf(CURSOR_POS "%s%s%s", d->height + 3, net_x, net_col, net_msg, d->theme[C_WHITE]);
	fflush(stdout);
}

static void *async_http_worker(void *arg) {
	t_req	*req = (t_req *)arg;
	char	resp[BUF_RESP_SUBMIT];
	int		ret = -1, retries = 0, is_shutting_down = 0;
	int		delay = req->d ? MAX(BACKOFF_MIN_DELAY, (int)(req->d->delay * 0.66f)) : BACKOFF_MIN_DELAY;

	/* 1. Send request with exponential backoff */
	while (!is_shutting_down) {
		ret = req->has_body ? http_post(req->path, req->body, resp, sizeof(resp)) 
							: http_get(req->path, resp, sizeof(resp));
		
		if (!ret || retries >= BACKOFF_MAX_RETRIES) break;

		/* Sleep in small increments to quickly detect shutdown signals */
		int slept = 0;
		while (slept < delay) {
			pthread_mutex_lock(&g_pool_mutex);
			is_shutting_down = g_shutting_down;
			pthread_mutex_unlock(&g_pool_mutex);
			if (is_shutting_down) break;
			usleep(NET_POLL_INTERVAL);
			slept += NET_POLL_INTERVAL;
		}
		retries++;
		delay = MIN(delay * 2, BACKOFF_MAX_DELAY);
	}

	if (ret != 0 && req->d && req->d->online) {
		req->d->online = 0;
		req->d->seed = sys_rand();
		spawn_fruit(req->d);
		refresh_network_status(req->d);
	}

	/* 2. Parse new fruit coordinates from response */
	else if (!ret) {
		if ((strncmp(req->path, "/eat", 4) == 0 || strncmp(req->path, "/sync", 5) == 0) && req->d) {
			char	*body = skip_headers(resp);
			char	*sep = strchr(body, '|');
			
			/* Custom safety check to avoid silent "0" on garbage data */
			if (sep && (isdigit(body[0]) || body[0] == '-') && (isdigit(*(sep + 1)) || *(sep + 1) == '-')) {
				*sep = '\0';
				int fruit_x = atoi(body);
				int fruit_y = atoi(sep + 1);
				
				/* The server now guarantees it will never return a phantom fruit due to 
				   idempotency checks (LastSeq). We can blindly trust the coordinates. */
				set_fruit_state(req->d, fruit_x, fruit_y, fruit_color(req->d));
			}
		}
	}

	/* 3. Mark request slot as available */
	pthread_mutex_lock(&g_pool_mutex);
	req->in_use = 0;
	pthread_mutex_unlock(&g_pool_mutex);
	return (NULL);
}

static void fire_and_forget(const char *path, const char *body, t_data *d) {
	pthread_t	tid;
	t_req		*req = NULL;
	
	/* 1. Find an empty request slot */
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
	
	/* 2. Launch detached worker thread */
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

int fetch_server_rules(t_data *d) {
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

int	check_rules_sync(t_data *d) {
	t_data tmp; /* Disposable structure to store the server's response */

	/* 1. Fetch the rules into the temporary structure */
	if (!fetch_server_rules(&tmp))
		return (0);

	/* 2. Field-by-field comparison */
	if (d->width != tmp.width ||
		d->height != tmp.height ||
		d->delay != tmp.delay ||
		d->speedup_factor != tmp.speedup_factor ||
		d->points_per_fruit != tmp.points_per_fruit ||
		d->cheat_timeout != tmp.cheat_timeout ||
		d->grow != tmp.grow ||
		d->penalty_interval != tmp.penalty_interval ||
		d->penalty_amount != tmp.penalty_amount ||
		d->spawn_fruit_max_attempts != tmp.spawn_fruit_max_attempts)
		return (0); /* Desynchronization detected! */

	return (1); /* Everything is perfectly synced */
}

int start_session(t_data *d) {
	char	resp[BUF_RESP_SUBMIT];
	
	d->token[0] = '\0';
	if (http_get("/token", resp, sizeof(resp)) != 0)
		return (0);
	char *body = skip_headers(resp);
	
	/* Server Authority: The backend dictates the initial coordinates 
	   (token|head_x|head_y|fruit_x|fruit_y) to prevent client-side RNG tampering */
	char *saveptr, *token_str = strtok_r(body, "|", &saveptr);
	if (!token_str) return (0);

	strncpy(d->token, token_str, BUF_TOKEN - 1);
	d->token[BUF_TOKEN - 1] = '\0';
	
	char *hx_str = strtok_r(NULL, "|", &saveptr);
	char *hy_str = strtok_r(NULL, "|", &saveptr);
	char *fx_str = strtok_r(NULL, "|", &saveptr);
	char *fy_str = strtok_r(NULL, "|", &saveptr);
	
	if (hx_str && hy_str && fx_str && fy_str) {
		d->body_x[0] = atoi(hx_str);
		d->body_y[0] = atoi(hy_str);
		d->fruit_x = atoi(fx_str);
		d->fruit_y = atoi(fy_str);
		d->fruit_color = fruit_color(d);
	}
	return (1);
}

void	notify_server(t_data *d, const char *action, int fx, int fy) {
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
	}
}


static int end_session(t_data *d, const char *name) {
	if (!IS_SESSION_ACTIVE(d)) return (-1);

	char	path[BUF_PATH], resp[BUF_RESP_SUBMIT];

	if (*name) snprintf(path, sizeof(path), "/submit/%s/%s/%d", d->token, name, d->steps);
	else snprintf(path, sizeof(path), "/quit/%s", d->token);

	return (http_get(path, resp, sizeof(resp)));
}

static int show_leaderboard(t_data *d) {
	char		path[BUF_PATH], resp[BUF_RESP_SCORES];

	snprintf(path, sizeof(path), "/scores/%d", LB_MAX_SCORES);
	if (http_get(path, resp, sizeof(resp)) < 0)
		return (-1);

	const char	title[] = LB_TITLE;
	const int	title_col = LB_COL_OFFSET + ((d->width - sizeof(title) + 1) >> 1);

	printf(CLEAR_SCREEN CURSOR_POS "%s" STYLE_BOLD "%s" STYLE_NO_BOLD "%s", 
		LB_TITLE_ROW, title_col, d->theme[C_MAGENTA], title, d->theme[C_WHITE]);
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

	char	name[MAX_NAME_LEN + 1] = {0};

	get_player_name(d, name, sizeof(name));
	show_loading();
	if (end_session(d, name) < 0 || show_leaderboard(d) < 0)
		printf(CLEAR_SCREEN "Network error\n");
}

/* Wait for all async requests to finish before tearing down the game */
void net_wait_all(void) {
	int pending;
	int elapsed_ms = 0;
	const int timeout_ms = 3000; /* Force shutdown after 3 seconds max */

	/* Signal all async workers to terminate their wait loops */
	pthread_mutex_lock(&g_pool_mutex);
	g_shutting_down = 1;
	pthread_mutex_unlock(&g_pool_mutex);

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
		
		if (pending) {
			usleep(NET_WAIT_DELAY);
			elapsed_ms += (NET_WAIT_DELAY / 1000);
			if (elapsed_ms >= timeout_ms) {
				break;
			}
		}
	} while (pending);
}

#endif
