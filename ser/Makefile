OBJ	:=$(patsubst %.c, %.o, $(wildcard *.c))
CC := gcc
CFLAGS := -g -Wall 
LIBS := -pthread 
TARGET := ser

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
clean:
	@rm -rf *.o $(TARGET)

