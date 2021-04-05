#pragma once
#define DEBUG 0
#include<unistd.h>
#include<sys/mman.h>
#include<iostream>
#include<sys/stat.h>
#include<sys/sem.h>
#include<sys/epoll.h>
#include<sys/shm.h>
#include<sys/signal.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<sys/uio.h>
#include<cstdarg>
#include<fcntl.h>
#include<cstring>
#include<pthread.h>
#include<cstdlib>
#include<cstdio>
#include<cassert>
#include<sys/types.h>
using std::cout;
using std::cin;
using std::endl;
using std::cerr;
