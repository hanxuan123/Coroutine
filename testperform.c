#include <time.h>
#include "coroutine.h"
#include <stdio.h>

struct args {
	int n;
};

static void foo(struct scheduler *s, void *ud) {
	for (int i = 0; i < 100000; i++) {
		coroutine_yield(s);
	}
}

static void foo2(struct scheduler *s, void *ud) {
	for (int i = 0; i < 100000; i++) {
		coroutine_yield(s);
	}
}

static void test(struct scheduler *s) {
    struct args arg1 = { 0 };
    struct args arg2 = { 200 };

    int co1 = coroutine_new(s, foo, &arg1);
    int  co2 = coroutine_new(s, foo, &arg2);
    printf("switch times test start!\n");
    clock_t start = clock();
    while (coroutine_status(s,co1) && coroutine_status(s,co2)) {
		coroutine_resume(s,co1);
		coroutine_resume(s,co2);
	} 
    clock_t end = clock();
    double swicth_times = 200000 / ((double)(end - start) / CLOCKS_PER_SEC);
    printf("switch times test end!\n");
    printf("It can switch %lf times per second\n", swicth_times);
}

int main() {
	struct schedule *S = coroutine_open();
	test(S);
	coroutine_close(S);
	return 0;
}