CFLAGS = -O3 -Wall
LIBS = -lpng
MAIN = ping-draw

all: $(MAIN)

$(MAIN): $(MAIN).o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f $(MAIN) *.o
