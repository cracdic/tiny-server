#include "csapp.h"

int main()
{
	int fd;
	char buf[MAXLINE];
	rio_t rio;

	fd = Open("./static/Symphony Of Destruction.txt", O_RDONLY, 0);
	Rio_readinitb(&rio, fd);
	while(Rio_readlineb(&rio, buf, MAXLINE) != 0)
		printf("%s", buf);
	Close(fd);
	return 0;
}
