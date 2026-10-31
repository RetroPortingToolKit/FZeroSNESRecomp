/* Single-threaded ROM-free mixer/provider tests. Game hosts supply the real lock. */
#include <assert.h>
static unsigned depth;
void RtlApuLock(void) {++depth;}
void RtlApuUnlock(void) {assert(depth);--depth;}
