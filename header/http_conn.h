#include"uniInclude.h"
#pragma once

class http_conn{
public :
	static const int RD_BUFFER_SIZE=1024;
	static const int WR_BUFFER_SIZE=2048;
	//请求方法
	enum METHOD {GET=0,POST,HEAD};
	//解析客户请求时，状态机状态
	enum PARSING_STATE {CHECKING_STATE_REQUESTING=0,
						CHECKING_STATE_HEADER,
						CHECKING_STATE_CONTENT};
	//处理请求时可能的结果
	enum HTTP_CODE{NO_REQUEST,GET_REQUEST,BAD_REQUEST,
					NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,
					INTERNAL_ERROR,CLOSED_CONNECTION};
	//行读取状态
	enum LINE_STATUS { LINE_OK=0,LINE_BAD,LINE_OPEN};

public :
	http_conn(){}
	~http_conn(){}
	//初始化监听请求
	void init(int sockfd,const sockaddr_in & in_addr);
	//关闭连接
	void close_conn(bool real_close=true);
	//处理客户请求
	void process();
	//non-block read
	bool read();
	//non-block write
	bool write();

private:
	//初始化连接
	void init();
	//处理http请求
	HTTP_CODE process_read();
	//填充http应答
	bool process_write(HTTP_CODE ret);

	//被 process_read 调用以分析http请求
	HTTP_CODE parse_request_line(char* text);
	HTTP_CODE parse_header(char* text);
	HTTP_CODE parse_content(char * text);
	HTTP_CODE do_request();
	char * get_line(){return m_readBuf+ m_startLine;}
	LINE_STATUS parse_line();

	//下面函数被 process_write调用 ：直译HTPP应答
	void unmap();
	bool add_response(const char* format,...);
	bool add_content(const char *content);
	bool add_status_line(int status,const char * title);
	bool add_headers(int content_length);
	bool add_content_length(int content_length);
	bool add_linger();
	bool add_blank_line();

public:
	//所有socket事件都注册到同一个epoll事件表中
	static int m_epollfd;
	static int m_userCnt;
    //该 http 连接的socket和对方的socket 地址
    int m_sockfd;

private:

	sockaddr_in m_address;
    //已经发送的字数数
    int m_bytes_have_sent;
	//需要发送的字节数
	int m_bytes_to_send;
    //读缓冲区
    char m_readBuf[ RD_BUFFER_SIZE ];
    //标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置
    int m_readIdx;
	//当前正在分析的字符在读缓冲区中的位置
	int m_checkedIdx;
	//当前正在解析的行起始位置
	int m_startLine;
	//写缓冲区
	char m_writeBuf[WR_BUFFER_SIZE];
	//写缓冲区中待发送的字节数
	int m_writeIdx;

	//主状态机当前所处的状态
	PARSING_STATE m_checkState;

	//请求方法
	METHOD m_method;

	//客户请求的目标文件完整路径： doc_root + m_url ,doc_root 为网站根目录
	char m_realFilePath[FILENAME_MAX];
	//客户请求的目标文件名
	char * m_url;
	//HTTP版本号
	char * m_version;
	//主机名
	char * m_host;
	//HTTP请求的信息体长度
	int m_contentLength;
	//HTTP请求 保持连接
	bool m_linger;

	//客户请求的目标文件被 mmap到内存中的位置
	char * m_fileAddr;
	//目标文件状态, 判断是否为目录, 可读可写, 文件大小
	struct stat m_fileStat;
	//用 writev 来写
	iovec m_iv[2];
	int m_ivCnt;
};