debug:
	rm *.o -f
	g++ -c src/sh.cpp -Wall -Wpedantic -g -O0
	gcc -c src/*.c -Wall -Wpedantic -g -O0
	g++ *.o -lsource-highlight -g -O0 -o ed



release:
	rm *.o -f
	g++ -c src/sh.cpp  -Ofast
	gcc -c src/*.c -Ofast
	g++ *.o -lsource-highlight -flto -Ofast -o ed
