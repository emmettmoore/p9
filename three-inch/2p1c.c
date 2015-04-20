// modified to match casq/2p1c

#define THREEINCH
#include "stuff.h"
#include <stdio.h>

#define NENTS 8000
#define QLIMIT 256*1024
#define ENTSIZE 64

long sleepsem = 0;
Proc *up;

void main(void)
{
	int pid, val, i, sleep_i;
	char c;
	char src[ENTSIZE ];
	char dest[ENTSIZE];
	Block *b;
//	print("2p1c starting\n");

//	for(i = 0; i < ENTSIZE; i++)
//		src[i] = '!' + i;
//	src[ENTSIZE - 1] = '\0';
	up = malloc(sizeof(*up));

	Queue *q = qopen(QLIMIT, 0, 0, 0);
	switch(pid = rfork(RFPROC|RFMEM)){
	case -1: /* fork failed */
		fprintf(stderr, "ABORT!\n");
		abort();
	case 0: /* child: consumer */
		for(i = 0; i < NENTS*2; i++){
			b = qget(q);
			if (b == nil) {
//				fprintf(stderr, "empty queue :(\n");
				i--;
			}
	//		else
	//			fprintf(stderr, "dest: %s", b->rp);
//			val = qread(q, dest, ENTSIZE);
//			print("dest: %s", dest);
//			if(val != ENTSIZE)
//				fprintf(stderr, "C: qread returned %d, expecting %d\n", val, ENTSIZE);
		}
//			fprintf(stderr, "child exiting\n");
		break;
	default: /* parent: producer */
		switch(pid = rfork(RFPROC|RFMEM)){
		case -1: /* fork failed */
			fprintf(stderr, "ABORT!\n");
			abort();
		case 0: /* child: producer */
			for(i = 0; i < NENTS; i++){
				snprint(src, 40, "P1: this is the contents of block %d\n", i);
				b = allocb(ENTSIZE);
//				if(b == nil)
//					fprintf(stderr, "P1 allocb returned nil!\n");
				memmove(b->wp, src, ENTSIZE);
				b->wp += ENTSIZE;
				val = qpassnolim(q, b);
			//	if(i % 100 == 0)
			//	if(val != 136)
			//		fprintf(stderr, "P1: qpassnolim returned %d\n", val);
//				val = qwrite(q, src, ENTSIZE);
//				if(val != ENTSIZE)
//					fprintf(stderr, "P1: qwrite returned %d, expecting %d\n", val, ENTSIZE);
			}
//			fprintf(stderr, "P1 exiting\n");
			break;
		default: /* parent: producer */
			for(i = 0; i < NENTS; i++){
				snprint(src, 40, "P2: this is the contents of block %d\n", i);
				b = allocb(ENTSIZE);
			//	if(b == nil)
			//		fprintf(stderr, "P2 allocb returned nil!\n");
				memmove(b->wp, src, ENTSIZE);
				b->wp += ENTSIZE;
				val = qpassnolim(q, b);
			//	if(i % 100 == 0)
	//			if(val != 136)
	//				fprintf(stderr, "P2: qpassnolim returned %d\n", val);
//				val = qwrite(q, src, ENTSIZE);
//				if(val != ENTSIZE)
//					fprintf(stderr, "P2: qwrite returned %d, expecting %d\n", val, ENTSIZE);
			}
	//		fprintf(stderr, "P2 done, waiting\n");
			waitpid();
			waitpid();
	//		print("2p1c complete\n");
		}
	}
}

