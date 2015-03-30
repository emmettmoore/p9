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

	print("1p2c starting\n");
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
			val = qread(q, dest, ENTSIZE);
			if(val != ENTSIZE)
				fprintf(stderr, "C1: qread returned %d, expecting %d\n", val, ENTSIZE);
		}
		break;
	default: /* parent: producer */
		switch(pid = rfork(RFPROC|RFMEM)){
			case -1: /* fork failed */
				fprintf(stderr, "ABORT!\n");
				abort();
			case 0: /* second consumer */
				for(i = 0; i < NENTS; i++){
					val = qread(q, dest, ENTSIZE);
					if(val != ENTSIZE)
						fprintf(stderr, "C2: qread returned %d, expecting %d\n", val, ENTSIZE);
				}
				break;
			default: /* parent: producer */
				for(i = 0; i < 2*NENTS; i++){
					val = qwrite(q, src, ENTSIZE);
					if(val != ENTSIZE)
						fprintf(stderr, "P: qwrite returned %d, expecting %d\n", val, ENTSIZE);
				}
				waitpid();
				waitpid();
				print("1p2c complete\n");
		}
	}
}
