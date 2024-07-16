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
connection_pool *connection_pool::Getinstance(){
    static connection_pool connPool;
    return &connPool;
}
connection_pool::~connection_pool(){
    DestroyPool();
}