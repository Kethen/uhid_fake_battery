#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#define LOG(...){ \
	fprintf(stderr, __VA_ARGS__); \
	fflush(stderr); \
}

void run_hwclock(){
	pid_t pid = fork();
	if(pid == -1){
		LOG("failed forking, terminating\n");
		exit(1);
	}
	if(pid != 0){
		waitpid(pid, NULL, 0);
		return;
	}
	const char *name = "/system/bin/hwclock";
	const char *args[3] = {0};
	args[0] = name;
	args[1] = "-s";
	setsid();
	int ret = execv(name, (char **)args);
	LOG("failed execv on hwclock, %d\n", errno);
	exit(1);
}

int main(int argc, const char **argv){
	int interval = 10;
	if(argc >= 2){
		interval = atoi(argv[1]);
	}
	LOG("beginning hwclock -s loop with %ds intervals\n", interval);
	while(true){
		run_hwclock();
		sleep(interval);
	}
}
