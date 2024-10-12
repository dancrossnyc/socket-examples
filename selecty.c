// Demonstration of a listener process that accepts incoming TCP
// connections and passes them to a worker process over Unix
// domain sockets; those workers will then multiplex themselves
// across the set of connections.
//
// Dan Cross <cross@gajendra.net>

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <fcntl.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// Sends a file descripter, `fd`, over a Unix domain socket `sd`
// via the POSIX access rights passing mechanism.
//
// Returns 1 on success, -1 on failure.
int
sendfd(int sd, int fd)
{
	struct msghdr mh;
	struct iovec iv;
	struct cmsghdr *cmsg;
	alignas(struct msghdr) unsigned char space[CMSG_SPACE(sizeof(int))];
	int ret;
	uint8_t ndesc;

	// We send a single byte containing the number of
	// descriptors we are sending.  This is not strictly
	// necessary, as the number is always 1, but some
	// implementations do not pass control data with an
	// otherwise empty data transfer, and this is a useful
	// value to check on the receiving side.
	ndesc = 1;
	memset(&iv, 0, sizeof(iv));
	iv.iov_base = &ndesc;
	iv.iov_len = 1;

	// Construct the message header.  Points to the iovec
	// and the space for the control message.
	memset(&mh, 0, sizeof(mh));
	mh.msg_iov = &iv;
	mh.msg_iovlen = 1;
	memset(space, 0, sizeof space);
	mh.msg_control = (void *)space;
	mh.msg_controllen = sizeof space;

	// Fill in the control message.
	cmsg = CMSG_FIRSTHDR(&mh);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));

	// Loop in case there's no room in the kernel buffer
	// to send.  Cf.Stevens et al.
	do {
		ret = sendmsg(sd, &mh, 0);
	} while (ret == 0);

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
	struct cmsghdr *cmsg;
	alignas(struct msghdr) unsigned char space[CMSG_SPACE(sizeof(int))];
	int ret;
	uint8_t ndesc;

	if (fdp == NULL)
		return -1;

	// Fill in an iovec to receive one byte of data.
	// Required on some systems that do not pass control
	// messages on empty data transfers.
	memset(&iv, 0, sizeof(iv));
	iv.iov_base = &ndesc;
	iv.iov_len = 1;

	// Fill in the msghdr structure.  `recvmsg(2)` will
	// update it.
	memset(&mh, 0, sizeof(mh));
	mh.msg_iov = &iv;
	mh.msg_iovlen = 1;
	memset(space, 0, sizeof space);
	mh.msg_control = (void *)space;
	mh.msg_controllen = sizeof space;

	ret = recvmsg(sd, &mh, 0);
	if (ret <= 0)
		return ret;
	if (ndesc != 1)
		return -1;
	cmsg = CMSG_FIRSTHDR(&mh);
	if (cmsg == NULL ||
	    cmsg->cmsg_len != CMSG_LEN(sizeof(int)) ||
	    cmsg->cmsg_level != SOL_SOCKET ||
	    cmsg->cmsg_type != SCM_RIGHTS ||
	    CMSG_NXTHDR(&mh, cmsg) != NULL)
	{
		return -1;
	}
	memcpy(fdp, CMSG_DATA(cmsg), sizeof(int));

	return 1;
}

void
dispatch(int sd, int sdworker)
{
	struct sockaddr_storage client;
	socklen_t clientlen;
	int nsd;

	memset(&client, 0, sizeof client);
	clientlen = sizeof client;
	nsd = accept(sd, (struct sockaddr *)&client, &clientlen);
	if (nsd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}
	if (sendfd(sdworker, nsd) < 0) {
		perror("sendfd");
		exit(EXIT_FAILURE);
	}
	close(nsd);
}

void
dispatcher(int sdworker, int port)
{
	int sd, sd6, maxsd;
	struct sockaddr_in sa;
	struct sockaddr_in6 sa6;
	fd_set sds;
	int one;

	sd = socket(PF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	sd6 = socket(PF_INET6, SOCK_STREAM, 0);
	if (sd6 < 0) {
		perror("socket6");
		exit(EXIT_FAILURE);
	}

	maxsd = sd;
	if (sd6 > maxsd)
		maxsd = sd6;

	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)port);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sd, (struct sockaddr *)&sa, sizeof sa) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	one = 1;
	(void)setsockopt(sd6, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof one);

	memset(&sa6, 0, sizeof sa6);
	sa6.sin6_family = AF_INET6;
	sa6.sin6_port = htons((unsigned short)port);
	sa6.sin6_addr = in6addr_any;
	if (bind(sd6, (struct sockaddr *)&sa6, sizeof sa6) < 0) {
		perror("bind6");
		exit(EXIT_FAILURE);
	}

	if (listen(sd, 255) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	if (listen(sd6, 255) < 0) {
		perror("listen6");
		exit(EXIT_FAILURE);
	}

	for (;;) {
		FD_ZERO(&sds);
		FD_SET(sd, &sds);
		FD_SET(sd6, &sds);

		if (select(maxsd + 1, &sds, NULL, NULL, NULL) < 0) {
			perror("select");
			exit(EXIT_FAILURE);
		}
		if (FD_ISSET(sd, &sds))
			dispatch(sd, sdworker);
		if (FD_ISSET(sd6, &sds))
			dispatch(sd6, sdworker);
	}
}

static bool
echo(int sd)
{
	unsigned char *p;
	unsigned char buf[1024];
	ssize_t nb, wb;

	nb = read(sd, buf, sizeof buf);
	if (nb < 0)
		perror("read");
	if (nb <= 0)
		return false;
	p = buf;
	while (nb > 0) {
		wb = write(sd, p, nb);
		if (wb < 0)
			perror("write");
		if (wb <= 0)
			return false;
		nb -= wb;
		p += wb;
	}

	return true;
}

void
worker(int sddispatcher)
{
	int sd, nsds, maxsd, flags, ret;
	fd_set allsds, rsds;

	flags = fcntl(sddispatcher, F_GETFL, 0);
	if (flags < 0) {
		perror("fcntl get flags");
		exit(EXIT_FAILURE);
	}
	if (fcntl(sddispatcher, F_SETFL, flags | O_NONBLOCK) < 0) {
		perror("fcntl set flags");
		exit(EXIT_FAILURE);
	}
	if (sddispatcher != 0) {
		if (dup2(sddispatcher, 0) < 0) {
			perror("dup2");
			exit(EXIT_FAILURE);
		}
		close(sddispatcher);
	}

	FD_ZERO(&allsds);
	FD_SET(0, &allsds);
	maxsd = 0;
	for (;;) {
		rsds = allsds;
		nsds = select(maxsd + 1, &rsds, NULL, NULL, NULL);
		if (nsds < 0) {
			perror("select");
			exit(EXIT_FAILURE);
		}
		for (sd = 1; sd <= maxsd; sd++) {
			if (FD_ISSET(sd, &rsds)) {
				if (!echo(sd)) {
					FD_CLR(sd, &allsds);
					if (sd == maxsd)
						--maxsd;
					close(sd);
				}
			}
		}
		if (FD_ISSET(0, &rsds)) {
			ret = recvfd(0, &sd);
			if (ret < 0) {
				if (errno == EWOULDBLOCK)
					continue;
				exit(EXIT_FAILURE);
			}
			if (ret == 0)
				break;
			printf("pid %d won the race for sd %d\n", getpid(), sd);
			if (sd > maxsd)
				maxsd = sd;
			FD_SET(sd, &allsds);
		}
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
