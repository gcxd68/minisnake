#ifndef SNAKE_H
# define SNAKE_H

# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <termios.h>
# include <fcntl.h>
# include <time.h>
# include <sys/ioctl.h>

# define MAX_WIDTH 200
# define MAX_HEIGHT 50
# define DELAY 200000

typedef enum e_dir {
	STOP,
	LEFT,
	RIGHT,
	UP,
	DOWN
}	t_dir;

typedef struct s_snake {    
	struct termios savedTerm;
	int width, height, fruitX, fruitY, size, grow, score, gameOver;
	float delay;
	int x[10001], y[10001];
	t_dir dir;
}	t_snake;

void	init_game(t_snake *s, char **argv);
void	spawn_fruit(t_snake *s);

#endif
