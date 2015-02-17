#define THREEINCH
#include "stuff.h"
#include <stdio.h>

void
main(void) {
	char *buf = "hello";
	char c;
	Queue *q;
	q = qopen(1024, 0, 0, 0);
	qwrite(q, buf, 5);
	print("hey\n");
	qread(q, &c, 1);
	print("did\n");
	printf("%s\n", buf);
	printf("buf2: %c\n", c);
	print("woooo! compiles and links !!!!\n");
	exits(0);
}
