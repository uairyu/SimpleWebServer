# SimpleWebServer
- - -
Linux下的C++轻量级Web服务器

* 实现了 **线程池 + 非阻塞socket + epoll(ET) + 事件处理Readctor** 并发模型
* 实现了**主从状态机**解析HTTP请求（可对URL编码进行解码），目前仅支持解析GET请求，
- - -

## 使用方法：
开发环境为Windows下的VSCode，使用远程SSH插件连接至虚拟机的Ubuntu，因此有使用VSCode环境的需要自行修改.vscode下的配置。

若没有VSCode也可使用下面步骤来编译代码：

1. 下载源代码
`git clone https://github.com/uairyu/SimpleWebServer`
2. 安装g++（clang++在VSCode中代码提示更好，可自行查阅资料安装LLVM+clang）

	`sudo apt-get install g++`

3. 编译，输入（默认使用g++，可修改makefile使用clang++）

	`make`

	便可在当前目录下得到`main`可执行文件，输入`./main`便可执行

- - -
## 配置
### 输出DEBUG信息
编辑`header/uniInclude.h`，修改`#define DEBUG 0`为`define DUBG 1`即可得到调试输出信息
### 修改监听端口
打开`main.cpp`文件，找到`main函数`位置，找到`port`变量修改为想要的值即可
### 修改线程数
默认线程数为1，可编辑`header/threadpool.h`，把`threadpool( int thread_num = 1, int max_requests = 10000 );`的`thread_num = 1`的1改为你想要的线程数，重新编译

- - -
## 开发中遇到一些问题
首先得说，《Linux高性能服务器编程》是一本好书，让我开拓了C++开发的视野，虽然在开发也遇到一些疑惑的东西，但一般在百度、CSDN、Google可得到解决

以下是原书15.6节上一些个人认为有问题的地方，如有不对请批评指出：

1. `main函数`调用`accept`时候没有使用while去把所有待连接的socket读完，导致的结果是如果网页下面需要请求多个资源，则会一直pending。

![](https://github.com/uairyu/SimpleWebServer/blob/master/image/bug_accept.png)

2. `process_write函数`返回false时，会使`m_sockfd=-1`，但没有直接return，因而会执行下面的`modfd函数`

![](https://github.com/uairyu/SimpleWebServer/blob/master/image/bug_write_ret.png)
- - -
# 压力测试

已经把测试所用网页放置`webbench-1.5`目录下，可自行测试。

测试环境：并发连接总数10000，测试5秒，请求的网页大小为13018字节。

测试机：Ubuntu18.04，CPU：I5-4210m 内存8G

被测试机：VMware15下Ubuntu18.04，CPU：I5-4210m 内存1G，线程池数量：1

QPS：3794，所有请求均成功

![](https://github.com/uairyu/SimpleWebServer/blob/master/image/webbench_result.png)
- - -
# 致谢

《Linux高性能服务器编程》/ 游双著 2013年6月第一版