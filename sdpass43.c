/*
 * Example code to demonstrate file descriptor passing
 * over Berkeley sockets.
 *
 * Dan Cross <cross@gajendra.net>
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	MAXBUF	1024
#define	SOCKET	"sock.example"

int unix_server(char *);
int unix_client(char *);

int
unix_server(char *path)
{
	int nsd, sd;
	socklen_t clen;
	struct sockaddr_un sock;

	sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	memset(&sock, 0, sizeof(sock));
	sock.sun_family = AF_UNIX;
	strncpy(sock.sun_path, path, sizeof(sock.sun_path));
	if (bind(sd, (struct sockaddr *)&sock, sizeof(sock)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}
	if (listen(sd, 5) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}
	clen = sizeof(sock);
	nsd = accept(sd, (struct sockaddr *)&sock, &clen);
	if (nsd < 0) {
		perror("accept");
		exit(EXIT_FAILURE);
	}
	close(sd);

	return(nsd);
}

int
unix_client(char *path)
{
	int sd;
	struct sockaddr_un sock;

	sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	memset(&sock, 0, sizeof(sock));
	sock.sun_family = AF_UNIX;
	strncpy(sock.sun_path, path, sizeof(sock.sun_path));
	if (connect(sd, (struct sockaddr *)&sock, sizeof(sock)) < 0) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	return(sd);
}

/* Send a file descriptor; returns 1 on success, -1 on error */
int
sendfd(int sd, int fd)
{
	struct msghdr mh;
	struct iovec iv;
	char dummy;

	memset(&iv, 0, sizeof(iv));
	iv.iov_base = &dummy;
	iv.iov_len = sizeof(char);

	memset(&mh, 0, sizeof(mh));
	mh.msg_name = NULL;
	mh.msg_namelen = 0;
	mh.msg_iov = &iv;
	mh.msg_iovlen = 1;

	/* Now we copy in the file descriptor. */
	mh.msg_accrights = (caddr_t)&fd;
	mh.msg_accrightslen = sizeof(int);

	return(sendmsg(sd, &mh, 0));
}

/* Returns 1 on success, 0 on EOF, -1 on error. */
int
recvfd(int sd, int *fdp)
{
	struct msghdr mh;
	struct iovec iv;
	int ret;
	char dummy;

	if (fdp == NULL)
		return(-1);

	memset(&iv, 0, sizeof(iv));
	iv.iov_base = &dummy;
	iv.iov_len = 1;

	memset(&mh, 0, sizeof(mh));
	mh.msg_name = NULL;
	mh.msg_namelen = 0;
	mh.msg_iov = &iv;
	mh.msg_iovlen = 1;
	mh.msg_accrights = (caddr_t)fdp;
	mh.msg_accrightslen = sizeof(int);

	if ((ret = recvmsg(sd, &mh, 0)) < 0)
		return(ret);

	if (mh.msg_accrightslen != sizeof(int))
		return(-1);

	return(1);
}

int
main(void)
{
	pid_t pid;
	int fd, sd, nb;
	char buf[MAXBUF];

	pid = fork();
	if (pid < 0) {
		perror("can't fork");
		exit(EXIT_FAILURE);
	} else if (pid == 0) {		/*  Child process.  */
		sd = unix_server(SOCKET);
		if (recv(sd, buf, MAXBUF, 0) <= 0) {
			perror("recv");
			exit(EXIT_FAILURE);
		}
		printf("buf == %s.\n", buf);
		if (recvfd(sd, &fd) <= 0) {
			perror("recvfd");
			exit(EXIT_FAILURE);
		}
		printf("C: fd == %d.\n", fd);
		while ((nb = read(fd, buf, MAXBUF)) > 0) {
			write(STDOUT_FILENO, buf, nb);
		}
		if (nb < 0) {
			perror("read");
			exit(EXIT_FAILURE);
		}
		close(fd);
		unlink(SOCKET);
	} else {			/*  Parent.  */
		sleep(1);
		sd = unix_client(SOCKET);
		memcpy(buf, "Hi there!", 10);
		if (send(sd, buf, MAXBUF, 0) < 0) {
			perror("send");
			exit(EXIT_FAILURE);
		}
		if ((fd = open("/etc/motd", O_RDONLY, 0400)) < 0) {
			perror("open(\"/etc/motd\", O_RDONLY)");
			exit(EXIT_FAILURE);
		}
		printf("P: fd == %d.\n", fd);
		if (sendfd(sd, fd) < 0) {
			perror("sendfd");
			exit(EXIT_FAILURE);
		}
		wait(NULL);
		close(fd);
	}

	return(EXIT_SUCCESS);
}
