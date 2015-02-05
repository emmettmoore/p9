#include    <u.h>
#include	<libc.h> // contains wrong "sleep" function. qio.c line 873
//#include    <lib.h>
#define KNAMELEN 28 /* from lib.h */
#include	"mem.h"
#include	"dat.h"
#include	"portfns.h" // XXX Why not fns.h?
#include	"error.h"
