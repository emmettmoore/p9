#include    "lib/u.h"
#include	"lib/libc.h"
#define KNAMELEN 28 /* from lib.h */
#define SLEEP(x) for(i=0; i<77*x; i++) {print(" \b");}
#include	"mem.h"
#include	"dat.h"
#include	"fns.h" // XXX Why not fns.h?
#include	"error.h"

extern long sleepsem;
extern Proc *up;