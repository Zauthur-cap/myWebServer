#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include "../log/log.h"
#include <time.h>

class util_timer; //前向声明
struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

class util_timer
{   
    public:
        util_timer() : prev(NULL), next(NULL) {}
    public:
        time_t expire; //任务的超时时间，这里使用绝对时间
        void (* cb_func)(client_data *); //任务回调函数
        client_data *user_data; //回调函数处理的客户数据，由定时器的执行者传递给回调函数
        util_timer *prev; //指向前一个定时器
        util_timer *next; //指向后一个定时器
};

//定义上升链表类
class sort_timer_lst{
    public:
        sort_timer_lst();
        ~sort_timer_lst();
        void add_timer(util_timer *timer); //添加定时器
        void adjust_timer(util_timer *timer); //调整定时器
        void del_timer(util_timer *timer); //删除定时器
        void tick(); //处理任务

    private:
        void add_timer(util_timer *timer,util_timer *lst_head); //添加定时器
        util_timer *head; //头结点
        util_timer *tail; //尾结点
};

#endif