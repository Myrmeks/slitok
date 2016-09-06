all: slitok_int slitok_all

slitok_int: slitok_int.cpp
	g++ -o slitok_int -O2 -Wall -std=gnu++11 slitok_int.cpp -lpthread

slitok_all: slitok_all.cpp
	g++ -o slitok_all -O2 -Wall -std=gnu++11 slitok_all.cpp -lpthread

clean:
	rm -f slitok_int
	rm -f slitok_all
