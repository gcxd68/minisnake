NAME          = minisnake
CC            = cc
CFLAGS        = -Wall -Wextra -Werror
SRCS          = minisnake_core.c minisnake_init.c minisnake_net.c
OBJS          = $(SRCS:.c=.o)
ONLINE_COLS   = $(shell awk '/define[[:space:]]+ONLINE_WIDTH[[:space:]]/{print $$NF + 2}' minisnake.h)
ONLINE_ROWS   = $(shell awk '/define[[:space:]]+ONLINE_HEIGHT[[:space:]]/{print $$NF + 4}' minisnake.h)

.PHONY: all clean fclean re

all: $(NAME) play.sh

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) -o $(NAME) $(OBJS)

%.o: %.c minisnake.h
	$(CC) $(CFLAGS) -c $< -o $@

play.sh:
	@printf '#!/bin/bash\n' > play.sh
	@printf 'if ! command -v gnome-terminal &>/dev/null; then\n' >> play.sh
	@printf '\tread -p "gnome-terminal is required but not installed. Install now? [y/N] " answer\n' >> play.sh
	@printf '\tif [ "$$answer" != "y" ] && [ "$$answer" != "Y" ]; then\n' >> play.sh
	@printf '\t\techo "Aborted."\n' >> play.sh
	@printf '\t\texit 1\n' >> play.sh
	@printf '\tfi\n' >> play.sh
	@printf '\tsudo apt-get install -y gnome-terminal\n' >> play.sh
	@printf 'fi\n' >> play.sh
	@printf 'if [ "$$1" = "online" ]; then\n' >> play.sh
	@printf '\tCOLS=$(ONLINE_COLS)\n' >> play.sh
	@printf '\tROWS=$(ONLINE_ROWS)\n' >> play.sh
	@printf 'elif [ "$$#" -eq 2 ]; then\n' >> play.sh
	@printf '\tCOLS=$$(($$1 + 2))\n' >> play.sh
	@printf '\tROWS=$$(($$2 + 4))\n' >> play.sh
	@printf 'else\n' >> play.sh
	@printf '\techo "Usage: ./play.sh online"\n' >> play.sh
	@printf '\techo "       ./play.sh WIDTH HEIGHT"\n' >> play.sh
	@printf '\texit 1\n' >> play.sh
	@printf 'fi\n' >> play.sh
	@printf 'gnome-terminal --geometry=$${COLS}x$${ROWS} --title="minisnake" -- "$$(dirname "$$0")/$(NAME)" "$$@" &\n' >> play.sh
	@printf 'disown && exit\n' >> play.sh
	@chmod +x play.sh
	@echo "Generated play.sh"

clean:
	@rm -f $(OBJS)

fclean: clean
	@rm -f $(NAME) play.sh

re: fclean all
