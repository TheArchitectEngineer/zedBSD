#include <stdio.h>
#include <pthread.h>
__thread int counter = 42;
__thread char name[16] = "main";
static void *worker(void *ignored) {
	(void)ignored;
	printf("thread counter=%d name=%s\n", counter, name);
	counter += 100;
	printf("thread after=%d\n", counter);
	return NULL;
}
int main(void) {
	pthread_t t;
	counter++;
	printf("main counter=%d name=%s\n", counter, name);
	pthread_create(&t, NULL, worker, NULL);
	pthread_join(t, NULL);
	printf("main again=%d\n", counter);
	return 0;
}
