all: slitok_int

slitok_int: slitok_int.cpp
	g++ -o slitok_int -O2 -Wall -std=gnu++11 slitok_int.cpp -lpthread

clean:
	rm -f slitok_int
