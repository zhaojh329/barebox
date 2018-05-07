#include <common.h>
#include <command.h>
#include <fs.h>
#include <net.h>
#include <progress.h>
#include <pico_stack.h>
#include <pico_ipv4.h>
#include <pico_socket.h>

enum {
	READ_STATE_LINE,
	READ_HEADER,
	READ_BODY,
	READ_ERROR
};

static bool done;
static int state;
static int dstfd;
static bool writed;
static char *http_msg;
static u32 read_len;
static u32 content_length;

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

static void cb_wget(uint16_t ev, struct pico_socket *s)
{
	if (ev & PICO_SOCK_EV_RD) {
		while (1) {
			if (state == READ_STATE_LINE) {
				char *line = read_line(s);

				if (!strstr(line, "200")) {
					printf("%s\n", line);
					state = READ_ERROR;
					done = true;
					return;
				}
				state = READ_HEADER;
			}

			if (state == READ_HEADER) {
				char *line = read_line(s);
				char *p;

				if (!line)
					return;

				if (!strcmp(line, "\r\n")) {
					state = READ_BODY;
					read_len = 0;
					init_progression_bar(content_length);
					continue;
				}

				p = strchr(line, ':');
				if (!p) {
					printf("Invalid header: %s\n", line);
					state = READ_ERROR;
					done = true;
					return;
				}

				*p++ = 0;

				if (!strcasecmp(line, "Content-Length")) {
					while (*p == ' ')
						p++;
					content_length = simple_strtoul(p, NULL, 0);
				}
				continue;
			}

			if (state == READ_BODY) {
				int r;
				char buf[4096];

				r = pico_socket_read(s, buf, sizeof(buf));
				if (r == 0) {
					done = true;
					return;
				}

				read_len += r;
				write(dstfd, buf, r);

				show_progress(read_len);

				if (read_len == content_length)
					done = true;
				return;
			}
		}
	}

	if (ev & PICO_SOCK_EV_ERR) {
		printf("Socket error received: %s. Bailing out.\n", strerror(pico_err));
		state = READ_ERROR;
		done = true;
		return;
	}

	if (ev & PICO_SOCK_EV_CLOSE) {
		printf("Socket received close from peer - Wrong case if not all client data sent!\n");
		done = true;
		return;
	}

	if (ev & PICO_SOCK_EV_WR) {
		if (!writed) {
			writed = true;
			pico_socket_write(s, http_msg, strlen(http_msg));
		}
	}
}

static int parse_url(const char *url, struct pico_ip4 *addr, uint16_t *port, const char **path)
{
	char buf[512] = "", *p;

	strcpy(buf, url);

	if (strncmp(buf, "http://", 7))
		return -1;

	*port = 80;

	p = strchr(buf + 7, ':');
	if (p) {
		*p++ = 0;
		*port = simple_strtoul(p, NULL, 0);
		if (*port == 0)
			return -1;
	}

	p = strchr(buf + 7, '/');
	if (p)
		*p = 0;

	p = (char *)strchr(url + 7, '/');
	if (p)
		*path = p;

	if (pico_string_to_ipv4(buf + 7, &addr->addr))
		return -1;

	return 0;
}
static int do_pico_wget(int argc, char *argv[])
{
	uint16_t port;
	struct pico_ip4 daddr;
	struct pico_socket *s;
	const char *path = "/";
	const char *file_name = "test";
	char buf[64] = "";
	int ret;

	if (argc < 2) {
		printf("Usage: wget [URL]\n");
		return -1;
	}

	if (parse_url(argv[1], &daddr, &port, &path)) {
		printf("Invalid url\n");
		return -1;
	}

	s = pico_socket_open(PICO_PROTO_IPV4, PICO_PROTO_TCP, &cb_wget);
	if (!s) {
		printf("opening socket failed\n");
		return -1;
	}

	http_msg = calloc(1, 128);

	if (path[1])
		file_name = path + 1;

	dstfd = open(file_name, O_WRONLY | O_CREAT);

	if (port != 80)
		sprintf(buf, ":%d", port);
	sprintf(http_msg, "GET %s HTTP/1.1\r\nHost: %pI4%s\r\n\r\n", path, &daddr.addr, buf);

	ret = pico_socket_connect(s, &daddr, port);
	if (ret < 0) {
		printf("connecting to %s:%d failed\n", &daddr.addr, port);
		return -1;
	}

	while (!done) {
		if (ctrlc()) {
			break;
		}
	}

	free(http_msg);
	close(dstfd);
	pico_socket_close(s);

	if (state != READ_ERROR)
		printf("\n'%s' saved\n", file_name);

	return 0;
}

BAREBOX_CMD_START(wget)
	.cmd		= do_pico_wget,
	BAREBOX_CMD_DESC("download of file from HTTP")
	BAREBOX_CMD_GROUP(CMD_GRP_NET)
BAREBOX_CMD_END

