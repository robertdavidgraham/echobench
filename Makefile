

bin/echobench: src/*.c
	gcc src/*.c -lrt -lpthread -o bin/echobench
