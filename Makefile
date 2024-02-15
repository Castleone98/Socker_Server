OBJS= 2020202096_semaphore_server.c
CC = gcc
EXEC = semaphore_server

all: $(OBJS)
	$(CC) -o $(EXEC) $^ -lpthread
clean:
	rm -rf $(EXEC)
