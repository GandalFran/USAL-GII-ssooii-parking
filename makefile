
linux: parking.c parking.h libparking_linux.a
	gcc -g parking.c libparking_linux.a -o p -lm -m32

solaris: parking.c parking.h libparking_solaris.a
	gcc -g parking.c libparking_solaris.a -o p -lm