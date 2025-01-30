#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#define LOG(...){ \
	fprintf(stderr, __VA_ARGS__); \
	fflush(stderr); \
}

void read_from_file(const char *path, char *buf, size_t buf_size){
	int fd = open(path, O_RDONLY);
	if(fd < 0){
		LOG("failed opening %s for reading, %d, terminating\n", path, errno);
		exit(1);
	}

	int ret = read(fd, buf, buf_size);
	close(fd);
	if(ret < 0){
		LOG("failed reading %s, %d, terminating\n", path, errno);
		exit(1);
	}
}

void print_usage(const char *path){
	LOG("usage: %s [battery level file path] [charger online file path] [ip] [port]\n", path);
}

int main(int argc, const char **argv){
	char level_path_buf[256];
	char charging_path_buf[256];
	uint16_t port = 7777;
	uint32_t v4 = 0;
	uint8_t *v4_bytes = (uint8_t *)&v4;
	v4_bytes[0] = 127;
	v4_bytes[3] = 1;

	if(argc > 5){
		print_usage(argv[0]);
		exit(1);
	}

	if(argc >= 2){
		strcpy(level_path_buf, argv[1]);
	}else{
		strcpy(level_path_buf, "/sys/class/power_supply/BAT1/capacity");
	}

	if(argc >= 3){
		strcpy(charging_path_buf, argv[2]);
	}else{
		strcpy(charging_path_buf, "/sys/class/power_supply/ACAD/online");
	}

	if(argc >= 4){
		unsigned int input[4];
		sscanf(argv[3], "%u.%u.%u.%u", &input, &input[1], &input[2], &input[3]);
		v4_bytes[0] = input[0];
		v4_bytes[1] = input[1];
		v4_bytes[2] = input[2];
		v4_bytes[3] = input[2];
	}

	if(argc >= 5){
		port = atoi(argv[4]);
	}

	int tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(tcp_sock < 0){
		LOG("failed opening tcp socket, %d, terminating\n", errno);
		exit(1);
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = v4;
	addr.sin_port = htons(port);

	LOG("connecting to tcp %u.%u.%u.%u:%u\n", v4_bytes[0], v4_bytes[1], v4_bytes[2], v4_bytes[3], port);

	int ret = connect(tcp_sock, (struct sockaddr *)&addr, sizeof(addr));
	if(ret != 0){
		LOG("failed conencting tcp socket, %d, terminating\n", errno);
		exit(1);
	}

	while(true){
		char buf[64] = {0};
		buf[0] = 'l';
		read_from_file(level_path_buf, &buf[1], sizeof(buf) - 2);
		ret = write(tcp_sock, buf, strlen(buf));
		if(ret != strlen(buf)){
			LOG("failed writing to tcp socket, %d, terminating\n", errno);
			exit(1);
		}
		LOG("sent tcp command %s\n", buf);
		memset(buf, 0, sizeof(buf));
		buf[0] = 'c';
		read_from_file(charging_path_buf, &buf[1], sizeof(buf) - 2);
		ret = write(tcp_sock, buf, strlen(buf));
		if(ret != strlen(buf)){
			LOG("failed writing to tcp socket, %d, terminating\n", errno);
			exit(1);
		}
		LOG("sent tcp command %s\n", buf);
		sleep(10);
	}

	return 0;
}
