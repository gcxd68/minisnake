# --- Names and Paths ---
NAME =			bin/minisnake
SERVER_NAME =	bin/server
OBJ_DIR =		obj/
BIN_DIR =		bin/
SRC_DIR =		src/
INC_DIR =		include/

# --- Compilation Flags ---
CC =		cc

# CFLAGS: Instructions for compiling .c into .o
# -Wall -Wextra -Werror : strict warnings, treated as errors
# -pedantic				: enforce strict ISO C standard compliance
# -O3					: aggressive compiler optimizations
# -flto					: link-time optimization (enables cross-file optimization)
# -pthread				: Enables POSIX threads for non-blocking HTTP requests
# -I$(INC_DIR)			: Look for headers in the include folder
CFLAGS =	-Wall -Wextra -Werror -pedantic -O3 -flto -pthread -I$(INC_DIR)

# Automatically enable ONLINE_BUILD macro if the 'net' configuration file exists
ifneq ($(wildcard net),)
	CFLAGS += -DONLINE_BUILD
endif

# LDFLAGS: Instructions for the final linking stage
# -flto					: Must be present here for link-time optimization to work
# -s					: strip debug symbols (hardens against reverse engineering)
# -pthread				: Must be included in linking stage as well
LDFLAGS =	-flto -s -pthread

# --- Source Files ---
SRCS =		$(SRC_DIR)minisnake_game.c \
			$(SRC_DIR)minisnake_sys.c \
			$(SRC_DIR)minisnake_net.c

# Transform src/file.c into obj/file.o
OBJS =		$(SRCS:$(SRC_DIR)%.c=$(OBJ_DIR)%.o)

# --- Rules ---
.PHONY: all clean fclean re server clean-server fclean-server re-server clean-all fclean-all re-all

.SILENT:

all: $(NAME)

# Link the client binary into the bin/ directory
$(NAME): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJS) $(LDFLAGS) -o $(NAME)

# Compile the Go server
server:
	@cd server && go mod tidy
	@cd server && CGO_ENABLED=1 go build -ldflags="-w -s" -o ../bin/server .

# Auto-generate net.h from 'net' config file in the include directory
$(INC_DIR)net.h:
	@if [ -f net ]; then \
		echo "/* Auto-generated file - DO NOT EDIT */" > $(INC_DIR)net.h; \
		echo "#ifndef NET_H" >> $(INC_DIR)net.h; \
		echo "# define NET_H\n" >> $(INC_DIR)net.h; \
		awk -F'=' 'NF==2 && !/^#/ {gsub(/^[ \t]+|[ \t]+$$/, "", $$1); gsub(/^[ \t]+|[ \t]+$$/, "", $$2); printf "# define %s \"%s\"\n", $$1, $$2}' net >> $(INC_DIR)net.h; \
		echo "\n#endif" >> $(INC_DIR)net.h; \
	else \
		echo "/* No net file found - Offline mode */" > $(INC_DIR)net.h; \
		echo "#ifndef NET_H\n# define NET_H\n#endif" >> $(INC_DIR)net.h; \
		echo "Notice: No 'net' file found, building in offline mode."; \
	fi

# Compile .o files into the obj/ directory
$(OBJ_DIR)%.o: $(SRC_DIR)%.c $(INC_DIR)minisnake.h $(INC_DIR)net.h
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Client Cleanup
clean:
	@rm -rf $(OBJ_DIR)

fclean: clean
	@rm -rf $(BIN_DIR)
	@rm -f $(INC_DIR)net.h

re: fclean all

# Server Cleanup
clean-server:
	@if [ -f bin/server ]; then cd server && go clean 2>/dev/null || true; fi

fclean-server: clean-server
	@rm -f bin/server

re-server: fclean-server server

# Global Cleanup
clean-all: clean clean-server
fclean-all: fclean fclean-server
re-all: fclean-all all server
