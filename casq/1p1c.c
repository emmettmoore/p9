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
    Block *b; 

	print("1p1c starting\n");
	for(i = 0; i < ENTSIZE; i++)
		src[i] = '!' + i;
	src[ENTSIZE - 1] = '\0';
	up = malloc(sizeof(*up));
	CasQueue *q = qopen();

	switch(pid = rfork(RFPROC|RFMEM)){
	case -1: /* fork failed */
		fprintf(stderr, "ABORT!\n");
		abort();
	case 0: /* child: consumer */
		for(i = 0; i < NENTS; i++){
			b = casqget(q);
            print("dest: %s", b->rp);
		}
		break;
	default: /* parent: producer */
		for(i = 0; i < NENTS; i++){
            sprint(src, "this is the contents of block %d\n", i);
            b = allocb(ENTSIZE);
			casqput(q, b);
		}
		waitpid();
		print("1p1c complete\n");
	}
}
