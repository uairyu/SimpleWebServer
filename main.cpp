#include "config.h"
#include "http_conn.h"
#include "locker.h"
#include "threadpool.h"
#include "uniInclude.h"
#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <cerrno>
#include <iostream>
#include <netinet/in.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#define MAX_FD 6550
#define MAX_EVENT_NUM 10000
extern int addfd( int epollfd, int fd, bool oneshot );
extern int removefd( int epollfd, int fd );
void addsig( int sig, void( handler )( int ), bool restart = true ) {
    struct sigaction sa;
    bzero( &sa, sizeof( sa ) );
    sa.sa_handler = handler;
    if ( restart ) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

void show_error( int connfd, const char *info ) {
    printf( "%s\n", info );
    send( connfd, info, strlen( info ), 0 );
    close( connfd );
}
int main() {
    int port = 2222;
    //忽略SIGPIPE信号
    addsig( SIGPIPE, SIG_IGN );

    //创建线程池
    threadpool<http_conn> *pool = nullptr;
    try {
        pool = new threadpool<http_conn>;
    } catch ( ... ) {
        printf( "exit with unknown Error\n" );
        return 1;
    }

    //预先为每个可能的客户连接分配一个http_conn对象
    http_conn *users = new http_conn[ MAX_FD ];
    if ( DEBUG ) {
        printf( "正在生成http_conn2类, 占用 %ld KB\n",
                sizeof( http_conn ) * MAX_FD / 1024 );
    }
    assert( users );

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    printf( "listenfd %d \n", listenfd );
    assert( listenfd >= 0 );
    // struct linger tmp = { 1, 0 };
    // setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof( tmp ) );

    int ret = 0;
    struct sockaddr_in addr;
    bzero( &addr, sizeof( addr ) );
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl( INADDR_ANY );
    addr.sin_port = htons( port );
    int flag = 1;
    setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof( flag ) );

    ret = bind( listenfd, (struct sockaddr *)&addr, sizeof( addr ) );
    assert( ret >= 0 );

    ret = listen( listenfd, 5 );
    assert( ret >= 0 );

    epoll_event events[ MAX_EVENT_NUM ];

    int epollfd = epoll_create( 50 );
    assert( epollfd != -1 );
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;

    while ( true ) {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUM, -1 );
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            printf( " epoll failure\n" );
            break;
        }
        for ( int i = 0; i < number; ++i ) {
            int fd = events[ i ].data.fd;
            if ( fd == listenfd ) {
                while ( true ) {
                    struct sockaddr_in addr;
                    socklen_t socklen = sizeof( addr );
                    int remotefd =
                        accept( listenfd, (struct sockaddr *)&addr, &socklen );
                    printf( "listen fd detect %d comes\n", remotefd );
                    //出现错误
                    if ( remotefd < 0 ) {
                        printf( "remote fd < 0 , errno is :%d\n", errno );
                        break;
                    }
                    //超过epoll能监听的数量
                    if ( http_conn::m_userCnt >= MAX_FD ) {
                        show_error( remotefd, "Internal server busy" );
                        break;
                    }
                    users[ remotefd ].init( remotefd, addr );
                }
            } else if ( events[ i ].events &
                        ( EPOLLHUP | EPOLLRDHUP | EPOLLERR ) ) {

                printf( "remotefd %d close\n", fd );
                users[ fd ].close_conn( true );

            } else if ( events[ i ].events & EPOLLIN ) {
                if ( users[ fd ].read() ) {
                    if ( DEBUG ) {
                        printf( "***main:fd %d read success\n", fd );
                    }
                    pool->append( users + fd );
                } else {
                    if ( DEBUG ) {
                        printf( "***main:read failed\n" );
                    }
                    users[ fd ].close_conn();
                }
            } else if ( events[ i ].events & EPOLLOUT ) {
                if ( DEBUG ) {
                    printf( "sockfd %d can write\n", fd );
                }

                if ( ! users[ fd ].write() ) {
                    users[ fd ].close_conn();
                }
            }
        }
        if ( DEBUG )
            printf( "userCnt:%d\n", http_conn::m_userCnt );
    }
    close( epollfd );
    close( listenfd );
    delete[] users;
    delete pool;
    return 0;
}