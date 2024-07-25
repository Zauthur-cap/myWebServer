#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool{ //用单例模式来进行编写
    public:
        MYSQL *GetConnection(); //获取连接
        bool ReleaseConnection(MYSQL *conn); //释放
        int GetFreeConn(); //获取连接
        void DestroyPool(); //销毁所有连接
        static connection_pool* GetInstance();
        void init(string url,string User,string PassWord,string DBName,int MaxConn,
        int Port,int close_log);

    private:
        connection_pool();
        ~connection_pool();
        int m_MaxConn;//最大连接数
        int m_FreeConn; // 可用连接数
        int m_CurConn; //当前已经连接的数量
        list<MYSQL *> connList;//连接池

        locker m_lock;
        sem reserve;
    public:
        string m_url;//主机地址
        string m_Port;//数据库的端口号
        string m_User;//数据库用户名
        string m_PassWord;//数据库密码
        string m_DatabaseName;//数据库名称
        int m_close_log; //用来记录日志功能是否打开
};


class connectionRAII{
    public:
        connectionRAII(MYSQL **con,connection_pool *connPool);
        ~connectionRAII();
    private:
        MYSQL *conRAII;
        connection_pool *poolRAII;
};
#endif