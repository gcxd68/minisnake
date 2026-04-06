#include "minisnake.h"

#ifndef ONLINE_BUILD

/* STUBS: network functions are disabled in offline builds */
void    handle_leaderboard(t_data *d) { (void)d; }
int		server_sync_rules(t_data *d) { (void)d; return (0); }
int		start_session(t_data *d) { (void)d; return (0); }
void    notify_server(t_data *d, const char *action) { (void)d; (void)action; }

#else

/* BUFFER SIZES: Memory allocation limits for network I/O */
# define BUF_RESP_SUBMIT    512
# define BUF_RESP_SCORES    8192
# define BUF_READ           4096
# define BUF_REQ            512
# define BUF_PATH           256
# define BUF_ENTRY          128
# define BUF_TOKEN			33
# define NUM_RULES          10

/* HTTP PARSING: Offsets and string lengths for minimal HTTP/1.x parsing */
# define HTTP_MIN_LEN       12
# define HTTP_VER_LEN       7
# define HTTP_STAT_OFFSET   8
# define HTTP_STAT_LEN      4

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

/* ASYNC REQUEST POOL: Pre-allocated memory for background HTTP requests without malloc */
# define REQ_POOL_SIZE      10

/* PREPROCESSOR CHECKS: Compile-time safety validation */
# if BUF_RESP_SUBMIT <= 0 || BUF_RESP_SCORES <= 0 || BUF_READ <= 0 || BUF_REQ <= 0 || BUF_PATH <= 0 || BUF_ENTRY <= 0
#  error "Buffer sizes must be strictly positive"
# endif
# if BUF_TOKEN < 33
#  error "BUF_TOKEN must be at least 33 to hold a 32-character hex token and a null terminator"
# endif
# if BUF_REQ < (BUF_PATH + 64)
#  error "BUF_REQ is too small to contain an HTTP request line and a path"
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

/* Structure used to pass data to the background thread */
typedef struct s_req {
	char	path[BUF_PATH];
	int		in_use;
}	t_req;

/* Static array acting as pre-allocated memory for requests */
static t_req g_req_pool[REQ_POOL_SIZE];

/* Mutex to prevent data races when threads claim or release a pool slot */
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

/* Synchronous HTTP GET request */
static int http_get(const char *path, char *out, int out_size) {
	char	req[BUF_REQ], buf[BUF_READ];
	int		fd, n, total = 0;

	if ((fd = server_connect()) < 0)
		return (-1);
	snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, HOST);
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

/* The background worker function for asynchronous requests */
static void *async_http_worker(void *arg) {
	t_req	*req = (t_req *)arg;
	char	resp[BUF_RESP_SUBMIT];
	
	/* Perform the HTTP request (blocks this thread, but not the game) */
	http_get(req->path, resp, sizeof(resp)); 

	/* Thread-safely mark the pool slot as available again */
	pthread_mutex_lock(&g_pool_mutex);
	req->in_use = 0;
	pthread_mutex_unlock(&g_pool_mutex);
	return (NULL);
}

/* Fire and Forget: Spawns a detached thread using a static resource pool. *
 * This prevents the game from freezing when communicating with the VPS.   */
static void fire_and_forget(const char *path) {
	pthread_t	tid;
	t_req		*req = NULL;
	int			i;
	
	/* 1. Thread-safely search for an available slot in the pool */
	pthread_mutex_lock(&g_pool_mutex);
	for (i = 0; i < REQ_POOL_SIZE; i++) {
		if (g_req_pool[i].in_use == 0) {
			req = &g_req_pool[i];
			req->in_use = 1; /* Reserve the slot */
			break;
		}
	}
	pthread_mutex_unlock(&g_pool_mutex);
	
	/* 2. If the pool is full, drop the ping to avoid crashing the game */
	if (!req) return;
	
	/* 3. Prepare the data in the reserved static slot */
	strncpy(req->path, path, BUF_PATH - 1);
	req->path[BUF_PATH - 1] = '\0';
	
	/* 4. Spawn the detached thread pointing to our static slot */
	if (pthread_create(&tid, NULL, async_http_worker, req) == 0)
		pthread_detach(tid);
	else {
		/* If thread creation fails due to system limits, free the slot */
		pthread_mutex_lock(&g_pool_mutex);
		req->in_use = 0;
		pthread_mutex_unlock(&g_pool_mutex);
	}
}

