CC      = gcc
CFLAGS  = -Wall -Wextra -g
LIBS    = -lpthread
TARGET  = server
SRCS    = server.c logger.c http_server.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LIBS)

clean:
	rm -f $(TARGET)

.PHONY: clean