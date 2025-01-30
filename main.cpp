#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#include <linux/uhid.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

pthread_mutex_t log_mutex;
#define LOG(...){ \
	pthread_mutex_lock(&log_mutex); \
	fprintf(stderr, __VA_ARGS__); \
	fflush(stderr); \
	pthread_mutex_unlock(&log_mutex); \
}

int lock_and_read(pthread_mutex_t *mutex, int fd, void *dst, size_t read_size){
	pthread_mutex_lock(mutex);
	int ret = read(fd, dst, read_size);
	pthread_mutex_unlock(mutex);
	return ret;
}

int lock_and_write(pthread_mutex_t *mutex, int fd, void *src, size_t write_size){
	pthread_mutex_lock(mutex);
	int ret = write(fd, src, write_size);
	pthread_mutex_unlock(mutex);
	return ret;
}

struct context {
	int fd;
	pthread_mutex_t *fd_mutex;
	uint8_t battery_level;
	uint8_t charging;
	uint16_t port;
};

void send_data(struct context *ctx){
	struct uhid_event outgoing_event;
	int ret;

	outgoing_event.type = UHID_INPUT2;
	outgoing_event.u.input2.size = 2;
	outgoing_event.u.input2.data[0] = ctx->battery_level;
	outgoing_event.u.input2.data[1] = ctx->charging;
	ret = lock_and_write(ctx->fd_mutex, ctx->fd, (void *)&outgoing_event, sizeof(outgoing_event));
	if(ret != sizeof(outgoing_event)){
		LOG("cannot write data, %d, terminating\n", errno);
		exit(1);
	}
}

struct con_context {
	struct context *main_ctx;
	int con_sock;
};

void *con_thread(void *args){
	struct con_context *ctx = (struct con_context *)args;
	int ret;
	uint8_t *target;
	uint8_t min;
	uint8_t max;

	while(true){
		char buf[64] = {0};
		ret = read(ctx->con_sock, buf, sizeof(buf));
		if(ret <= 0){
			break;
		}

		buf[sizeof(buf) - 1] = '\0';
		LOG("received tcp command %s\n", buf);
		switch(buf[0]){
			case 'l':
				target = &ctx->main_ctx->battery_level;
				min = 0;
				max = 100;
				break;
			case 'c':
				target = &ctx->main_ctx->charging;
				min = 0;
				max = 1;
				break;
			default:
				continue;
		}

		int input = atoi(&buf[1]);
		if(input > max){
			input = max;
		}
		if(input < min){
			input = min;
		}

		*target = input;

		send_data(ctx->main_ctx);
	}

	close(ctx->con_sock);
	free(ctx);
	return NULL;
}