static char *skip_headers(char *response) {
	char *body = strstr(response, "\r\n\r\n");
	if (body) return (body + 4);
	body = strstr(response, "\n\n");
	if (body) return (body + 2);
	return (response);
}

/* Initialization 1: Fetch game dimensions and rules from the server dynamically */
int server_sync_rules(t_data *d) {
	char	resp[BUF_RESP_SUBMIT];
	char	*saveptr, *body;
	char	*fields[NUM_RULES];

	if (http_get("/rules", resp, sizeof(resp)) != 0)
		return (0);

	body = skip_headers(resp);
	fields[0] = strtok_r(body, "|", &saveptr);
	if (!fields[0]) return (0);

	for (int i = 1; i < NUM_RULES; i++) {
		fields[i] = strtok_r(NULL, "|", &saveptr);
		if (!fields[i]) return (0);
	}

	/* Safely convert and apply server rules to game data */
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

/* Initialization 2: Fetch a specific session token AND seed for the current game instance */
int start_session(t_data *d) {
	if (!d->online) return (0);

	char    resp[BUF_RESP_SUBMIT];
	char    *body, *saveptr, *token_str, *seed_str;

	/* 1. Invalidate any existing session */
	d->token[0] = '\0';
	if (http_get("/token", resp, sizeof(resp)) != 0)
		return (0);
	body = skip_headers(resp);
	
	/* 2. Extract the Token */
	token_str = strtok_r(body, "|", &saveptr);
	if (!token_str) return (0);

	strncpy(d->token, token_str, BUF_TOKEN - 1);
	d->token[BUF_TOKEN - 1] = '\0';
	
	/* 3. Extract the Seed */
	seed_str = strtok_r(NULL, "|", &saveptr);
	if (!seed_str) return (0);

	/* 4. Convert the string seed to unsigned 32-bit integer */
	d->seed = (uint32_t)strtoul(seed_str, NULL, 10);
	return (1);
}

/* Asynchronous Ping: Spawns a background request to notify the server of an event */
void notify_server(t_data *d, const char *action) {
	if (!IS_SESSION_ACTIVE(d)) return;

	char path[BUF_PATH];

	/* If it's an "eat" action, build the URL with anti-cheat parameters */
	if (strcmp(action, "eat") == 0)
		snprintf(path, sizeof(path), "/eat/%s/%d/%d/%d", 
				 d->token, d->steps, d->fruit_x, d->fruit_y); 
	/* Otherwise (for "cheat" for example), keep the basic format */
	else
		snprintf(path, sizeof(path), "/%s/%s", action, d->token);
	fire_and_forget(path);
}

static int end_session(t_data *d, const char *name) {
	if (!d->token[0]) return (-1);

	char    path[BUF_PATH], resp[BUF_RESP_SUBMIT];

	/* We only send the Token and the Name. The VPS already knows the score. */
	snprintf(path, sizeof(path), "/submit/%s/%s/%d", d->token, name, d->steps);
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

	char	name[MAX_NAME_LEN + 1];
	int		ret = 0;

	if (d->score) {
		printf(CURSOR_POS ERASE_LINE "Name: ", d->height + UI_PROMPT_ROW_OFF, UI_PROMPT_COL);
		fflush(stdout);
		if (!read_name(name, sizeof(name))) return ;
	}
	show_loading();
	if (d->score)
		ret = end_session(d, name);
	if (ret || show_leaderboard(d) < 0)
		printf(CLEAR_SCREEN "Network error");
}

#endif
