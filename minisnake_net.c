#include "minisnake.h"

#ifndef ONLINE_BUILD

/* Stubs: network functions are disabled in offline builds */
void    handle_leaderboard(t_data *d) { (void)d; }
void    vps_start_session(t_data *d) { (void)d; }
void    vps_eat(t_data *d) { (void)d; }
void    vps_cheat(t_data *d) { (void)d; }

#else

# define BUF_RESP_SUBMIT    512         
# define BUF_RESP_SCORES    8192        
# define BUF_READ           4096        
# define BUF_REQ            512         
# define BUF_PATH           256         
# define BUF_ENTRY          128         

# define HTTP_MIN_LEN       12          
# define HTTP_VER_LEN       7           
# define HTTP_STAT_OFFSET   8           
# define HTTP_STAT_LEN      4           
# define HTTP_CRLF_LEN      4           
# define HTTP_LF_LEN        2           

# define LB_TITLE_ROW       1           
# define UI_PROMPT_ROW_OFF  4           
# define UI_PROMPT_COL      1           
# define LB_MAX_SCORES      20
# define LB_START_ROW       3           
# define LB_COL_OFFSET      2           
# define MAX_NAME_LEN       8
# define UI_NAME_WIDTH      12          
# define UI_SCORE_WIDTH     7           

# define LDB_TITLE          "--- LEADERBOARD ---"

/* Structure used to pass data to the background thread */
typedef struct s_req {
    char path[BUF_PATH];
} t_req;

static int vps_connect(void)
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

    if ((fd = vps_connect()) < 0)
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
void vps_start_session(t_data *d)
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
void vps_eat(t_data *d)
{
    char path[BUF_PATH];
    if (!d->online || !d->token[0]) return;
    snprintf(path, sizeof(path), "/eat/%s", d->token);
    fire_and_forget(path);
}

/* Asynchronous Ping: Called if the local anticheat detects tampering */
void vps_cheat(t_data *d)
{
    char path[BUF_PATH];
    if (!d->online || !d->token[0]) return;
    snprintf(path, sizeof(path), "/cheat/%s", d->token);
    fire_and_forget(path);
}

/* Submit the final score (The client no longer provides the score value) */
static int vps_submit(t_data *d, const char *name)
{
    char    path[BUF_PATH], resp[BUF_RESP_SUBMIT];

    printf(CLEAR_SCREEN "Submitting...");
    fflush(stdout);
    if (!d->token[0]) return (-1);
    
    /* We only send the Token and the Name. The VPS already knows the score. */
    snprintf(path, sizeof(path), "/submit/%s/%s", d->token, name);
    return (http_get(path, resp, sizeof(resp)));
}

static int vps_show(t_data *d)
{
    const char  title[] = LDB_TITLE;
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
    if (vps_submit(d, name) < 0 || vps_show(d) < 0)
        printf(CLEAR_SCREEN "Network error");
}

#endif
