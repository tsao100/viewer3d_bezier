# Makefile for viewer3d_bezier

CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lXm -lXt -lX11 -lm

TARGET = viewer3d_bezier
SRC = viewer3d_bezier.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
