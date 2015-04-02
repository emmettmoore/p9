/* basic test of queue without concurrency */
#define THREEINCH
#include "stuff.h"
#include <stdio.h>

#define NENTS 10
#define QLIMIT 256*1024
#define ENTSIZE 64

long sleepsem = 0;
Proc *up;

void main(void)
{
	int pid, val, i;
	char src[ENTSIZE ];
	char dest[ENTSIZE];

	print("basic starting\n");
	for(i = 0; i < ENTSIZE; i++)
		src[i] = '!' + i;
	src[ENTSIZE - 1] = '\0';
	up = malloc(sizeof(*up));
	Queue *q = qopen(QLIMIT, 0, 0, 0);

	for(i = 0; i < NENTS; i++){
		val = qwrite(q, src, ENTSIZE);
		if(val != ENTSIZE)
			fprintf(stderr, "P: qwrite returned %d, expecting %d\n", val, ENTSIZE);
	}

	for(i = 0; i < NENTS; i++){
		val = qread(q, dest, ENTSIZE);
		if(val != ENTSIZE)
			fprintf(stderr, "C: qread returned %d, expecting %d\n", val, ENTSIZE);
	}
	print("basic complete\n");
}
