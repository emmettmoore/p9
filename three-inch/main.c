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
	char *buf = "abcdefgh";
	char *buf2 = "00000000";
	char src[ENTSIZE ];
	char dest[ENTSIZE];
	for(i = 0; i < ENTSIZE; i++)
		src[i] = '!' + i;
	src[ENTSIZE - 1] = '\0';
	printf("payload: %s\n", src);
	up = malloc(sizeof(*up));
	Queue *q = qopen(QLIMIT, 0, 0, 0);

	switch(pid = rfork(RFPROC|RFMEM)){
	case -1: /* fork failed */
		fprintf(stderr, "ABORT!\n");
		abort();
	case 0: /* child: consumer */
//		print("hey\n");			// NEED this or else it stalls forevar....
		for(i = 0; i < NENTS; i++){
			val = qread(q, dest, ENTSIZE);
			if(i % 256)
				printf("C: %d: qread returned %d\n", i, val);
		}
		break;
	default: /* parent: producer */
		SLEEP(0);
		for(i = 0; i < NENTS; i++){
			val = qwrite(q, buf, ENTSIZE);
			if(i % 256)
				printf("P: %d: qwrite returned %d\n", i, val);
		}
		waitpid();
	}
}
