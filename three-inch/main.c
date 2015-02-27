#define THREEINCH
#include "stuff.h"
#include <stdio.h>

void main(void)
{
	int pid, val, i;
	char c;
	char *buf = "hello";
	char *buf2 = "a";
	Queue *q = qopen(1024, 0, 0, 0);
	qwrite(q, buf2, 1);

	switch(pid = rfork(RFPROC|RFMEM)){
	case -1: /* fork failed */
		print("ABORT!\n");
		abort();
	case 0: /* child: consumer */
		printf("C: here!\n");
		for(i = 0; i < 5; i++){
			val = qread(q, &c, 1);
			printf("C: qread returned %d\n", val);
			printf("C: read %c\n", c);
		}
		break;
	default: /* parent: producer */
		val = qwrite(q, buf, 5);
		printf("P: qwrite returned %d\n", val);
		printf("P: wrote %s\n", buf);
		waitpid();
	}
}
