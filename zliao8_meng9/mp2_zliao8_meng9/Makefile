# CS 425 Distributed System
# University of Illinois
# MP2
# Zihan Liao - zliao8
# Meng Qinglei - meng9

CC = gcc
INC = -I. -Ilibs
FLAGS = -g -W -Wall
LIBS = -lpthread

all: server

server: libdictionary.o libhttp.o queue.o server.c
	$(CC) $(FLAGS) $(INC) $^ -o $@ $(LIBS)

libdictionary.o: libs/libdictionary.c libs/libdictionary.h
	$(CC) -c $(FLAGS) $(INC) $< -o $@ $(LIBS)

libhttp.o: libs/libhttp.c libs/libhttp.h
	$(CC) -c $(FLAGS) $(INC) $< -o $@ $(LIBS)

queue.o: queue.c queue.h
	$(CC) -c $(FLAGS) $(INC) $< -o $@ $(LIBS)

clean:
	$(RM) -r *.o server