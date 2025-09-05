SRC =		minisnake_core.c \
			minisnake_init.c

OBJ =		$(SRC:.c=.o)

NAME =		minisnake
CC =		cc
CFLAGS =	-Wall -Wextra -Werror

all:		$(NAME)

$(NAME):	$(OBJ)
			$(CC) $(CFLAGS) -o $(NAME) $(OBJ)

clean:
			rm -f $(OBJ)

fclean:		clean
			rm -f $(NAME)

re:			fclean all

.PHONY:		all clean fclean re
