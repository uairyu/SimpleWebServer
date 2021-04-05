#include "http_conn.h"

#include "uniInclude.h"
#include <asm-generic/errno.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form =
    "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form =
    "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form =
    "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form =
    "There was an unusual problem serving the requested file.\n";

const char *doc_root = "/var/www";

int setnonblocking( int fd ) {
    int oldfl = fcntl( fd, F_GETFL );
    int newfl = oldfl | O_NONBLOCK;
    fcntl( fd, F_SETFL, newfl );
    return oldfl;
}

void addfd( int epollfd, int fd, bool oneshot ) {
    epoll_event ev;
    bzero( &ev, sizeof( ev ) );
    ev.data.fd = fd;
    // EPOLLRDHUP 对端断开连接
    ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if ( oneshot ) {
        ev.events |= EPOLLONESHOT;
    }

    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &ev );
    setnonblocking( fd );
}

void removefd( int epollfd, int fd ) {
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );
    // shutdown(fd,SHUT_RDWR);
}

void modfd( int epollfd, int fd, int fl, bool oneshot = true ) {
    epoll_event ev;
    bzero( &ev, sizeof( ev ) );
    ev.data.fd = fd;
    ev.events = fl | EPOLLET | EPOLLRDHUP;
    if ( oneshot ) {
        ev.events |= EPOLLONESHOT;
    }

    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &ev );
}

//

int http_conn::m_userCnt = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn( bool real_close ) {
    if ( real_close && m_sockfd != -1 ) {
        removefd( m_epollfd, m_sockfd );
        m_sockfd = -1;
        m_userCnt--;
    }
}

void http_conn::init( int sockfd, const sockaddr_in &in_addr ) {
    m_sockfd = sockfd;
    m_address = in_addr;
    if ( DEBUG ) {
        printf( "http_conn init for %d , addr is: %s:%d\n", sockfd,
                inet_ntoa( in_addr.sin_addr ), ntohs( in_addr.sin_port ) );
        // int reuse = 1;
        // setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse,
        //            sizeof( reuse ) );
    }
    addfd( m_epollfd, m_sockfd, true );
    m_userCnt++;
    init();
}

void http_conn::init() {
    m_checkState = CHECKING_STATE_REQUESTING;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_bytes_have_sent = 0;
    m_bytes_to_send = 0;
    m_version = 0;
    m_contentLength = 0;
    m_host = 0;
    m_startLine = 0;
    m_checkedIdx = 0;
    m_readIdx = 0;
    m_writeIdx = 0;
    bzero( m_writeBuf, WR_BUFFER_SIZE );
    bzero( m_readBuf, RD_BUFFER_SIZE );
    bzero( m_realFilePath, FILENAME_MAX );
}

