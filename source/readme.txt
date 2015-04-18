#include<signal.h>
typedef void (*sighandler_t)(int);
sighander_t signal (int signum, sighandler_t hander);
