all : test testperform client server

test : test.c coroutine.c
	gcc -g -Wall -o $@ $^

testperform : testperform.c coroutine.c
	gcc -g -Wall -o $@ $^

client : client.c 
	gcc -g -Wall -o $@ $^ -lunp

server : server.c coroutine.c
	gcc -g -Wall -o $@ $^


clean :
	rm test testperform client server
