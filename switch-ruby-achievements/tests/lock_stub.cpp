#include "pt_lock.h"
bool pt_lock_acquire(int) { return true; }
void pt_lock_release(void) {}
int pt_lock_last_errno(void) { return 0; }
