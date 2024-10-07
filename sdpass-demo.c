// Demonstration of a listener process that accepts
// incoming TCP connections and passes them to a
// worker process over Unix domain sockets.
//
// Dan Cross <cross@gajendra.net>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// Sends a file descripter, `fd`, over a Unix domain socket `sd`
// via the 4.4BSD access rights passing mechanism.
//
// Returns 1 on success, -1 on failure.
int
sendfd(int sd, int fd)
{
	struct msghdr mh;
	struct iovec iv;
	struct cmsghdr *ptr;
	size_t len;
	int ret;
	char dummy;

	// Construct the control message header.  Note this is
	// malloc'ed to ensure proper alignment.
	len = CMSG_SPACE(sizeof(int));
	ptr = malloc(len);
	if (ptr == NULL)
		return -1;
	memset(ptr, 0, len);
	ptr->cmsg_len = len;
	ptr->cmsg_level = SOL_SOCKET;
	ptr->cmsg_type = SCM_RIGHTS;
	memcpy(CMSG_DATA(ptr), &fd, sizeof(int));

	// We send a single byte of dummy data in case the
	// implementation does not pass control data with an
	// otherwise empty data transfer.
	dummy = 0;
	memset(&iv, 0, sizeof(iv));
	iv.iov_base = &dummy;
	iv.iov_len = 1;

	// Construct the message header.  Points to the dummy
	// data and the control message header.
	memset(&mh, 0, sizeof(mh));
	mh.msg_name = NULL;
	mh.msg_namelen = 0;
	mh.msg_iov = &iv;
	mh.msg_iovlen = 1;
	mh.msg_control = (caddr_t)ptr;
	mh.msg_controllen = len;
	mh.msg_flags = 0;

	// Loop in case there's no room in the kernel buffer
	// to send.  Cf.Stevens et al.
	do {
		ret = sendmsg(sd, &mh, 0);
	} while (ret == 0);
	free(ptr);

	return ret;
}

// Receives a file descriptor over the Unix domain socket `sd`
// and store it into `*fdp` on success.
//
// Returns 1 on success, 0 on EOF, -1 on error.
int
recvfd(int sd, int *fdp)
{
	struct msghdr mh;
	struct iovec iv;
	struct cmsghdr *ptr;
	size_t len;
	int ret;
	char dummy;

	if (fdp == NULL)
		return -1;

	// Allocate space for the control message.
	len = CMSG_SPACE(sizeof(int));
	ptr = malloc(len);
	if (ptr == NULL)
		return -1;

	// Fill in an iovec to receive one byte of dummy data.
	// Required on some systems that do not pass control
	// messages on empty data transfers.
	memset(&iv, 0, sizeof(iv));
	iv.iov_base = &dummy;
	iv.iov_len = 1;

	// Fill in the msghdr structure.  `recvmsg(2)` will
	// update it.
	memset(&mh, 0, sizeof(mh));
	mh.msg_name = NULL;
	mh.msg_namelen = 0;
	mh.msg_iov = &iv;
	mh.msg_iovlen = 1;
	mh.msg_control = ptr;
	mh.msg_controllen = len;
	mh.msg_flags = 0;

	ret = recvmsg(sd, &mh, 0);
	if (ret <= 0) {
		free(ptr);
		return ret;
	}
	if (mh.msg_flags != 0) {
		free(ptr);
		return -1;
	}
	memcpy(fdp, CMSG_DATA(ptr), sizeof(int));
	free(ptr);

	return 1;
}

void
dispatcher(int sdworker, int port)
{
	int sd, nsd;
	struct sockaddr_in sa;
	struct sockaddr_in client;
	socklen_t clientlen;

	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
		perror("socket");
		close(sdworker);
		exit(EXIT_FAILURE);
	}
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)port);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sd, (struct sockaddr *)&sa, sizeof sa) < 0) {
		perror("bind");
		close(sdworker);
		close(sd);
		exit(EXIT_FAILURE);
	}
	if (listen(sd, 255) < 0) {
		perror("listen");
		close(sdworker);
		close(sd);
		exit(EXIT_FAILURE);
	}

	for (;;) {
		memset(&client, 0, sizeof client);
		clientlen = sizeof client;
		nsd = accept(sd, (struct sockaddr *)&client, &clientlen);
		if (nsd < 0) {
			perror("accept");
			break;
		}
		if (sendfd(sdworker, nsd) < 0) {
			perror("sendfd");
			close(nsd);
			break;
		}
		close(nsd);
	}
	close(sdworker);
	close(sd);
	exit(EXIT_FAILURE);
}

static void
echo(int sd)
{
	char *p;
	char buf[1024];
	ssize_t nb, wb;

	for (;;) {
		nb = read(sd, buf, sizeof buf);
		if (nb < 0)
			perror("read");
		if (nb <= 0)
			return;
		p = buf;
		while (nb > 0) {
			wb = write(sd, p, nb);
			if (wb < 0)
				perror("write");
			if (wb <= 0)
				return;
			nb -= wb;
			p += wb;
		}
	}
}

void
worker(int sddispatcher)
{
	int sd;

	while (recvfd(sddispatcher, &sd) > 0) {
		echo(sd);
		close(sd);
	}
}

int
main(void)
{
	int sds[2];
	pid_t pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sds) < 0) {
		perror("socketpair");
		exit(EXIT_FAILURE);
	}

	for (int k = 0; k < 3; k++) {
		pid = fork();
		if (pid < 0) {
			perror("fork");
			exit(EXIT_FAILURE);
		}
		if (pid == 0) {	// Child
			close(sds[0]);
			worker(sds[1]);
			exit(EXIT_SUCCESS);
		}
	}
	close(sds[1]);
	dispatcher(sds[0], 8200);

	return EXIT_SUCCESS;
}
