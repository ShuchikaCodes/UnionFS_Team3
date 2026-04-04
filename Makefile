CC      = gcc
CFLAGS  = $(shell pkg-config --cflags fuse3) -Wall -g
LDFLAGS = $(shell pkg-config --libs fuse3)

TARGET  = mini_unionfs

all: $(TARGET)

$(TARGET): src/*.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

mount: $(TARGET)
	mkdir -p lower upper mnt
	./$(TARGET) lower upper mnt

unmount:
	fusermount -u mnt

clean:
	rm -f $(TARGET)
	fusermount -u mnt 2>/dev/null; rm -rf lower upper mnt