PORT = 59620
FLAGS = -DPORT=$(PORT) -g -Wall -std=gnu99
DEPENDENCIES = hash.h ftree.h client.h server.h


all: rcopy_client rcopy_server

rcopy_client: rcopy_client.o ftree.o hash_functions.o client_functions.o server_functions.o
	gcc ${FLAGS} -o $@ $^

rcopy_server: rcopy_server.o ftree.o hash_functions.o client_functions.o server_functions.o
	gcc ${FLAGS} -o $@ $^

%.o: %.c ${DEPENDENCIES}
	gcc ${FLAGS} -c $<

clean:
	rm *.o rcopy_client rcopy_server
	chmod 755 test/sandbox
	chmod 755 test/sandbox/*
	rm -rf test/sandbox

ls:
	chmod 755 test/sandbox
	chmod 755 test/sandbox/*
	ls -R test/sandbox

server:
	chmod 777 sandbox && rm -r sandbox
	clear
	./rcopy_server .

client:
	clear
	./rcopy_client adir localhost

debug:
	chmod 777 sandbox && rm -r sandbox
	gdb ./rcopy_server
