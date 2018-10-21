#include "ThreadPool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ADDRESS             "127.0.0.1"
#define PORT                8848

#define MAX_EVENT_NUM       1000
#define MAXLEN              1024
#define MAX_CHAT_NUM        10

extern int initserver(int type, const struct sockaddr * addr, socklen_t alen, int qlen);

/* 从主线程向工作线程数据结构 */
struct Fd{
    int epollfd;
    int sockfd;
};

/* 用户说明 */
struct User{
    int sockfd;                     //文件描述符
    char client_buf[MAXLEN];        //数据的缓冲区
};


struct User user_client[MAX_CHAT_NUM];  //定义一个全局的客户数据表

/* 由于epoll设置的EPOLLONESHOT模式，当出现errno =EAGAIN,就需要重新设置文件描述符（可读） */
void reset_oneshot(int epollfd, int fd)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/* 向epoll内核事件表里面添加可写的事件 */
void addReadFd(int epollfd, int fd, int oneshot)
{
    struct epoll_event event;
    event.data.fd = fd;
    //event.events |= ~EPOLLIN;
    event.events |= EPOLLOUT;
    event.events |= EPOLLET;
    if(oneshot)
    {
        event.events |= EPOLLONESHOT;
    } 
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/* 群聊函数 */
void groupChat (int epollfd , int sockfd , char *buf)
{
        
    int i = 0 ;
    for ( i  = 0 ; i < MAX_CHAT_NUM ; i++)
    {
        if (user_client[i].sockfd == sockfd)
        {
            continue ;
        }
        strncpy (user_client[i].client_buf ,buf , strlen (buf)) ;
        addReadFd (epollfd , user_client[i].sockfd , 1);
    }
}

/* 接受数据的函数，也就是线程的回调函数 */
void funcation (void *args)
{
    int sockfd = ((struct Fd*)args)->sockfd ; 
    int epollfd =((struct Fd*)args)->epollfd;
    char buf[MAXLEN];
    memset (buf , 0, MAXLEN);
    
    printf ("start new thread to receive data on fd :%d\n", sockfd);
    
    //由于我将epoll的工作模式设置为ET模式，所以就要用一个循环来读取数据，防止数据没有读完，而丢失。
    while (1)
    {
        int ret = recv (sockfd ,buf , MAXLEN-1 , 0);
        if (ret == 0)
        {
            close (sockfd);
            break;
        }
        else if (ret < 0)
        {
            if (errno == EAGAIN)
            {
                reset_oneshot (epollfd, sockfd);  //重新设置（上面已经解释了）
                break;
            }
        }
        else
        {
            printf ("read data is %s\n", buf);
            usleep (100000);
            groupChat (epollfd , sockfd, buf );
        }
    }
    printf ("end thread receive  data on fd : %d\n", sockfd);
}

/* 这是重新注册，将文件描述符从可写变成可读 */
void changeFdtoRead (int epollfd , int fd)
{
       struct epoll_event event;
       event.data.fd = fd ;
       event.events  |= ~EPOLLOUT ;
       event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
       epoll_ctl (epollfd , EPOLL_CTL_MOD , fd , &event);
}

/* 与前面的解释一样 */
int reset_read_oneshot (int epollfd , int sockfd)
{
    struct epoll_event event;
    event.data.fd = sockfd ;
    event.events = EPOLLOUT |EPOLLET | EPOLLONESHOT ;
    epoll_ctl (epollfd, EPOLL_CTL_MOD , sockfd , &event);
    return 0 ;
}

/* 发送读的数据 */
int readfun (void *args)
{
       int sockfd = ((struct Fd *)args)->sockfd ;
       int epollfd= ((struct Fd *)args)->epollfd ;
       int ret, i;
       for (i = 0 ; i < MAX_CHAT_NUM ; i++)
       {
           if(user_client[i].sockfd == sockfd)
           {
                ret = send (sockfd, user_client[i].client_buf , strlen (user_client[i].client_buf), 0);    //发送数据
                break;
           }
       }
        
       if (ret == 0 )
       {
           
           close (sockfd);
           printf ("发送数据失败\n");
           return -1 ;
       }
       else if (ret == EAGAIN)
       {
           reset_read_oneshot (epollfd , sockfd);
           printf("send later\n");
           return -1;
       }
       memset (&user_client[i].client_buf , '\0', sizeof (user_client[i].client_buf));
       changeFdtoRead (epollfd , sockfd);       //重新设置文件描述符
       return 0;
}

/* 套接字设置为非阻塞 */
int setNonblocking (int fd)
{
    int old_option = fcntl (fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl (fd , F_SETFL , new_option);
    return old_option ;
}

/* 添加新fd到epollfd中 */
int addFd (int epollfd , int fd , int oneshot)
{
    struct epoll_event  event;
    event.data.fd = fd ;
    event.events = EPOLLIN | EPOLLET ;
    if (oneshot)
    {
        event.events |= EPOLLONESHOT ;
    }
    epoll_ctl (epollfd , EPOLL_CTL_ADD ,fd , &event);
    setNonblocking (fd);
    return 0 ;
}

int main(int argc, char const *argv[])
{
    struct sockaddr_in address ;

    memset (&address, 0, sizeof (address));
    address.sin_family = AF_INET ;
    inet_pton (AF_INET ,ADDRESS , &address.sin_addr);
    address.sin_port =htons(PORT) ;

    //创建套接字
    int listenfd = initserver(SOCK_STREAM, (struct sockaddr *)&address, sizeof(address), 10);  

    struct epoll_event events[MAX_EVENT_NUM];
        
    int epollfd = epoll_create (5);                 //创建内核事件描述符表
    assert (epollfd != -1);
    addFd (epollfd , listenfd, 0);
        
    threadpool_t *thpool ;                          //线程池
    thpool = threadpool_create (5, 10, 5) ;        //线程池的一个初始化

    int index = -1;

    while (1)
    {
        int ret = epoll_wait (epollfd, events, MAX_EVENT_NUM , -1);//等待就绪的文件描述符，这个函数会将就绪的复制到events的结构体数组中。
        if (ret < 0)
        {
            printf ("epoll failure\n");
            break; 
        }
        int i = 0;
        for ( i = 0 ; i < ret ; i++ )
        {
            int sockfd = events[i].data.fd ;

            if (sockfd == listenfd && index < MAX_CHAT_NUM)
            {
                struct sockaddr_in client_address ;
                socklen_t  client_length = sizeof (client_address);
                int connfd = accept (listenfd , (struct sockaddr*)&client_address,&client_length);
                user_client[++index].sockfd = connfd ;
                memset (&user_client[index].client_buf , 0, sizeof (user_client[index].client_buf));
                addFd (epollfd , connfd , 1);               //将新的套接字加入到内核事件表里面。
                printf("Add a new user!\n");
            }
            else if (events[i].events & EPOLLIN) 
            {
                struct Fd fds_for_new_worker ;
                fds_for_new_worker.epollfd = epollfd ; 
                fds_for_new_worker.sockfd = sockfd ;
                
                threadpool_add_task (thpool, (void*)funcation, &fds_for_new_worker);//将任务添加到工作队列中
            }else if (events[i].events & EPOLLOUT)
            {
                
                struct Fd fds_for_new_worker ;
                fds_for_new_worker.epollfd = epollfd ;
                fds_for_new_worker.sockfd = sockfd ;
                threadpool_add_task (thpool, (void*)readfun , &fds_for_new_worker );//将任务添加到工作队列中
            }
                        
        }

    }
                            
    if(threadpool_destroy(thpool) == 0)
        printf("threadpool destroy sucess!\n");
    else
    {
        printf("threadpool destroy error!\n");
        return EXIT_FAILURE;
    }

    close (listenfd);
        return EXIT_SUCCESS;
    
    return 0;
}