void *data_thread(void *args){
	struct context *ctx = (struct context *)args;

	int ret;

	int tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
	if(tcp_sock < 0){
		LOG("failed opening tcp socket, %d, terminating\n", errno);
		exit(1);
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(ctx->port);

	ret = bind(tcp_sock, (struct sockaddr*)&addr, sizeof(addr));
	if(ret != 0){
		LOG("failed binding tcp socket, %d, terminating\n", errno);
		exit(1);
	}

	ret = listen(tcp_sock, 10);
	if(ret != 0){
		LOG("failed setting tcp socket to passive, %d, terminating\n", errno);
		exit(1);
	}

	LOG("listening on tcp port %u\n", ctx->port);

	do{
		int con_sock = accept(tcp_sock, (struct sockaddr*)NULL, NULL);
		if(con_sock < 0){
			LOG("failed accepting connection, %d, terminating\n", errno);
			exit(1);
		}

		struct con_context *con_ctx = (struct con_context *)malloc(sizeof(struct con_context));
		if(con_ctx == NULL){
			LOG("failed allocating connection context, terminating\n");
			exit(1);
		}

		con_ctx->main_ctx = ctx;
		con_ctx->con_sock = con_sock;

		pthread_t ct;
		ret = pthread_create(&ct, NULL, con_thread, con_ctx);
		if(ret != 0){
			LOG("failed creating connection thread, terminating\n");
			exit(1);
		}

		pthread_detach(ct);
	}while(true);

	return NULL;
}

void *incoming_thread(void *args){
	struct context *ctx = (struct context *)args;

	struct uhid_event incoming_event;
	struct uhid_event outgoing_event;

	while(true){
		sleep(0);
		int ret = read(ctx->fd, (void *)&incoming_event, sizeof(incoming_event));
		if(ret != sizeof(incoming_event)){
			if(errno == EAGAIN){
				continue;
			}
			LOG("cannot read incoming event, %d, terminating\n", errno);
			exit(1);
		}
		switch(incoming_event.type){
			case UHID_START:
			case UHID_STOP:
			case UHID_CLOSE:
			case UHID_OUTPUT:
				break;
			case UHID_OPEN:{
				send_data(ctx);
				break;
			}
			case UHID_GET_REPORT:{
				outgoing_event.type = UHID_GET_REPORT_REPLY;
				outgoing_event.u.get_report_reply.id = incoming_event.u.get_report.id;
				outgoing_event.u.get_report_reply.err = 0;
				outgoing_event.u.get_report_reply.size = 2;
				outgoing_event.u.get_report_reply.data[0] = ctx->battery_level;
				outgoing_event.u.get_report_reply.data[1] = ctx->charging;
				ret = lock_and_write(ctx->fd_mutex, ctx->fd, (void *)&outgoing_event, sizeof(outgoing_event));
				if(ret != sizeof(outgoing_event)){
					LOG("cannot write get report reply, %d, terminating\n", errno);
					exit(1);
				}
				break;
			}
			case UHID_SET_REPORT:{
				// reply nonsense
				outgoing_event.type = UHID_SET_REPORT_REPLY;
				outgoing_event.u.set_report_reply.id = incoming_event.u.set_report.id;
				outgoing_event.u.set_report_reply.err = 0;
				ret = lock_and_write(ctx->fd_mutex, ctx->fd, (void *)&outgoing_event, sizeof(outgoing_event));
				if(ret != sizeof(outgoing_event)){
					LOG("cannot write set report reply, %d, terminating\n", errno);
					exit(1);
				}
				break;
			}
			default:
				LOG("unknown event type %d, ignoring\n", incoming_event.type);
		}
	}

	return NULL;
}

void start_uhid_threads(struct context *ctx){
	int fd = open("/dev/uhid", O_RDWR);
	if(fd < 0){
		LOG("failed opening /dev/uhid, %d\n", errno);
		exit(1);
	}

	pthread_mutex_t fd_mutex;
	pthread_mutex_init(&fd_mutex, NULL);

	ctx->fd = fd;
	ctx->fd_mutex = &fd_mutex;

	#if 0
	uint8_t rd[] = {
		0x05, 0x85, // Usage Page (Battery System)
		0x09, 0x02, // Usage (Smart Battery Battery Status)
		0xA1, 0x01, //   Collection (Application)
		0x09, 0x65, //     Usage (Absolute State Of Charge)
		0x15, 0x00, //     Logical Minimum (0)
		0x25, 0x64, //     Logical Maximum (100)
		0x75, 0x08, //     Report Size (8)
		0x95, 0x01, //     Report Count (1)
		0x81, 0x02, //     Input (Data,Var,Abs)
		0x09, 0x44, //     Usage (Charging)
		0x15, 0x00, //     Logical Minimum (0)
		0x25, 0x64, //     Logical Maximum (1)
		0x75, 0x08, //     Report Size (8)
		0x95, 0x01, //     Report Count (1)
		0x81, 0x02, //     Input (Data,Var,Abs)
		0xC0 //         End Collection
	};
	#else
	uint8_t rd[] = {
		0x05, 0x0d, // Usage Page (Digitizer)
		0x09, 0x02, // Usage (Pen)
		0xA1, 0x01, //   Collection (Application)
		0x09, 0x3B, //     Usage (Battery Strength)
		0x15, 0x00, //     Logical Minimum (0)
		0x25, 0x64, //     Logical Maximum (100)
		0x75, 0x08, //     Report Size (8)
		0x95, 0x01, //     Report Count (1)
		0x81, 0x02, //     Input (Data,Var,Abs)
		0x09, 0xB1, //     Usage (Unknown)
		0x15, 0x00, //     Logical Minimum (0)
		0x25, 0x64, //     Logical Maximum (1)
		0x75, 0x08, //     Report Size (8)
		0x95, 0x01, //     Report Count (1)
		0x81, 0x02, //     Input (Data,Var,Abs)
		0xC0 //         End Collection
	};
	#endif

	struct uhid_event create_event = {
		.type = UHID_CREATE2,
		.u = {
			.create2 = {
				.rd_size = sizeof(rd),
				.bus = BUS_USB,
				.vendor = 0x1234,
				.product = 0x5678,
				.version = 1,
				.country = 0
			}
		}
	};
	strcpy((char *)create_event.u.create2.name, "uhid fake battery");
	strcpy((char *)create_event.u.create2.phys, "virtual");
	strcpy((char *)create_event.u.create2.uniq, "fake");
	memcpy(create_event.u.create2.rd_data, rd, sizeof(rd));

	int ret = write(fd, (void *)&create_event, sizeof(create_event));
	if(ret != sizeof(create_event)){
		LOG("failed creating uhid device, %d\n", errno);
		exit(1);
	}

	struct uhid_event incoming_event;
	do{
		ret = read(fd, (void *)&incoming_event, sizeof(incoming_event));
		if(ret != sizeof(incoming_event) && errno == EAGAIN){
			sleep(0);
			continue;
		}
		if(ret != sizeof(incoming_event)){
			LOG("failed receiving initial event, %d\n", errno);
			exit(1);
		}
		if(incoming_event.type == 0){
			sleep(0);
			continue;
		}
		break;
	}while(true);

	if(incoming_event.type != UHID_START){
		LOG("initial event is not UHID_START but %d\n", incoming_event.type);
		exit(1);
	}

	LOG("uhid begin, dev_flags 0x%016x\n", incoming_event.u.start.dev_flags);

	pthread_t dt;
	ret = pthread_create(&dt, NULL, data_thread, ctx);
	if(ret != 0){
		LOG("failed creating data thread, %d, terminating\n", ret);
		exit(1);
	}

	pthread_t it;
	ret = pthread_create(&dt, NULL, incoming_thread, ctx);
	if(ret != 0){
		LOG("failed creating incoming thread, %d, terminating\n", ret);
		exit(1);
	}

	pthread_join(dt, NULL);
	pthread_join(it, NULL);
}

void print_usage(const char *path){
	LOG("usage: %s [tcp port number]\n", path);
}

int main(int argc, const char **argv){
	struct context ctx = {
		.battery_level = 0x32,
		.charging = 0,
		.port = 7777
	};

	if(argc > 2){
		print_usage(argv[0]);
		exit(1);
	}

	if(argc == 2){
		int input = atoi(argv[1]);
		if(input == 0){
			print_usage(argv[0]);
			exit(1);
		}
		ctx.port = input;
	}

	pthread_mutex_init(&log_mutex, NULL);
	start_uhid_threads(&ctx);
}
