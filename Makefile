all: conan_cap

conan_cap: conan_cap.c Makefile
	gcc -Wall -g conan_cap.c -o conan_cap
