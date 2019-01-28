test:test.c
	gcc -g -fsanitize=address -o test test.c
bug:bug.c
	gcc -g -fsanitize=address -o bug bug.c
clinet:client.c
	gcc -o client client.c

server:server.c
	gcc -o server server.c

udpserver:udpserver.c
	gcc -o udpserver udpserver.c -fsanitize=address
