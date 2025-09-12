MAIN=http-service
CC=cc
CFLAGS=-Wall -g -O0
LIBS= 

$(MAIN): $(MAIN).c http.o basic.o config.o
	$(CC) -o $(MAIN) $(MAIN).c http.o basic.o config.o $(CFLAGS) $(LIBS)

http.o: http.c http.h
	$(CC) -c -o $@ $< $(CFLAGS)

basic.o: basic.c basic.h
	$(CC) -c -o $@ $< $(CFLAGS)

config.o: config.c config.h
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm -f $(MAIN) $(MAIN).o http.o basic.o dbconfig.o config.o
