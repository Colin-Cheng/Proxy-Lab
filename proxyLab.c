#include <stdio.h>
#include "csapp.h"
#include <ctype.h>

//the const variable below are requested in the reference
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connect_hdr = "Connection: close\r\n";
static const char *proxy_hdr = "Proxy-Connection: close\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

//the micro below show some string type and cache size
#define EOF_TYPE 1
#define HOST_TYPE 2
#define OTHER_TYPE 3
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_NUM 10

int getType(char *buf);
void doit(int connfd);
void parseUri(char uri[],char hostname[],char path[],char port[]);
void build_http
(char *server_http,char *hostname,char*path,char* port,rio_t *clientrio);
void* thread(void *vargp);
void initCache();
void PreRead(int index);
void afterRead(int index);
void preWrite(int index);
void afterWrite(int index);
int findCache(char *url);
void updateLRU(int index);
int findSuitCache();
void writeCacheContent(char *url,char* buf);

//the struct of a cacheLine for a file
typedef struct 
{
    char content[MAX_OBJECT_SIZE];
    char url[MAXLINE];
    int time;
    int isEmpty;
    int readCount;
    int writeCount;
    sem_t mutex;
    sem_t w;
}cacheunit;

//the cache struct consist of ten cacheLine
typedef struct 
{
    cacheunit cacheUnit[CACHE_NUM];
}Cache;

Cache cache;
//record the overall time to update the cacheline time
int allTime=0;
//the flag is used to void compete of "allTime"
sem_t flag;
/*
the main function to connect the client , and it is same to 
the code in the page 672 of textbook
*/

int main(int argc,char **argv)
{
    int listenfd,*connfd;
    char hostname[MAXLINE],port[MAXLINE];
    socklen_t clientlen;
    struct  sockaddr_storage clientaddr;
    pthread_t tid;
    Signal(SIGPIPE, SIG_IGN);
    Sem_init(&flag,0,1);
    // the cmd parameters fail
    if (argc!=2){
        fprintf(stderr, "usage: %s <port>\n",argv[0]);
        exit(1);
    }
    initCache();
    listenfd= open_listenfd(argv[1]);
    while (1)
    {
        clientlen=sizeof(clientaddr);
        connfd=malloc(sizeof(int));
        *connfd=Accept(listenfd,(SA*)&clientaddr,&clientlen);
        Getnameinfo((SA*)&clientaddr,clientlen,hostname,MAXLINE,
                    port,MAXLINE,0);
        printf("Accepted connection from (%s , %s)\n",hostname,port);
        Pthread_create(&tid,NULL,thread,(void*)connfd);
    }
}

//create a new thread to run for servering the client
void *thread(void *vargo)
{
    int connfd = *((int *)vargo);
    Pthread_detach(pthread_self());
    free(vargo);
    doit(connfd);
    Close(connfd);
    return NULL;
}

//the main process to get the cmd,parse the cmd,find the cache,
//build the header,connect to the server and return the content 
//to the client
void doit(int connfd)
{
    int serverFd;
    rio_t clientrio,serverrio;
    char server_http[MAXLINE],buf[MAXLINE],method[MAXLINE],path[MAXLINE];
    char port[MAXLINE],uri[MAXLINE],version[MAXLINE],hostname[MAXLINE];
    
    P(&flag);
        allTime+=1;
    V(&flag);

    rio_readinitb(&clientrio,connfd);
    rio_readlineb(&clientrio,buf,MAXLINE);
    sscanf(buf,"%s %s %s",method,uri,version);

    if (strcasecmp(method,"GET")){
        printf("Proxy does not implement this method");
        return;
    }

    int cacheIndex;
    if ((cacheIndex=findCache(uri))>0)
    {
        PreRead(cacheIndex);
        rio_writen(connfd,cache.cacheUnit[cacheIndex].content,
            strlen(cache.cacheUnit[cacheIndex].content));
        printf("the proxy has received %lu bytes\n",
            strlen(cache.cacheUnit[cacheIndex].content));
        afterRead(cacheIndex);
        updateLRU(cacheIndex);
    }

    parseUri(uri,hostname,path,port);
    build_http(server_http,hostname,path,port,&clientrio);
    serverFd=open_clientfd(hostname,port);
    if (serverFd<0){
        printf("connection failed\n");
        return;
    }
    rio_readinitb(&serverrio,serverFd);
    rio_writen(serverFd,server_http,strlen(server_http));

    size_t len;
    size_t allCount=0;
    char cacheBuf[MAX_OBJECT_SIZE];
    while ((len=rio_readlineb(&serverrio,buf,MAXLINE))!=0)
    {
        allCount+=len;
        if (allCount<MAX_OBJECT_SIZE)
            strcat(cacheBuf,buf);
        rio_writen(connfd,buf,len);
    }
    printf("the proxy has received %lu bytes\n",allCount);
    Close(serverFd);
    if (allCount<MAX_OBJECT_SIZE)
        writeCacheContent(uri,cacheBuf);
}

