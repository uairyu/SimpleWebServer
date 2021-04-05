#pragma once
#include <pthread.h>

#include <cstdio>
#include <exception>
#include <list>

#include "http_conn.h"
#include "locker.h"
#include "uniInclude.h"

template <typename T>
class threadpool {
public:
    threadpool( int thread_num = 1, int max_requests = 10000 );
    ~threadpool();
    bool append( T *request );

private:
    static void *worker( void *va_arg );
    void run();

private:
	int m_id; //线程id
    int m_threadNum;            //线程池中的线程数
    int m_maxRequests;          //请求队列中允许的最大请求数
    pthread_t *m_threads;       //线程池数组, 大小为m_threadNum
    std::list<T *> m_workerQue; //请求队列
    mutex m_queueLocker;        //保护请求队列的锁
    sem m_queueStat;            //是否有任务
    bool m_stop;                //是否结束线程
};

template <typename T>
threadpool<T>::threadpool( int thread_num, int max_requests )
    : m_threadNum( thread_num ), m_maxRequests( max_requests ), m_stop( false ),
      m_threads( nullptr ){
		  if(DEBUG){
			  printf("m_threadNum:'%d',m_maxRequests:'%d'\n",m_threadNum,m_maxRequests);
		  }
    if ( ( thread_num <= 0 ) || ( max_requests <= 0 ) ) {
        throw std::exception();
    }

    m_threads = new pthread_t[ thread_num ];
    if ( ! m_threads )
        throw std::exception();

    for ( int i = 0; i < m_threadNum; ++i ) {
        printf( "creating thread %d \n", i );
        if ( pthread_create( m_threads + i, NULL, worker, this ) != 0 ) {
            delete[] m_threads;
            throw std::exception();
        }
        if ( pthread_detach( m_threads[ i ] ) ) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append( T *requests ) {
    m_queueLocker.lock();
    if ( m_workerQue.size() > m_maxRequests ) {
        m_queueLocker.unlock();
        return false;
    }
    if ( DEBUG ) {
        printf( "threadpool append fd %d \n",((http_conn* )requests)->m_sockfd);
    }

    m_workerQue.push_back( requests );
    m_queueLocker.unlock();
    m_queueStat.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker( void *arg ) {
    threadpool *pool = (threadpool *)arg;
    if ( DEBUG ) {
        printf( "threadpool run\n" );
    }
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run() {
    while ( ! m_stop ) {
        m_queueStat.wait();
        if ( DEBUG ) {
            printf( "queue get task\n" );
        }
        m_queueLocker.lock();
        if ( m_workerQue.empty() ) {
            m_queueLocker.unlock();
            continue;
        }
        T *request = m_workerQue.front();
        m_workerQue.pop_front();
        m_queueLocker.unlock();
        if ( ! request ) {
            continue;
        }
        request->process();
    }
}
