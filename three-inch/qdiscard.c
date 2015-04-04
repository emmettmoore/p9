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
	char c;
	char src[ENTSIZE ];
	char dest[ENTSIZE];
    Block *b;
    char *data;
    print("qdiscard starting\n");
	for(i = 0; i < ENTSIZE; i++)
		src[i] = '!' + i;
	src[ENTSIZE - 1] = '\0';
	up = malloc(sizeof(*up));
	Queue *q = qopen(QLIMIT, 0, 0, 0);

    for(i = 0; i < NENTS; i++){
        sprint(src, "this is the contents of block %d\n", i);
        val = qwrite(q, src, ENTSIZE);
        if(val != ENTSIZE)
            fprintf(stderr, "P: qwrite returned %d, expecting %d\n", val, ENTSIZE);
    }
    

    /* discard 1 block, then read again */
    val = qdiscard(q, ENTSIZE);
    val = qread(q, dest, ENTSIZE);
    if (val != ENTSIZE) {
        fprintf(stderr, "C: qread returned %d, expecting %d\n", val, ENTSIZE);
    }
    print("data: %s", dest);

    print("qdiscard complete\n");
}

