﻿#ifndef LOCKER_H
#define LOCKER_H

#include<exception> // 进行异常处理的库
#include<pthread.h>  // 多线程
#include<semaphore.h> //信号量的库文件

//下面来定义posix信号量
class sem{
    public:
        sem(){
            if(sem_init(&m_sem,0,0) != 0){
                throw std::exception();
            }
        }
        sem(int num){       //给定信号量的初始化
            if(sem_init(&m_sem,0,num) != 0){
                throw std::exception();
            }
        }
        ~sem(){
            sem_destroy(&m_sem);
        }
        bool wait(){ //信号量减1
            return sem_wait(&m_sem) == 0;
        }
        bool post(){ //信号量加1
            return sem_post(&m_sem) == 0;
        }
    private:
        sem_t m_sem;
};

//定义互斥锁
class locker{
    public:
        locker(){ 
            if(pthread_mutex_init(&m_mutex,NULL) != 0){
                throw std::exception();
            }
        }
        ~locker(){
            pthread_mutex_destroy(&m_mutex);
        }
        bool lock(){
            return pthread_mutex_lock(&m_mutex) == 0;
        }
        bool unlock(){
            return pthread_mutex_unlock(&m_mutex) == 0;
        }
        pthread_mutex_t *get(){
            return &m_mutex;
        }
    private:
        pthread_mutex_t m_mutex;
};

//定义条件变量
class cond{
    public:
        cond(){
            if(pthread_cond_init(&m_cond,NULL) != 0){
                throw std::exception();
            }
        }
        ~cond(){
            pthread_cond_destroy(&m_cond);
        }
        bool wait(pthread_mutex_t *m_mutex,struct timespec t){
            int ret = 0;
            ret = pthread_cond_timedwait(&m_cond,m_mutex,&t);
            return ret == 0;
        }
        bool signal(){
            return pthread_cond_signal(&m_cond) == 0;
        }
        bool broadcast(){
            return pthread_cond_broadcast(&m_cond) == 0;
        }
    private:
        pthread_cond_t m_cond;  
};

#endif