//从状态机 解析出一行内容
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    /*checkedIdx 指向Rdbuffer 中当前正在分析的字节
    readIdx 指向Rdbuffer 数据尾部的下一字节
    0-checkedIdx 的字节已经分析完毕
    checkedIdx-RdIdx的字节逐个分析
    */

    for ( ; m_checkedIdx < m_readIdx; ++m_checkedIdx ) {
        temp = m_readBuf[ m_checkedIdx ];
        if ( temp == '\r' ) {
            //还没读完
            if ( ( m_checkedIdx + 1 ) == m_readIdx ) {
                return LINE_OPEN;
            } else if ( m_readBuf[ m_checkedIdx + 1 ] == '\n' ) {
                m_readBuf[ m_checkedIdx++ ] = '\0';
                m_readBuf[ m_checkedIdx++ ] = '\0';

                return LINE_OK;
            }
            return LINE_BAD;
        } else if ( temp == '\n' ) {
            if ( m_checkedIdx > 1 && m_readBuf[ m_checkedIdx - 1 ] == '\r' ) {
                m_readBuf[ m_checkedIdx - 1 ] = '\0';
                m_readBuf[ m_checkedIdx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

bool http_conn::read() {
    if ( m_readIdx >= RD_BUFFER_SIZE ) {
        return false;
    }
    int bytes_read = 0;
    while ( true ) {
        bytes_read = recv( m_sockfd, m_readBuf + m_readIdx,
                           RD_BUFFER_SIZE - m_readIdx, 0 );

        if ( DEBUG ) {
            printf( "recv %d %d bytes\n", m_sockfd, bytes_read );
            if ( bytes_read > 0 )
                printf( "recv origin request:\n'%s'\n", m_readBuf );
        }
        if ( bytes_read == -1 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                return true;
            }
            return false;
        } else if ( bytes_read == 0 ) {
            return false;
        }
        m_readIdx += bytes_read;
    }
    return true;
}

//解析HTTP请求行,获得请求方法 目标URI HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line( char *text ) {
    int jmp = 0;

    m_url = strpbrk( text, " \t" );
    if ( ! m_url ) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;
    if ( DEBUG ) {
        printf( "method :%s\n", method );
    }
    if ( strcasecmp( method, "GET" ) == 0 ) {
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }
    //上面得出请求方法 m_method
    m_url += strspn( m_url, " \t" );
    m_version = strpbrk( m_url, " \t" );
    if ( ! m_version ) {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn( m_version, " \t" );
    if ( strcasecmp( m_version, "HTTP/1.1" ) != 0 ) {
        return BAD_REQUEST;
    }
    if ( strncasecmp( m_url, "http://", 7 ) == 0 ) {
        m_url += 7;
        m_url = strchr( m_url, '/' );
    }
    if ( ! m_url || m_url[ 0 ] != '/' ) {
        return BAD_REQUEST;
    }
    char *pp = nullptr;
    while ( ( pp = strchr( m_url, '%' ) ) != nullptr ) {
        //处理url编码
        char t2[ 3 ];
        if ( ( pp + 1 ) < m_version && ( pp + 2 ) < m_version &&
             isalnum( *( pp + 1 ) ) && isalnum( *( pp + 2 ) ) ) {
            strncpy( t2, pp + 1, 2 );
            t2[ 2 ] = 0;
            unsigned char f;
            sscanf( t2, "%x", &f );
            *pp = f;
            int tlen = (int)( m_version - pp );
            for ( int i = 1; i < tlen - 2; ++i ) {
                pp[ i ] = pp[ i + 2 ];
            }
        } else
            return BAD_REQUEST;
    }
    if ( DEBUG ) {
        printf( "m_url:'%s'\n", m_url );
    }
    // 上面得出请求路径 m_url

    m_checkState = CHECKING_STATE_HEADER;
    return NO_REQUEST;
}

//解析HTTP请求的 一个 头部信息
http_conn::HTTP_CODE http_conn::parse_header( char *text ) {
    //遇到空行,表示头部字段解析完毕
    if ( DEBUG ) {
        //    printf( "\nsockfd %d begin parse_header\n", m_sockfd );
        //    printf( "text:'%s'\n", text );
    }
    if ( text[ 0 ] == '\0' ) {
        if ( DEBUG ) {
            printf( "空行,头部字段解析完毕\n" );
        }
        if ( m_contentLength != 0 ) {
            m_checkState = CHECKING_STATE_CONTENT;
            return NO_REQUEST;
        }
        if ( DEBUG ) {
            printf( "得到请求\n" );
        }
        return GET_REQUEST;
    } else if ( strncasecmp( text, "Connection:", 11 ) == 0 ) {
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 ) {
            m_linger = true;
        }
    } else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 ) {
        text += 15;
        text += strspn( text, " \t" );
        m_contentLength = atol( text );
    } else if ( strncasecmp( text, "Host:", 5 ) == 0 ) {
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    } else {
        // printf( "--unkonwn header:'%s'\n\n", text );
    }
    return NO_REQUEST;
}

// 解析http请求的信息体,判断是否完整读入
http_conn::HTTP_CODE http_conn::parse_content( char *text ) {
    if ( m_readIdx >= ( m_contentLength + m_checkedIdx ) ) {
        text[ m_contentLength ] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//主状态机
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while ( ( ( m_checkState == CHECKING_STATE_CONTENT ) &&
              ( line_status == LINE_OK ) ) ||
            ( ( line_status = parse_line() ) == LINE_OK ) ) {
        text = get_line();
        m_startLine = m_checkedIdx;

        switch ( m_checkState ) {
        case CHECKING_STATE_REQUESTING:

            ret = parse_request_line( text );
            if ( ret == BAD_REQUEST ) {
                return BAD_REQUEST;
            }
            break;
        case CHECKING_STATE_HEADER:

            ret = parse_header( text );
            if ( ret == BAD_REQUEST )
                return BAD_REQUEST;
            else if ( ret == GET_REQUEST ) {
                return do_request();
            }
            break;
        case CHECKING_STATE_CONTENT:

            ret = parse_content( text );
            if ( ret == GET_REQUEST ) {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

/*得到一个完整正确的HTTP请求后,分析目标文件属性.
                                                                                        若目标文件存在可读且不为目录则用
                                                                                        mmap映射到m_fileAddr,并告诉对方获取文件成功
                                                                                        */
http_conn::HTTP_CODE http_conn::do_request() {
    if ( DEBUG ) {
        printf( "begin do_request\n" );
        printf( "doc_root:'%s'\n", doc_root );
    }
    strcpy( m_realFilePath, doc_root );
    int len = strlen( doc_root );
    strncpy( m_realFilePath + len, m_url, FILENAME_MAX - len - 1 );
    if ( DEBUG ) {
        printf( "sockfd %d complete m_readFiilepath:'%s'\n", m_sockfd,
                m_realFilePath );
    }
    if ( stat( m_realFilePath, &m_fileStat ) < 0 ) {
        if ( DEBUG ) {
            printf( "can't get file stat\n" );
        }
        return NO_RESOURCE;
    }
    if ( ! ( m_fileStat.st_mode & S_IROTH ) ) {
        if ( DEBUG ) {
            printf( "can't read\n" );
        }
        return FORBIDDEN_REQUEST;
    }
    if ( S_ISDIR( m_fileStat.st_mode ) ) {
        if ( DEBUG ) {
            printf( "get directory ,wrong\n" );
        }
        return BAD_REQUEST;
    }

    int fd = open( m_realFilePath, O_RDONLY );
    assert( fd >= 0 );
    m_fileAddr =
        (char *)mmap( 0, m_fileStat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    return FILE_REQUEST;
}

//对内存映射区执行munmap操作
void http_conn::unmap() {
    if ( m_fileAddr ) {
        munmap( m_fileAddr, m_fileStat.st_size );
        m_fileAddr = 0;
    }
}

// 写HTTP响应
bool http_conn::write() {
    int temp = 0;
    if ( m_bytes_to_send == 0 ) {
        modfd( m_epollfd, m_sockfd, EPOLLIN, true );
        init();
        return true;
    }
    while ( true ) {
        temp = writev( m_sockfd, m_iv, m_ivCnt );
        if ( DEBUG ) {
            printf( "writes %d fd %d bytes,head:%d ,content:%ld\n", m_sockfd,
                    temp, m_writeIdx, m_fileStat.st_size );
        }
        if ( temp <= -1 ) {
            //如何当前写缓冲没有空间,等下一轮 EPOLLOUT 事件
            //在此期间,服务器无法接收同一客户的下一个请求,但可以保证连接的完整性
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                modfd( m_epollfd, m_sockfd, EPOLLOUT, true );
                return true;
            }
            unmap();
            return false;
        }
        m_bytes_have_sent += temp;
        m_bytes_to_send -= temp;
        if ( m_bytes_have_sent >= m_iv[ 0 ].iov_len && m_ivCnt>1 ) {
            m_iv[ 0 ].iov_len = 0;
            m_iv[ 1 ].iov_base = m_fileAddr + m_bytes_have_sent - m_writeIdx;
            m_iv[ 1 ].iov_len = m_bytes_to_send;
        } else {
            m_iv[ 0 ].iov_base = m_writeBuf + m_bytes_have_sent;
            m_iv[ 0 ].iov_len -= m_bytes_have_sent;
        }
        if ( m_bytes_to_send <= 0 ) {
            //发送HTTP响应成功,
            //然后根据HTTP请求中的Connection字段决定是否关闭连接
            unmap();
            modfd( m_epollfd, m_sockfd, EPOLLIN, true );

            if ( m_linger ) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

//往写缓冲中写入待发送数据
bool http_conn::add_response( const char *format, ... ) {
    if ( m_writeIdx >= WR_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_writeBuf + m_writeIdx,
                         WR_BUFFER_SIZE - 1 - m_writeIdx, format, arg_list );
    if ( len >= ( WR_BUFFER_SIZE - 1 - m_writeIdx ) ) {
        va_end( arg_list );
        return false;
    }
    m_writeIdx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line( int status, const char *title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_conn::add_headers( int content_len ) {
    add_content_length( content_len );
    add_linger();
    return add_blank_line();
}

bool http_conn::add_content_length( int content_len ) {
    return add_response( "Content-Length:%d\r\n", content_len );
}

bool http_conn::add_linger() {
    return add_response( "Connection:%s\r\n",
                         ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line() { return add_response( "%s", "\r\n" ); }

bool http_conn::add_content( const char *content ) {
    return add_response( "%s", content );
}

//根据服务器处理HTTP请求的结果,决定返回客户端的内容
bool http_conn::process_write( HTTP_CODE ret ) {
    switch ( ret ) {
    case INTERNAL_ERROR:
        add_status_line( 500, error_500_title );
        add_headers( strlen( error_500_form ) );
        if ( ! add_content( error_500_form ) ) {
            return false;
        }
        break;
    case BAD_REQUEST:
        add_status_line( 400, error_400_title );
        add_headers( strlen( error_400_form ) );
        if ( ! add_content( error_400_form ) ) {
            return false;
        }
        break;
    case NO_RESOURCE:
        add_status_line( 404, error_404_title );
        add_headers( strlen( error_404_form ) );
        if ( ! add_content( error_404_form ) ) {
            return false;
        }
        break;
    case FORBIDDEN_REQUEST:
        add_status_line( 403, error_403_title );
        add_headers( strlen( error_403_form ) );
        if ( ! add_content( error_403_form ) ) {
            return false;
        }
        break;
    case FILE_REQUEST:
        add_status_line( 200, ok_200_title );
        if ( m_fileStat.st_size != 0 ) {
            add_headers( m_fileStat.st_size );
            m_iv[ 0 ].iov_base = m_writeBuf;
            m_iv[ 0 ].iov_len = m_writeIdx;
            m_iv[ 1 ].iov_base = m_fileAddr;
            m_iv[ 1 ].iov_len = m_fileStat.st_size;
            m_ivCnt = 2;
            m_bytes_to_send = m_writeIdx + m_fileStat.st_size;
            return true;
        } else {
            const char *ok_string = "<html><body></body></html>";
            add_headers( strlen( ok_string ) );
            if ( ! add_content( ok_string ) ) {
                return false;
            }

        }
        break;
    default:
        return false;
    }
    m_iv[ 0 ].iov_base = m_writeBuf;
    m_iv[ 0 ].iov_len = m_writeIdx;
    m_ivCnt = 1;
    m_bytes_to_send = m_writeIdx;
    return true;
}

// 由线程池中的工作线程调用, 处理HTTP请求入口函数
void http_conn::process() {
    if ( DEBUG ) {
        printf( "sockfd %d begin process\n", m_sockfd );
    }

    HTTP_CODE read_ret = process_read();
    if ( DEBUG ) {
        printf( "end process_read \n" );
    }
    if ( read_ret == NO_REQUEST ) {
        modfd( m_epollfd, m_sockfd, EPOLLIN, true );
        return;
    }

    if ( DEBUG ) {
        printf( "sockfd %d begin process_write\n", m_sockfd );
    }
    bool write_ret = process_write( read_ret );
    if ( DEBUG ) {
        printf( "sockfd %d end process_write,ret=%d\n", m_sockfd, write_ret );
    }
    if ( ! write_ret ) {
        close_conn();
		return;
    }
    modfd( m_epollfd, m_sockfd, EPOLLOUT, true );
}