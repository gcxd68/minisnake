NAME    = minisnake
CC      = cc

# CFLAGS: Instructions for compiling .c into .o
# -Wall -Wextra -Werror : strict warnings, treated as errors
# -O3                   : aggressive compiler optimizations
# -flto                 : link-time optimization (enables cross-file optimization)
CFLAGS  = -Wall -Wextra -Werror -O3 -flto

# LDFLAGS: Instructions for the final linking stage
# -s                    : strip debug symbols (hardens against reverse engineering)
# -flto                 : must be repeated here to finalize cross-file optimizations
LDFLAGS = -flto -s

SRCS    = minisnake_game.c minisnake_sys.c minisnake_net.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean fclean re

.SILENT:

all: $(NAME)

$(NAME): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(NAME)

net.h:
	@if [ -f net ]; \
	then \
		echo "/* Auto-generated file - DO NOT EDIT */" > net.h; \
		echo "#ifndef NET_H" >> net.h; \
		echo "# define NET_H\n" >> net.h; \
		awk -F'=' 'NF==2 && !/^#/ {gsub(/^[ \t]+|[ \t]+$$/, "", $$1); gsub(/^[ \t]+|[ \t]+$$/, "", $$2); printf "# define %s \"%s\"\n", $$1, $$2}' net >> net.h; \
		echo "\n#endif" >> net.h; \
	else \
		echo "No 'net' file found, building in offline mode."; \
	fi

%.o: %.c minisnake.h net.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS)

fclean: clean
	rm -f $(NAME) net.h

re: fclean all
