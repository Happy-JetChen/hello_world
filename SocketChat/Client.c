#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <pthread.h>
    
#define ADDRESS             "127.0.0.1"
#define PORT                8848
#define BUFFER_SIZE         1024

extern int connect_retry(int domain, int type, int protocol, const struct sockaddr * addr, socklen_t alen);

void *recv_message(void* fd)
{
    int cfd = *((int *)fd);
    int len;
    char message[BUFFER_SIZE];
    while(1)
   {
        if((len = recv(cfd, message, BUFFER_SIZE, 0)) < 0)
        {
            perror("recv error!\n");  
            exit(1);  
        }
        else
        {
            printf("Message from anther Client: %s",message);
            memset(message, 0, BUFFER_SIZE);
        }
    }
}
     
int main(int argc, char **argv)
{
    pthread_t thread_recv;
    //定义IPV4的TCP连接的套接字描述符
    int sock_cli;
     
    //定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(ADDRESS);
    servaddr.sin_port = htons(PORT);  //服务器端口
     
    //连接服务器，成功返回0，错误返回-1
    //创建套接字
    if((sock_cli = connect_retry(servaddr.sin_family, SOCK_STREAM, IPPROTO_TCP, (struct sockaddr*)&servaddr, sizeof(servaddr))) < 0)
    {
        perror("connect error");
        exit(1);
    }
    printf("connect server(IP:%s).\n", ADDRESS);  
     
    char sendbuf[BUFFER_SIZE];

    //客户端将控制台输入的信息发送给服务器端，服务器原样返回信息
    pthread_create(&thread_recv, NULL, recv_message, (void*)&sock_cli);

    while (fgets(sendbuf, sizeof(sendbuf), stdin) != NULL)
    {
        send(sock_cli, sendbuf, strlen(sendbuf),0); ///发送
        if(strcmp(sendbuf,"exit\n")==0)
        {
            printf("client exited.\n");
            break;
        }
        memset(sendbuf, 0, sizeof(sendbuf));
    }
     
    close(sock_cli);
    return 0;
}