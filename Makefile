CC=cc
CFLAGS=-std=c23 -Wall

http-service: http-service.c basic.c basic.h
	$(CC) -o http-service http-service.c basic.c $(CFLAGS)
