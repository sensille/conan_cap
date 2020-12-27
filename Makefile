all: conan_cap

conan_cap: conan_cap.c model1.c model2.c gcode.c Makefile
	gcc -Wall -g conan_cap.c model1.c model2.c gcode.c -o conan_cap