//parse the uri to get the hostname ,path ,and the port(if it exits)
void parseUri(char uri[],char hostname[],char path[],char port[])
{
    //get the hostname from uri
    int i=0;
    char* hostnamePos=strstr(uri,"//");
    if (hostnamePos!=NULL)
        hostnamePos+=2;
    else
        hostnamePos=uri;
    strcpy(hostname,hostnamePos);
    int len=strlen(hostname);
    for (i=0;i<len;++i){
        if (hostname[i]=='/'||hostname[i]==':'){
            hostname[i]='\0';
            break;
        }
    }
    
    //get the port from uri
    char* portPos=strchr(hostnamePos,':');
    if (portPos!=NULL){
        strcpy(port,portPos+1);
        len=strlen(port);
        for (i=0;i<len;++i){
            if (!isdigit(port[i])){
                port[i]='\0';
                break;
            }
        }
    }
    else{
        port[0]='8';port[1]='0';port[2]='\0';
    }

    //get the path from uri
    char *pathPos=strstr(uri,"//");
    if (pathPos!=NULL)
        pathPos+=2;
    else
        pathPos=uri;
    pathPos=strchr(pathPos,'/');
    strcpy(path,pathPos);
    char *endpos = strchr(path,':');
    if (endpos!=NULL){
        (*endpos)='\0';
    }
    printf("hostname:%s\npath:%s\nport:%s\n",hostname,path,port);
    return;
}

//build the http header that will be sent to the server
void build_http
(char *server_http,char *hostname,char*path,char* port,rio_t *clientrio)
{
    char requestLine[MAXLINE],buf[MAXLINE];
    char host_hdr[MAXLINE],other_hdr[MAXLINE];
    //build the main string to get the service
    sprintf(requestLine,"GET %s HTTP/1.0\r\n",path);
    while (rio_readlineb(clientrio,buf,MAXLINE)>0)
    {
        int type = getType(buf);
        if (type==EOF_TYPE)
            break;
        else if (type==HOST_TYPE){
            strcpy(host_hdr,buf);
        }
        else{
            strcat(other_hdr,buf);
        }
    }
    if (strlen(host_hdr)==0)
        sprintf(host_hdr,"Host: %s\r\n",hostname);
    sprintf(server_http,"%s%s%s%s%s%s\r\n",
            requestLine,
            host_hdr,
            connect_hdr,
            proxy_hdr,
            user_agent_hdr,
            other_hdr);
    return;
}

//tell the type of the string from the client
int getType(char *buf)
{
    if(strcmp(buf,"\r\n")==0)
        return EOF_TYPE;
    else if (!strncasecmp(buf,host_key,strlen(host_key)))
        return HOST_TYPE;
    else if (strncasecmp(buf,connection_key,strlen(connection_key))
             &&strncasecmp(buf,proxy_connection_key,strlen(proxy_connection_key))
             &&strncasecmp(buf,user_agent_key,strlen(user_agent_key)))
        return OTHER_TYPE;
    return 0;
}

//initilize the variable of cache
void initCache()
{
    int i = 0;
    for (i = 0; i < 10; ++i)
    {
        cache.cacheUnit[i].isEmpty=1;
        cache.cacheUnit[i].time=0;
        Sem_init(&cache.cacheUnit[i].mutex,0,1);
        Sem_init(&cache.cacheUnit[i].w,0,1);
        cache.cacheUnit[i].readCount=0;
        cache.cacheUnit[i].writeCount=0;
    }
}

// the four functions below are to lock and unlock the process
// to avoid compete
void PreRead(int index)
{
    P(&cache.cacheUnit[index].mutex);
    cache.cacheUnit[index].readCount++;
    if (cache.cacheUnit[index].readCount==1)
        P(&cache.cacheUnit[index].w);
    V(&cache.cacheUnit[index].mutex);
}

void afterRead(int index)
{
    P(&cache.cacheUnit[index].mutex);
    cache.cacheUnit[index].readCount--;
    if (cache.cacheUnit[index].readCount==0)
        V(&cache.cacheUnit[index].w);
    V(&cache.cacheUnit[index].mutex);
}

void preWrite(int index)
{
    P(&cache.cacheUnit[index].w);
}

void afterWrite(int index)
{
    V(&cache.cacheUnit[index].w);
}

int findCache(char *url)
{
    int i,result=-1;
    for (i=0;i<CACHE_NUM;++i){
        PreRead(i);
        if ((cache.cacheUnit[i].isEmpty==0)&&
            strcmp(cache.cacheUnit[i].url,url)==0)
            result=i;
        afterRead(i);
    }
    return result;
}

//update the read/write cache's visit time 
void updateLRU(int index)
{
    preWrite(index);
    cache.cacheUnit[index].time=allTime;
    afterWrite(index);
}

//find a emtpy or LRU cache for the server content
int findSuitCache()
{
    int i,result=-1,minTime=0x7fffffff;
    for (i=0;i<CACHE_NUM;++i){
        PreRead(i);
        if (cache.cacheUnit[i].isEmpty){
            result=i;
        }
        afterRead(i);
        if (result==i)
            break;
    }
    if (result!=-1)
        return result;
    for (i=0;i<CACHE_NUM;++i){
        PreRead(i);
        if (cache.cacheUnit[i].time<minTime){
            minTime=cache.cacheUnit[i].time;
            result=i;
        }
        afterRead(i);
    }
    return result;
}

//write the server content to the cache
void writeCacheContent(char *url,char* buf)
{
    int index = findSuitCache();
    preWrite(index);
    strcpy(cache.cacheUnit[index].content,buf);
    strcpy(cache.cacheUnit[index].url,url);
    cache.cacheUnit[index].isEmpty=0;
    afterWrite(index);
    updateLRU(index);
}

