NAME	= minisnake
CC		= cc
CFLAGS	= -Wall -Wextra -Werror
SRCS	= minisnake_core.c minisnake_sys.c minisnake_net.c
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