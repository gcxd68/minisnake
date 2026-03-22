NAME          = minisnake
CC            = cc
CFLAGS        = -Wall -Wextra -Werror
SRCS          = minisnake_core.c minisnake_init.c minisnake_net.c
OBJS          = $(SRCS:.c=.o)
ONLINE_COLS   = $(shell awk '/define[[:space:]]+ONLINE_WIDTH[[:space:]]/{print $$NF + 2}' minisnake.h)
ONLINE_ROWS   = $(shell awk '/define[[:space:]]+ONLINE_HEIGHT[[:space:]]/{print $$NF + 4}' minisnake.h)

.PHONY: all clean fclean re
.SILENT:

all: $(NAME) play.sh

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) -o $(NAME) $(OBJS)

# 1. Teach the Makefile how to generate the keys
keys.h: obfuscator.py
	if [ ! -f obfuscator.py ]; then \
		echo "\033[31mError: obfuscator.py not found.\033[0m"; \
		exit 1; \
	fi
	python3 obfuscator.py > keys.h

# 2. Force the compiler to wait until keys.h is ready
%.o: %.c minisnake.h keys.h
	$(CC) $(CFLAGS) -c $< -o $@

play.sh:
	printf '#!/bin/bash\n' > play.sh
	printf 'if ! command -v gnome-terminal &>/dev/null; then\n' >> play.sh
	printf '\tread -p "gnome-terminal is required but not installed. Install now? [y/N] " answer\n' >> play.sh
	printf '\tif [ "$$answer" != "y" ] && [ "$$answer" != "Y" ]; then\n' >> play.sh
	printf '\t\techo "Aborted."\n' >> play.sh
	printf '\t\texit 1\n' >> play.sh
	printf '\tfi\n' >> play.sh
	printf '\tsudo apt-get install -y gnome-terminal\n' >> play.sh
	printf 'fi\n' >> play.sh
	printf 'if [ "$$1" = "online" ]; then\n' >> play.sh
	printf '\tCOLS=$(ONLINE_COLS)\n' >> play.sh
	printf '\tROWS=$(ONLINE_ROWS)\n' >> play.sh
	printf 'elif [ "$$#" -eq 2 ]; then\n' >> play.sh
	printf '\tCOLS=$$(($$1 + 2))\n' >> play.sh
	printf '\tROWS=$$(($$2 + 4))\n' >> play.sh
	printf 'else\n' >> play.sh
	printf '\techo "Usage: ./play.sh online"\n' >> play.sh
	printf '\techo "       ./play.sh WIDTH HEIGHT"\n' >> play.sh
	printf '\texit 1\n' >> play.sh
	printf 'fi\n' >> play.sh
	printf 'DIR=$$(dirname "$$0")\n' >> play.sh
	printf 'TTY=$$(tty)\n' >> play.sh
	printf 'gnome-terminal --wait --geometry=$${COLS}x$${ROWS} --title="minisnake" -- bash -c "$$DIR/$(NAME) $$@ 2>$$TTY"\n' >> play.sh
	chmod +x play.sh

clean:
	rm -f $(OBJS)

# 3. Remove keys.h when cleaning up
fclean: clean
	rm -f $(NAME) play.sh keys.h

re: fclean all