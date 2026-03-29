NAME	= minisnake
CC		= cc
# -Wall -Wextra -Werror : strict warnings, treated as errors
# -O3                   : aggressive compiler optimizations
# -flto                 : link-time optimization across translation units
# -s                    : strip debug symbols from the binary (hardens against reverse engineering)
CFLAGS	= -Wall -Wextra -Werror -O3 -flto -s
SRCS	= minisnake_game.c minisnake_sys.c minisnake_net.c
OBJS	= $(SRCS:.c=.o)

.PHONY: all clean fclean re

.SILENT:

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) -o $(NAME) $(OBJS)

keys.h:
	@if [ -f keys ]; then \
		python3 obfuscator.py > keys.h; \
	else \
		echo "No 'keys' file found, building in offline mode."; \
	fi

%.o: %.c minisnake.h keys.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME) keys.h

re: fclean all