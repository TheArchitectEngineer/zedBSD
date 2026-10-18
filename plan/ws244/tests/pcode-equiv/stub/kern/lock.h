struct mutex { int held; };
void mutex_lock(struct mutex *m);
void mutex_unlock(struct mutex *m);
