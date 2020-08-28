#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 16777216 
#define MAX_OBJECT_SIZE 8388608 

#define EOF_TYPE 1
#define HOST_TYPE 2
#define OTHER_TYPE 3

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *user_agent_key= "User-Agent";
static const char *host_key = "Host";

void doit(int connfd);
int parse_uri(char *uri,char *hostname,char *path,int *port);
void build_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio);
char strType(char *buf);
int connect_Server(char *hostname,int port,char *http_header);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);


int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, SIG_IGN);
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);                                             //line:netp:tiny:doit
        Close(connfd);                                            //line:netp:tiny:close
    }
    return 0;
}
/* $end tinymain */

/*
 *  * doit - handle one HTTP request/response transaction
 *   */
/* $begin doit */
void doit(int fd) 
{
    int serverfd;
    char server_http_header[MAXLINE];
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    int port;
    rio_t rio, server_rio;

    /* Read request line and headers */
    rio_readinitb(&rio, fd); 
    if (!rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
        clienterror(fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }                                                    //line:netp:doit:endrequesterr

    /* parse the uri to get hostname, cgiargs and port */
    parse_uri(uri, filename, cgiargs, &port);
    /* build the http header */
    build_header(server_http_header, filename, cgiargs, port, &rio);
    /* connect to the server */
    serverfd = connect_Server(filename, port, server_http_header);
    
    if (serverfd<0){
        clienterror(fd, method, "404", "Not Found",
                    "Proxy couldn't find this file");
      return;
    }
  
    rio_readinitb(&server_rio, serverfd);
    rio_writen(serverfd,server_http_header,strlen(server_http_header));
    size_t s;
    
    while((s=rio_readlineb(&server_rio,buf,MAXLINE))!=0)
    {
        printf("proxy received %lu bytes,then send\n",s);
        rio_writen(fd,buf,s);
    }
    Close(serverfd);
}
/* $end doit */

/* build the http header */
void build_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio)
{
    char buf[MAXLINE],request_hdr[MAXLINE],other_hdr[MAXLINE],host_hdr[MAXLINE];
    char type;
    /*request line*/
    sprintf(request_hdr,requestlint_hdr_format,path);
    /*get other request header for client rio and change it */
    while(Rio_readlineb(client_rio,buf,MAXLINE)>0)
    {
        type = strType(buf);
        if (type == EOF_TYPE)
            break;

        if (type == HOST_TYPE)
        {
            strcpy(host_hdr,buf);
            continue;
        }

        if (type == OTHER_TYPE)
            strcat(other_hdr,buf);
    }
    if(strlen(host_hdr)==0)
    {
        sprintf(host_hdr,host_hdr_format,hostname);
    }
    sprintf(http_header,"%s%s%s%s%s%s%s",request_hdr,host_hdr,conn_hdr,prox_hdr,
	    user_agent_hdr,other_hdr,endof_hdr);
}

/* return the type of the string from the client */
char strType(char *buf)
{
    char type;
    if (strcmp(buf,endof_hdr)==0)
        type = EOF_TYPE;
    else if (!strncasecmp(buf,host_key,strlen(host_key)))
        type = HOST_TYPE;
    else if (!strncasecmp(buf,connection_key,strlen(connection_key))
                  &&!strncasecmp(buf,proxy_connection_key,strlen(proxy_connection_key))
                  &&!strncasecmp(buf,user_agent_key,strlen(user_agent_key)))
         {
             type = OTHER_TYPE;
         }
     return type;
} 

/*Connect to the server*/
inline int connect_Server(char *hostname,int port,char *http_header){
    char portStr[100];
    int rc;
    sprintf(portStr,"%d",port);
    rc = Open_clientfd(hostname,portStr);
    if (rc < 0) {
        fprintf(stderr, "Open_clientfd error: %s\n", strerror(errno));
        return 0;
    }
    return rc;
}

/* parse the uri to get the hostname, file path and port */
int parse_uri(char *uri,char *hostname,char *path,int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
	hostname[0] = '\0';
	return -1;
    }
       
    /* Extract the host name */
    if (strstr(uri,"http://"))
    	hostbegin = uri + 7;
    else
	hostbegin = uri + 8;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the port number */
    *port = 80; 
    if (*hostend == ':')   
	*port = atoi(hostend + 1);
    
    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
	path[0] = '\0';
    }
    else {
	pathbegin++;	
	strcpy(path, pathbegin);
    }

    char tmp[MAXLINE];
    sprintf(tmp, "/%s", path);
    strcpy(path, tmp);
    return 0;   
}


/*
 *  * clienterror - returns an error message to the client
 *   */
/* $begin clienterror */
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
    sprintf(buf, "%sContent-type: text/html\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}
/* $end clienterror */
