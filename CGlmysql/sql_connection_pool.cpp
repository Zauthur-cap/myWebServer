#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool(){
    m_CurConn = 0;
    m_FreeConn = 0;
}
connection_pool *connection_pool::GetInstance(){
    static connection_pool connPool;
    return &connPool;
}


void connection_pool::init(string url,string User,string PassWord,string DBName,int Port,int MaxConn,int close_log){
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DBName;
    m_close_log = close_log;

    for(int i = 0;i < MaxConn;i++){
        MYSQL *con = NULL;
        con = mysql_init(con);

        if(con == NULL){
            LOG_ERROR("MYSQL ERROR");
            exit(1);
        }

        con = mysql_real_connect(con,url.c_str(),User.c_str(),PassWord.c_str(),DBName.c_str,Port,NULL,0);

        if(con == NULL){
            LOG_ERROR("MYSQL ERROR");
            exit(1);
        }

        connList.push_back(con);
        ++m_FreeConn;
    }
    reserve = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
}


//有连接请求时，从数据连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection(){
    MYSQL *con = NULL;

    if(connList.size() == 0) return NULL;
    //等待资源
    reserve.wait();
    m_lock.lock();  //加锁

    con = connList.front(); //取出第一个
    connList.pop_front();

    --m_FreeConn; // 可用连接-1
    ++m_CurConn;  //当前连接数+1

    m_lock.unlock();//解锁
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL* con){
    if(con == NULL) return false;
    m_lock.lock();
    connList.push_back(con);
    ++m_FreeConn;
    --m_CurConn;

    m_lock.unlock();
    reserve.post();
    return true;
}

void connection_pool::DestroyPool(){
    m_lock.lock();
    if(connList.size() > 0){
        list<MYSQL *>::iterator it;
        for(it = connList.begin();it != connList.end();it++){
            MYSQL *con = *it;
            mysql_close(con);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    m_lock.unlock();
}

//获取当前空闲的连接数
int  connection_pool::GetFreeConn(){
    return this->m_FreeConn;
}

connection_pool::~connection_pool(){
    DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL,connection_pool *connPool){
    *SQL = connPool->GetConnection();
    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);
}
