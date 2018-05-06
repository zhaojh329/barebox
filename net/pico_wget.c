#include <common.h>
#include <command.h>
#include <libfile.h>
#include <fs.h>
#include <net.h>
#include <pico_stack.h>
#include <pico_ipv4.h>
#include <pico_socket.h>

static char *http_msg;
static bool writed;
static char buf[1024];
static void *read_data;
static char *read_pos;
static int read_len;
static int dstfd;

static void cb_tcpclient(uint16_t ev, struct pico_socket *s)
{
    int r;

	if (ev & PICO_SOCK_EV_RD) {
        do {
            r = pico_socket_read(s, read_pos, 1024);
            if (r > 0) {
            	read_len += r;
            	read_pos += r;
            	printf("read %d\n", read_len);

            	if (read_len == 139659) {
					char *p = strstr(read_data, "\r\n\r\n");
			    	if (p) {
			    		printf("write %d\n", read_len - 4);
			    		p += 4;
			    		read_len -= 4;
			    		write(dstfd, p, read_len);
			    		close(dstfd);
			    	}
            	}
            }

            if (r < 0)
                return;
        } while(r > 0);
    }

    if (ev & PICO_SOCK_EV_CONN) {
        printf("Connection established with server.\n");
    }

    if (ev & PICO_SOCK_EV_ERR) {
        printf("Socket error received: %s. Bailing out.\n", strerror(pico_err));
        return;
    }

    if (ev & PICO_SOCK_EV_CLOSE) {
        printf("Socket received close from peer - Wrong case if not all client data sent!\n");
        pico_socket_close(s);
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
	char *p;

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

	if (pico_string_to_ipv4(buf + 7, &addr->addr))
		return -1;

	p = (char *)strchr(url + 7, '/');
	if (p)
		*path = p;

	return 0;
}
static int do_pico_wget(int argc, char *argv[])
{
	uint16_t port;
	struct pico_ip4 daddr;
	struct pico_socket *s;
	const char *path = "/";
	int ret;

	if (argc < 2) {
		printf("Usage: wget [URL]\n");
		return -1;
	}

	if (parse_url(argv[1], &daddr, &port, &path)) {
		printf("Invalid url\n");
		return -1;
	}

	printf("Connecting to: %pI4:%d\n", &daddr.addr, port);

	s = pico_socket_open(PICO_PROTO_IPV4, PICO_PROTO_TCP, &cb_tcpclient);
	if (!s) {
		printf("opening socket failed\n");
		return -1;
	}

	read_len = 0;
	writed = false;
	http_msg = calloc(1, 128);
	read_data = calloc(1, 1024 * 1024 * 4);
	read_pos = read_data;
	dstfd = open(path + 1, O_WRONLY | O_CREAT);

	if (port != 80)
		sprintf(buf, ":%d", port);
	sprintf(http_msg, "GET %s HTTP/1.1\r\nHost: %pI4%s\r\n\r\n", path, &daddr.addr, buf);

	ret = pico_socket_connect(s, &daddr, port);
	if (ret < 0) {
		printf("connecting to %s:%d failed\n", &daddr.addr, port);
		return -1;
	}

	return 0;
}

BAREBOX_CMD_START(wget)
	.cmd		= do_pico_wget,
	BAREBOX_CMD_DESC("download of files from HTTP")
	BAREBOX_CMD_GROUP(CMD_GRP_NET)
BAREBOX_CMD_END


