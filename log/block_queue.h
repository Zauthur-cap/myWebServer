#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

template <class T>
class block_queue{
    public:
    //创建队列并且分配大小
        block_queue(int max_size = 1000){
            if(max_size <= 0){
                exit(-1);
            }
            
            m_max_size = max_size;
            m_array = new T[max_size];
            m_size = 0;
            m_front = -1;
            m_back = -1;
        }
    //清除队列
        void clear(){
            m_mutex.lock();
            m_size = 0;
            m_front = -1;
            m_back = -1;
            m_mutex.unlock();
        }

        ~block_queue(){
            m_mutex.lock();
            if(m_array != NULL){
                delete [] m_array;
            }
            m_mutex.unlock();
        }

    //接下来判断队列是否已经满了
    bool full(){
        m_mutex.lock();
        if(m_size >= m_max_size){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    private:
        locker m_mutex;
        cond m_cond;

        T *m_array;
        int m_size;
        int m_max_size;
        int m_front;
        int m_back;
};

#endif