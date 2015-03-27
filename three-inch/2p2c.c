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
        switch(pid = rfork(RFPROC|RFMEM)){
            case -1: /* fork failed */
                fprintf(stderr, "ABORT!\n");
                abort();
            case 0: /* child2: consumer */
                SLEEP(1);
                for(i = 0; i < NENTS; i++){
                    val = qread(q, dest, ENTSIZE);
                    if(i % 256)
                        printf("Child consumer PID: %d\n", getpid());
                }
                break;
            default: /* child1: consumer */
                SLEEP(1);
                for(i = 0; i < NENTS; i++){
                    val = qread(q, dest, ENTSIZE);
                    if(i % 256)
                        printf("Child consumer PID: %d\n", getpid());
                }
            waitpid();
            break;
        }
        break;
	default: /* parent: producer */
        switch(pid = rfork(RFPROC|RFMEM)){
            case -1: /* fork failed */
                fprintf(stderr, "ABORT!\n");
                abort();
            case 0: /* child: producer */
                for(i = 0; i < NENTS; i++){
                    val = qwrite(q, buf, ENTSIZE);
                    if(i % 256)
                        printf("Child producer PID: %d\n", getpid());
                }
                print("exiting child produce\n");
          //      exits("closing child producer\n");
                break;
            default: /* parent: producer */
                for(i = 0; i < NENTS; i++){
                    val = qwrite(q, buf, ENTSIZE);
                    if(i % 256)
                        printf("(default) Parent producer PID: %d\n", getpid());
                }

                waitpid();
                waitpid();
                print("after waitpid\n");
        }
	}
    printf("pid %d exiting\n", getpid());

}

