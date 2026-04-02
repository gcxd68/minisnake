#include "minisnake.h"

#ifndef ONLINE_BUILD

/* Stub: leaderboard is disabled in offline builds */
void    handle_leaderboard(t_data *d)
{
    (void)d;
}

/* Stub: session handling is disabled in offline builds */
void    vps_start_session(t_data *d)
{
    (void)d;
}

#else

# define BUF_RESP_SUBMIT    512         /* Small buffer: submit response is just "OK" */
# define BUF_RESP_SCORES    8192        /* Large buffer: up to 20 leaderboard entries */
# define BUF_READ           4096        /* Internal read() chunk size in http_get */
# define BUF_REQ            512         /* HTTP request line */
# define BUF_PATH           256         /* VPS API endpoint path */
# define BUF_ENTRY          128         /* One parsed leaderboard line */

# define HTTP_MIN_LEN       12          /* Minimum size to validate "HTTP/1.x 200" */
# define HTTP_VER_LEN       7           /* Length of "HTTP/1." */
# define HTTP_STAT_OFFSET   8           /* Offset to find " 200" */
# define HTTP_STAT_LEN      4           /* Length of " 200" */
# define HTTP_CRLF_LEN      4           /* Length of "\r\n\r\n" */
# define HTTP_LF_LEN        2           /* Length of "\n\n" */

# define LB_TITLE_ROW       1           /* Row for the leaderboard title */
# define UI_PROMPT_ROW_OFF  4           /* Offset below game board for name prompt */
# define UI_PROMPT_COL      1           /* Column start for name prompt */
# define LB_MAX_SCORES      20
# define LB_START_ROW       3           /* First row inside the game frame */
# define LB_COL_OFFSET      2           /* Left margin inside the frame */
# define MAX_NAME_LEN       8
# define UI_NAME_WIDTH      12          /* Column width for player name display */
# define UI_SCORE_WIDTH     7           /* Column width for score display */

# define LDB_TITLE          "--- LEADERBOARD ---"

static int vps_connect(void)
{
    struct sockaddr_in  addr;
    struct hostent      *he;
    int                 fd;

    /* VPS_HOST and VPS_PORT are now plain strings defined in net.h */
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

/* Perform a simple HTTP/1.0 GET request and store the raw response in out.
   Returns 0 on success, -1 on any network error. */
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
    if (n < 0)
        return (close(fd), -1);
    out[total] = '\0';
    close(fd);
    /* Check if the HTTP response is exactly HTTP/1.x 200 OK */
    if (total < HTTP_MIN_LEN || strncmp(out, "HTTP/1.", HTTP_VER_LEN) != 0 || strncmp(out + HTTP_STAT_OFFSET, " 200", HTTP_STAT_LEN) != 0)
        return (-1);
    return (0);
}

/* Skip HTTP response headers and return a pointer to the body.
   Handles both \r\n\r\n (standard) and \n\n (non-conforming servers). */
static char *skip_headers(char *response)
{
    char    *body = strstr(response, "\r\n\r\n");

    if (body)
        return (body + HTTP_CRLF_LEN);
    body = strstr(response, "\n\n");
    if (body)
        return (body + HTTP_LF_LEN);
    return (response);
}

/* Fetches a unique session token from the VPS when the game starts.
   This marks the official start time on the server to prevent speedhacks. */
void vps_start_session(t_data *d)
{
    char    resp[BUF_RESP_SUBMIT];

    if (!d->online) return;
    /* Call the /start route */
    if (http_get("/start", resp, sizeof(resp)) == 0)
    {
        /* Copy the 32-character hex token received from the body */
        strncpy(d->token, skip_headers(resp), 32);
        d->token[32] = '\0';
    }
    else
        d->token[0] = '\0';
}

/* Submits the final score using the session token.
   The server will validate the score against the elapsed time. */
static int vps_submit(t_data *d, const char *name)
{
    char    path[BUF_PATH], resp[BUF_RESP_SUBMIT];

    printf(CLEAR_SCREEN "Submitting...");
    fflush(stdout);
    /* If no token was obtained at startup, we cannot submit */
    if (!d->token[0])
        return (-1);
    snprintf(path, sizeof(path), "/submit/%s/%s/%d", d->token, name, d->cheat ? 0 : REAL_SCORE);
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
    /* VPS pipe format (relayed from Dreamlo): name|score|seconds|extras\n per entry */
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
static int read_name(char *name, size_t size)
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

void handle_leaderboard(t_data *d)
{
    if (!d->online) return;
    char name[MAX_NAME_LEN + 1];
    printf(CURSOR_POS ERASE_LINE "Name: ", d->height + UI_PROMPT_ROW_OFF, UI_PROMPT_COL);
    fflush(stdout);
    if (!read_name(name, sizeof(name)))
        return ;
    if (vps_submit(d, name) < 0 || vps_show(d) < 0)
        printf(CLEAR_SCREEN "Network error");
}

#endif
