CC = gcc -std=gnu99

test:test_simple.o coroutine.o 
	$(CC) *.o -o test -lpthread

test_simple.o:test_simple.c
	$(CC) -c -g test_simple.c

coroutine.o:coroutine.c coroutine.h
	$(CC) -c -g coroutine.c

clean:
	rm *.o test