sha: sha256.c
	gcc -march=native -O3 -Ofast -w -o sha sha256.c -lm -lpthread
