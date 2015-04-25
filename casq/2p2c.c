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
	for(i = 0; i < ENTSIZE; i++)
		src[i] = '!' + i;
	src[ENTSIZE - 1] = '\0';
	up = malloc(sizeof(*up));
	Queue *q = qopen(QLIMIT, 0, 0, 0);

	print("2p2c starting\n");
	switch(pid = rfork(RFPROC|RFMEM)){
	case -1: /* fork failed */
		fprintf(stderr, "ABORT!\n");
		abort();
	case 0: /* child: consumer */
		switch(pid = rfork(RFPROC|RFMEM)){
		case -1: /* fork failed */
			fprintf(stderr, "ABORT!\n");
			abort();
		case 0: /* child2: consumer */
			for(i = 0; i < NENTS; i++){
				val = qread(q, dest, ENTSIZE);
				print("consumer2 dest: %s", dest);
				if(val != ENTSIZE)
					fprintf(stderr, "C2: qread returned %d, expecting %d\n", val, ENTSIZE);
			}
			break;
		default: /* child1: consumer */
			for(i = 0; i < NENTS; i++){
				val = qread(q, dest, ENTSIZE);
				print("consumer1 dest: %s", dest);
				if(val != ENTSIZE)
					fprintf(stderr, "C1: qread returned %d, expecting %d\n", val, ENTSIZE);
			}
			waitpid();
		}
		break;
	default: /* parent: producer */
		switch(pid = rfork(RFPROC|RFMEM)){
		case -1: /* fork failed */
			fprintf(stderr, "ABORT!\n");
			abort();
		case 0: /* child: producer */
			for(i = 0; i < NENTS; i++){
				sprint(src, "P1: this is the contents of block %d\n", i);
				val = qwrite(q, src, ENTSIZE);
				if(val != ENTSIZE)
					fprintf(stderr, "P1: qwrite returned %d, expecting %d\n", val, ENTSIZE);
			}
			break;
		default: /* parent: producer */
			for(i = 0; i < NENTS; i++){
				sprint(src, "P2: this is the contents of block %d\n", i);
				val = qwrite(q, src, ENTSIZE);
				if(val != ENTSIZE)
					fprintf(stderr, "P2: qwrite returned %d, expecting %d\n", val, ENTSIZE);
			}
			waitpid();
			waitpid();
			print("2p2c complete\n");
		}
	}
}

