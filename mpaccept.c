// Demonstration of a multiple processes accepting
// connections on the same bound listening socket.
//
// Dan Cross <cross@gajendra.net>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int
main(void)
{
	int sd, nsd;
	struct sockaddr_in sa;
	struct sockaddr_in client;
	socklen_t clientlen;
	pid_t pid;

	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_port = htons((unsigned short)8200);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sd, (struct sockaddr *)&sa, sizeof sa) < 0) {
		perror("bind");
		close(sd);
		exit(EXIT_FAILURE);
	}
	if (listen(sd, 255) < 0) {
		perror("listen");
		close(sd);
		exit(EXIT_FAILURE);
	}

	for (int k = 0; k < 3; k++) {
		pid = fork();
		if (pid < 0) {
			perror("fork");
			exit(EXIT_FAILURE);
		}
		if (pid > 0)  // Parent
			continue;
		// Child.
		pid = getpid();
		for (;;) {
			memset(&client, 0, sizeof client);
			clientlen = sizeof client;
			nsd = accept(sd, (struct sockaddr *)&client, &clientlen);
			if (nsd < 0) {
				perror("accept");
				close(sd);
				exit(EXIT_FAILURE);
			}
			printf("pid %d accepted a connection\n", pid);
			close(nsd);
		}
	}
	close(sd);
	for (int k = 0; k < 3; k++)
		wait(NULL);

	return EXIT_SUCCESS;
}
