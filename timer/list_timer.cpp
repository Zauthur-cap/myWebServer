#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}



/**
 * @brief 析构函数，用于销毁链表中的所有计时器对象。
 * 
 * 该函数遍历链表，逐个删除链表中的计时器对象。这是必要的，因为如果不在析构函数中手动删除这些对象，
 * 它们将不会被自动销毁，从而导致内存泄漏。通过遍历链表并删除每个节点，确保了链表中的所有资源都能被正确释放。
 */
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head; // 初始化一个指针tmp，用于遍历链表。
    while(tmp) // 当tmp指向的节点不为空时，继续遍历。
    {
        head = tmp->next; // 将head指针移动到下一个节点，准备删除当前节点。
        delete tmp; // 删除当前节点的计时器对象。
        tmp = head; // 更新tmp指针为新的head，继续遍历下一个节点。
    }
}

void sort_timer_lst::add_timer(util_timer *timer){
    if(!timer){
        return;
    }
    if(!head){
        head = tail = timer;
        return;
    }
    if(timer->expire < head->expire){
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer,head);
}

//调整定时器，任务发生变化时，调整定时器在链表中的位置
void sort_timer_lst::adjust_timer(util_timer *timer){
    if(!timer){
        return;
    }
    util_timer *tmp = timer->next;
    if(!tmp || (timer->expire < tmp->expire)){
        return;
    }
    if(timer == head){
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer,head);
    }
    else{
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer,timer->next);
    }
}