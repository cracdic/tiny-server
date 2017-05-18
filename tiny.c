/* tiny - A simple, iterative HTTP/1.0 Web server that uses the
 * GET method to serve static and dynamic content
 */
#include "csapp.h"

#define DEF_MODE S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH
#define DEF_UMASK S_IWGRP|S_IWOTH

volatile sig_atomic_t pid;


void handler(int sig); 
void doit(int fd);
void read_requesthdrs(rio_t *rp, char *hdr);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *hdr);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *hdr);
void clienterror(int fd, char *cause, char *errnum,
				 char *shortmsg, char *longmsg);

void handler(int sig)
{
	int olderrno = errno;

	while(waitpid(-1, NULL, 0) > 0) {
		Sio_puts("Handler reaped child\n");
	}
	if (errno != ECHILD)
		Sio_error("waitpid error");
	Sleep(1);
	errno = olderrno;
}

int main(int argc, char **argv)
{
	int listenfd, connfd;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;

	Signal(SIGCHLD, handler);

	/* Check command-line args */
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	listenfd = Open_listenfd(argv[1]);
	while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE,
					port, MAXLINE, 0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);
		doit(connfd);
		Close(connfd);
	}
}

void doit(int fd)
{ 
	int logfd, is_static;
	struct stat sbuf;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char filename[MAXLINE], cgiargs[MAXLINE];
	char requesthead[MAXLINE] = "Requested headers\r\n";
	rio_t rio;

	/* Read request line and headers */
	Rio_readinitb(&rio, fd);
	Rio_readlineb(&rio, buf, MAXLINE);
	printf("Request headers:\n");
	printf("%s", buf);
	sprintf(requesthead, "%s%s", requesthead, buf);
	sscanf(buf, "%s %s %s", method, uri, version);
	if (strcasecmp(method, "GET")) {
		clienterror(fd, method, "501", "Not implemented",
				 "Tiny does not implement this method");
		return;
	}
	read_requesthdrs(&rio, requesthead);
	umask(DEF_UMASK);
	logfd = Open("output.txt", O_CREAT|O_APPEND|O_WRONLY, DEF_MODE);
	Rio_writen(logfd, requesthead, strlen(requesthead));
	Close(logfd);
	
	/* Parse URI from GET request */
	is_static = parse_uri(uri, filename, cgiargs);
	if (stat(filename, &sbuf) < 0) {
		clienterror(fd, filename, "404", "Notfound",
					"Tiny couldn't find this file");
		return;
	}

	if (is_static) { /* Serve static content */
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
			clienterror(fd, filename, "403", "Forbidden",
						"Tiny couldn't find this file");
			return;
		}
		serve_static(fd, filename, sbuf.st_size, requesthead);
	}
	else { /* Serve dynamic content */
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
			clienterror(fd, filename, "403", "Forbidden",
						"Tiny couldn't run the CGI program");
			return;
		}
		serve_dynamic(fd, filename, cgiargs, requesthead);
	}
}

void clienterror(int fd, char *cause, char *errnum,
				 char *shortmsg, char *longmsg)
{
	char buf[MAXLINE], body[MAXBUF];

	/* Build the HTTP response body */
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	/* Print the HTTP response */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp, char *hdr)
{
	char buf[MAXLINE];

	Rio_readlineb(rp, buf, MAXLINE);
	sprintf(hdr, "%s%s", hdr, buf);
	//Rio_writen(rp->rio_fd, bufp, strlen(bufp));
	while(strcmp(buf, "\r\n")) {
		Rio_readlineb(rp, buf, MAXLINE);
		printf("%s", buf);
	   	sprintf(hdr, "%s%s", hdr, buf);
	}
	return;	
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
	char *ptr;

	if (!strstr(uri, "cgi-bin")) { /* Static content */
		strcpy(cgiargs, "");
		strcpy(filename, ".");
		strcat(filename, uri);
		if (uri[strlen(uri)-1] == '/')
			strcat(filename, "home.html");
		return 1;
	}
	else { /* Dynamic content */
		ptr = index(uri, '?');
		if (ptr) {
			strcpy(cgiargs, ptr+1);
			*ptr = '\0';
		}
		else
			strcpy(cgiargs, "");
		strcpy(filename, ".");
		strcat(filename, uri);
		return 0;
	}
}

void serve_static(int fd, char *filename, int filesize, char *hdr)
{
	int srcfd;
	char *srcp, filetype[MAXLINE], buf[MAXBUF];

	/* Send response headers to client */
	get_filetype(filename, filetype);
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
	sprintf(buf, "%sConnection: close\r\n", buf);
	sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
	sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
	Rio_writen(fd, buf, strlen(buf));
	printf("Response headers:\n");
	printf("%s", buf);

	/* Send request headers to client */
	if (!strcmp(filetype, "text/plain"))
		Rio_writen(fd, hdr, strlen(hdr));

	/* Send response body to client */
	srcfd = Open(filename, O_RDONLY, 0);
	srcp = malloc(filesize);
	Rio_readn(srcfd, srcp, filesize);
	//srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
	
	Close(srcfd);
	Rio_writen(fd, srcp, filesize);
	free(srcp);
	//Munmap(srcp, filesize);
}

/*
 * get_filetype - Derive file type from filename
 */
void get_filetype(char *filename, char *filetype)
{
	if (strstr(filename, ".html"))
		strcpy(filetype, "text/html");
	else if (strstr(filename, ".gif"))
		strcpy(filetype, "image/gif");
	else if (strstr(filename, ".png"))
		strcpy(filetype, "image/png");
	else if (strstr(filename, ".jpg"))
		strcpy(filetype, "image/jpeg");
	else if (strstr(filename, ".mp4"))
		strcpy(filetype, "video/mpeg");
	else
		strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *hdr)
{
	char buf[MAXLINE], *emptylist[] = { NULL };

	/* Return first part of HTTP response */
	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Server: Tiny Web Server\r\n");
	Rio_writen(fd, buf, strlen(buf));

	/* Return request headers */
	Rio_writen(fd, hdr, strlen(hdr));

	if (Fork() == 0) { /* Child */
		/* Real server would set all CGI vars here */
		setenv("QUERY_STRING", cgiargs, 1);
		Dup2(fd, STDOUT_FILENO);		 /* Redirect stdout to client */
		Execve(filename, emptylist, environ); /* Run CGI program */
	}
	//Wait(NULL); /* Parent waits for and reaps child */
}

