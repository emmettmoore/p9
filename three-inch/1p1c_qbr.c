
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
	char src[ENTSIZE];
	char dest[ENTSIZE];
    char *data;
    Block *b = allocb(ENTSIZE);


	print("1p1c starting\n");
	for(i = 0; i < ENTSIZE; i++)
		src[i] = '!' + i;
	src[ENTSIZE - 1] = '\0';
	up = malloc(sizeof(*up));
	Queue *q = qopen(QLIMIT, 0, 0, 0);

	switch(pid = rfork(RFPROC|RFMEM)){
	case -1: /* fork failed */
		fprintf(stderr, "ABORT!\n");
		abort();
	case 0: /* child: consumer */
		for(i = 0; i < NENTS; i++){
			b = qbread(q, 999);
            data = (char *)b->rp;
            print("data: %s", data);
		}
		break;
	default: /* parent: producer */
		for(i = 0; i < NENTS; i++){
            sprint(src, "this is the contents of block %d\n", i);
			val = qwrite(q, src, ENTSIZE);
			if(val != ENTSIZE)
				fprintf(stderr, "P: qwrite returned %d, expecting %d\n", val, ENTSIZE);
		}
		waitpid();
		print("1p1c complete\n");
	}
}
