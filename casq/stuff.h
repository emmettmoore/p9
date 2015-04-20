#include    "lib/u.h"
#include	"lib/libc.h"
#define KNAMELEN 28 /* from lib.h */
#define SLEEP(x) for(sleep_i=0; sleep_i<77*x; sleep_i++) {print(" \b");}
#include	"mem.h"
#include	"dat.h"
#include	"fns.h" // XXX Why not fns.h?
#include	"error.h"

typedef struct CasQueue CasQueue; //portdat.h
CasQueue* casqopen(int limit); 
Block* casqget(CasQueue *q); 
int casqput(CasQueue *q, Block *b); 
int casqsize(CasQueue *q);
void casqfree(CasQueue *q); 
void casqclose(CasQueue *q); 
void casqsetlimit(CasQueue *q, int limit);
extern long sleepsem;
extern Proc *up;
extern Block* allocb(int size);

