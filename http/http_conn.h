#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
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
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../log/log.h" 
#include "../timer/lst_timer.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200; //文件名的最大长度,单位是字节
    static const int READ_BUFFER_SIZE = 2048; //读缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 1024; //写缓冲区的大小
    enum METHOD //HTTP请求方法，enum的作用是定义一个枚举类型
    {
        GET = 0, //从服务器获取数据
        POST,    //向服务器提交数据
        HEAD,    //获取报文首部
        PUT,     //向服务器传送文件
        DELETE,  //请求服务器删除Request-URI所标识的资源
        TRACE,   //回显服务器收到的请求，主要用于测试或诊断
        OPTIONS, //列出可对Request-URI指定的资源的请求方法
        CONNECT, //用于代理服务器
        PATH     //路径
    };
    enum CHECK_STATE //主状态机的状态
    {
        CHECK_STATE_REQUESTLINE = 0, //解析请求行
        CHECK_STATE_HEADER,         //解析请求头
        CHECK_STATE_CONTENT         //解析消息体
    };
    enum HTTP_CODE //服务器处理HTTP请求的可能结果
    {
        NO_REQUEST,         //请求不完整，需要继续读取请求报文数据
        GET_REQUEST,        //获得了一个完整的HTTP请求
        BAD_REQUEST,        //HTTP请求报文语法错误
        NO_RESOURCE,        //服务器没有请求的资源
        FORBIDDEN_REQUEST,  //客户对资源没有足够的访问权限
        FILE_REQUEST,       //文件请求
        INTERNAL_ERROR,     //服务器内部错误
        CLOSED_CONNECTION   //客户端已经关闭连接
    };
    enum LINE_STATUS //从状态机的状态
    {
        LINE_OK = 0,  //读取到一个完整的行
        LINE_BAD,     //行出错
        LINE_OPEN     //行数据尚且不完整
    };
public:
    http_conn() {}//构造函数
    ~http_conn() {}//析构函数
public:
    void init(int sockfd, const sockaddr_in &addr);//初始化新接受的连接,参数为文件描述符和指向地址结构的指针
    void close_conn(bool real_close = true);//关闭连接
    void process();//处理客户请求
    bool read();//非阻塞读操作
    bool write();//非阻塞写操作

    sockaddr_in *get_address()//获取客户端socket地址
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);//初始化数据库读取表
    int timer_flag;//定时器标志,作用是判断定时器是否超时
    int improv;//改进标志，作用是判断是否改进

private:
    void init();//初始化连接
    HTTP_CODE process_read();//解析HTTP请求
    bool process_write(HTTP_CODE ret);//填充HTTP应答
    HTTP_CODE parse_request_line(char *text);//解析请求行
    HTTP_CODE parse_headers(char *text);//解析请求头
    HTTP_CODE parse_content(char *text);//解析消息体
    HTTP_CODE do_request();//处理请求
    char *get_line()//读取一行数据
    {
        return m_read_buf + m_start_line;
    }
    LINE_STATUS parse_line();//解析一行数据
    void unmap();//释放资源
    bool add_response(const char *format, ...);//添加响应报文
    bool add_content(const char *content);//添加响应内容
    bool add_status_line(int status, const char *title);//添加状态行
    bool add_headers(int content_length);//添加响应头
    bool add_content_length(int content_length);//添加响应内容长度
    bool add_linger();//添加连接状态
    bool add_blank_line();//添加空行

public:
    static int m_epollfd;//所有socket上的事件都被注册到同一个epoll内核事件表中，所以将epoll文件描述符设置为静态的
    static int m_user_count;//统计用户数量
    MYSQL *mysql;//数据库连接
    int m_state;//读为0，写为1

private:
    int m_sockfd;//该HTTP连接的socket和对方的socket地址
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];//读缓冲区
    int m_read_idx;//标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
    int m_checked_idx;//当前正在分析的字符在读缓冲区中的位置
    int m_start_line;//当前正在解析的行的起始位置
    char m_write_buf[WRITE_BUFFER_SIZE];//写缓冲区
    int m_write_idx;//写缓冲区中待发送的字节数
    CHECK_STATE m_check_state;//主状态机当前所处的状态
    METHOD m_method;//请求方法
    char m_real_file[FILENAME_LEN];//客户请求的目标文件的完整路径，其内容等于doc_root+m_url,doc_root是网站根目录
    char *m_url;//客户请求的目标文件的文件名
    char *m_version;//HTTP协议版本号，我们仅支持HTTP/1.1
    char *m_host;//主机名
    int m_content_length;//HTTP请求的消息体的长度
    bool m_linger;//HTTP请求是否要求保持连接
    char *m_file_address;//客户请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat;//目标文件的状态
    struct iovec m_iv[2];//io向量机制iovec
    int m_iv_count;//io向量的计数
    int cgi;//是否启用的POST
    char *m_string;//存储请求头数据
    int bytes_to_send;//剩余发送字节数
    int bytes_have_send;//已发送字节数
    char *doc_root;//网站根目录
    map<string, string> m_users;//用户名和密码
    int m_TRIGMode;//触发模式
    int m_close_log;//关闭日志
    
    char sql_user[100];//数据库用户名
    char sql_passwd[100];//数据库密码
    char sql_name[100];//数据库名
};
#endif