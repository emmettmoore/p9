#define THREEINCH
#include "stuff.h"
#include <stdio.h>

void
main(void) {
    print("in main, haven't done anything\n");
	char *buf = "hello";
	char c;
	Queue *q;
    print("declared some shit\n");
	q = qopen(1024, 0, 0, 0);
    print("qopen success\n");
	qwrite(q, buf, 5);
	print("qwrite success\n");
	qread(q, &c, 1);
	print("qread success\n");
	printf("%s\n", buf);
	printf("buf2: %c\n", c);
	print("woooo! three inchez!!!!\n");
	exits(0);
}
