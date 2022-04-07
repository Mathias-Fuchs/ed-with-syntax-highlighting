debug:
	rm *.o -f
	g++ -c sh.cpp -Wall -Wpedantic -g -O0
	gcc -c *.c -Wall -Wpedantic -g -O0
	g++ *.o -lsource-highlight -g -O0 -o ed



release:
	rm *.o -f
	g++ -c sh.cpp  -Ofast
	gcc -c *.c -Ofast
	g++ *.o -lsource-highlight -flto -Ofast -o ed
