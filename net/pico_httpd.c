#include <common.h>
#include <command.h>
#include <fs.h>
#include <net.h>
#include <pico_stack.h>
#include <pico_ipv4.h>
#include <pico_socket.h>

enum {
	READ_START_LINE,
	READ_HEADER
};

static bool done;
static int state;
static char write_buf[4096];
static int writed, write_buf_len;
static struct pico_socket *cli;

static char *read_line(struct pico_socket *s)
{
	static char buf[128];
	char *p = buf;

	while (1) {
		if (pico_socket_read(s, p, 1) == 0)
			return NULL;

		if (*p == '\n') {
			p[1] = 0;
			return buf;
		}
		*(++p) = 0;
	}

	return NULL;
}

static void send_data(struct pico_socket *s)
{
    int w;

    while (writed < write_buf_len) {
        w = pico_socket_write(s, write_buf + writed, write_buf_len - writed);
        if (w <= 0)
			return;
		writed += w;
    }
}

static void cb_http(uint16_t ev, struct pico_socket *s)
{
	if (ev & PICO_SOCK_EV_RD) {
		while (1) {
			if (state == READ_START_LINE) {
				char *line = read_line(s);
				char *p;

				if (!line) {
					pico_socket_close(s);
					return;
				}

				if (!strncmp(line, "GET ", 4)) {
					pico_socket_close(s);
					return;
				}

				line += 4;

				while (*line == ' ')
					line++;

				p = strchr(line, ' ');
				if (!p) {
					pico_socket_close(s);
					return;
				}

				*p++ = 0;
				while (*p == ' ')
					p++;

				state = READ_HEADER;
			}

			if (state == READ_HEADER) {
				char *line = read_line(s);
				int len, fd;

				if (!line)
					return;

				if (!strcmp(line, "\r\n")) {
					struct stat st;

					lstat("/env/index.html", &st);

					len = sprintf(write_buf, "HTTP/1.1 200 OK\r\n"
						"Server: picotcp in barebox\r\n"
						"Content-Type: text/html\r\n"
						"Content-Length: %llu\r\n\r\n", st.st_size);

					fd = open("/env/index.html", O_RDONLY);
					len += read(fd, write_buf + len, sizeof(write_buf) - len);
					close(fd);
					write_buf_len = len;
					writed = 0;
					send_data(s);
					return;
				}
			}
		}
	}

	if (ev & PICO_SOCK_EV_CONN) {
		struct pico_ip4 orig = {};
		uint16_t port = 0;
        char peer[30] = "";
		int yes = 1;

		cli = pico_socket_accept(s, &orig, &port);
		pico_ipv4_to_string(peer, orig.addr);
        pico_socket_setoption(cli, PICO_TCP_NODELAY, &yes);
		printf("Connection established with %s:%d.\n", peer, port);
	}

	if (ev & PICO_SOCK_EV_ERR) {
		printf("Socket error received. Bailing out.\n");
		done = true;
		return;
	}

    if (ev & PICO_SOCK_EV_CLOSE) {
        printf("Socket received close from peer.\n");
		pico_socket_shutdown(s, PICO_SHUT_WR);
		printf("SOCKET> Called shutdown write, ev = %d\n", ev);
    }

	if ((ev & PICO_SOCK_EV_WR) && s == cli) {
		send_data(s);

		if (writed == write_buf_len)
			pico_socket_close(s);
	}
}

static int do_pico_httpd(int argc, char *argv[])
{
	struct pico_ip4 inaddr_any = {};
	struct pico_socket *s;
	uint16_t listen_port = 80;
	int ret;

	s = pico_socket_open(PICO_PROTO_IPV4, PICO_PROTO_TCP, &cb_http);
	if (!s) {
		printf("opening socket failed\n");
		return -1;
	}

	ret = 1;
	pico_socket_setoption(s, PICO_TCP_NODELAY, &ret);

	if (pico_socket_bind(s, &inaddr_any, &listen_port)) {
		printf("binding socket to port %d failed\n", listen_port);
		return 1;
	}

	if (pico_socket_listen(s, 1)) {
        printf("listening on port %d failed\n", listen_port);
		return -1;
    }

    printf("Launching PicoTCP http server\n");

	while (!done) {
		if (ctrlc()) {
			break;
		}
	}

	pico_socket_close(s);

	return 0;
}

BAREBOX_CMD_START(httpd)
	.cmd		= do_pico_httpd,
	BAREBOX_CMD_DESC("A simple HTTP Server")
	BAREBOX_CMD_GROUP(CMD_GRP_NET)
BAREBOX_CMD_END

