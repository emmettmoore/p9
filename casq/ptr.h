/*
 * userspace macros for storing counters in unused pointer bits
 *
 * pointers in userspace are under 1fffffb8, so the first three bits are
 * maskable (depending on pointer size)
 */

/* amd64 kernels macros for storing counters in unused pointer bits
 *
 * the 20 or so least significant bits of pointers in the kernel are maskable,
 * so we use 16 bits to store the counter
 */

#define PTRSCREEN 0xFFFFFFFFFFFF0000
#define PTRLEN    32
#define PTRHDRLEN 3

#define PTR(p)              ((Block*) ((int)p & PTRSCREEN))
#define PTRINC(p)           ((Block*) ((int)p +  (1 << (PTRLEN - PTRHDRLEN))))
#define PTRCNT(p)           ((Block*) ((int)p & ~PTRSCREEN))
#define PTRCOMBINE(p1, p2)  ((Block*) ((int)p1 | ((int) PTRCNT(PTRINC(p2)))))

