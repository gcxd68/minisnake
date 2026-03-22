NAME          = minisnake
CC            = cc
CFLAGS        = -Wall -Wextra -Werror
SRCS          = minisnake_core.c minisnake_init.c minisnake_net.c
OBJS          = $(SRCS:.c=.o)

.PHONY: all clean fclean re
.SILENT:

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) -o $(NAME) $(OBJS)

# Generate keys.h if it doesn't exist or if obfuscator.py changed
keys.h: obfuscator.py
	if [ ! -f obfuscator.py ]; then \
		echo "\033[31mError: obfuscator.py not found.\033[0m"; \
		exit 1; \
	fi
	python3 obfuscator.py > keys.h

# Compile objects with a dependency on keys.h
%.o: %.c minisnake.h keys.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME) keys.h

re: fclean all