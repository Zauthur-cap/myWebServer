#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;
//初始化数据库读取表，将用户名和密码以键值对的形式存入map中
void http_conn::initmysql_result(connection_pool *connPool){
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);
    //在user表中检索username，passwd数据，浏览器端输入
    if(mysql_query(mysql, "SELECT username, passwd FROM user")){
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));       //输出错误信息,mysql_error()用于获得最近的一次MySQL操作所产生的文本错误信息
    }
    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);
    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);
    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while(MYSQL_ROW row = mysql_fetch_row(result)){ //循环条件是mysql_fetch_row()函数返回非空
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

//对文件描述符设置非阻塞
int setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);  //fcntl()用于操作文件描述符的一些特性，F_GETFL表示获取文件描述符的标志
    int new_option = old_option | O_NONBLOCK; //O_NONBLOCK表示非阻塞
    fcntl(fd, F_SETFL, new_option); //F_SETFL表示设置文件描述符的标志
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT,保证一个socket连接在任一时刻都只被一个线程处理
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode){//epollfd:epoll内核事件表的文件描述符，fd:文件描述符，one_shot:是否开启EPOLLONESHOT，TRIGMode:触发模式
    epoll_event event;
    event.data.fd = fd;
    if(TRIGMode == 1){//LT模式,默认
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;//EPOLLRDHUP表示TCP连接被对方关闭，或者对方关闭了写操作
    }
    else{
        event.events = EPOLLIN | EPOLLRDHUP;        
    }
    if(one_shot){ //开启EPOLLONESHOT
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event); //注册新的fd到epollfd指示的epoll内核事件表中
    setnonblocking(fd); //将文件描述符设置为非阻塞
}

//从内核事件表删除描述符
void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0); //从epollfd指示的epoll内核事件表中删除fd
    close(fd); //关闭文件描述符   
}

//将事件重置为EPOLLONESHOT,为什么要重置呢？因为一个socket连接在任一时刻都只被一个线程处理
/**
 * @brief Modifies the file descriptor in the epoll instance.
 * 
 * This function modifies the file descriptor in the epoll instance specified by `epollfd`.
 * It sets the specified event `ev` for the file descriptor `fd`.
 * The `TRIGMode` parameter determines the mode of the event.
 * 
 * @param epollfd The file descriptor of the epoll instance.
 * @param fd The file descriptor to modify.
 * @param ev The event to set for the file descriptor.
 * @param TRIGMode The mode of the event.
 */
void modfd(int epollfd,int fd,int ev,int TRIGMode){
    epoll_event event;//epoll事件
    event.data.fd = fd;
    if(TRIGMode == 1){
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;//ET模式
    }
    else{
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
    }
}

int http_conn::m_user_count = 0; //静态成员变量，统计用户数量
int http_conn::m_epollfd = -1; //静态成员变量，epoll内核事件表的文件描述符，初始化为-1

//关闭连接
void http_conn::close_conn(bool real_close){
    if(real_close && (m_sockfd != -1)){ //如果real_close为真且m_sockfd不为-1
        removefd(m_epollfd, m_sockfd); //从epoll内核事件表中删除m_sockfd
        m_sockfd = -1; //重置m_sockfd
        m_user_count--; //用户数量减一
    }
}
//初始化连接
void http_conn::init(int sockfd,const sockaddr_in &addr,char *root,int TRIGMode,int close_log, string user,string passwd,string sqlname){
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd, sockfd, true, m_TRIGMode); //将m_sockfd注册到epoll内核事件表中
    m_user_count++; //用户数量加一
    doc_root = root; //网站根目录
    m_TRIGMode = TRIGMode; //触发模式
    m_close_log = close_log; //是否关闭日志
    strcpy(sql_user,user.c_str()); //数据库用户名
    strcpy(sql_passwd,passwd.c_str()); //数据库密码
    strcpy(sql_name,sqlname.c_str()); //数据库名
    init(); //初始化连接
}

//初始化新接受的连接
void http_conn::init(){
    mysql = NULl;
    bytes_to_send = 0;//待发送字节数
    bytes_have_send = 0;//已发送字节数
    m_check_state = CHECK_STATE_REQUESTLINE;//主状态机初始状态
    m_linger = false;//默认不保持连接
    m_method = GET;//默认请求方法为GET
    m_url = 0;//请求的目标文件的文件名
    m_version = 0;//HTTP协议版本号
    m_content_length = 0;//HTTP请求的消息体的长度
    m_host = 0;//主机名
    m_start_line = 0;//当前正在解析的行的起始位置
    m_checked_idx = 0;//当前正在分析的字符在读缓冲区中的位置
    m_read_idx = 0;//标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
    m_write_idx = 0;//写缓冲区中待发送的字节数
    cgi = 0;//是否启用的POST
    m_state = 0;//读为0，写为1
    timer_flag = 0;//定时器标志
    improv = 0;//改进标志
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);//读缓冲区
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);//写缓冲区
    memset(m_real_file, '\0', FILENAME_LEN);//客户请求的目标文件的完整路径
}

//从状态机，用于解析出一行内容
//返回值为行的读取状态
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp; //临时变量
    for(;m_checked_idx < m_read_idx; ++m_checked_idx){ //m_read_idx表示读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
        temp = m_read_buf[m_checked_idx]; //获取当前要分析的字节
        if(temp == '\r'){
            if((m_checked_idx + 1) == m_read_idx){
                return LINE_OPEN;
            }
            else if(m_read_buf[m_checked_idx + 1] == '\n'){ //如果当前字符是'\r'且下一个字符是'\n'
                m_read_buf[m_checked_idx++] = '\0'; //将'\r'替换为'\0'
                m_read_buf[m_checked_idx++] = '\0'; //  将'\n'替换为'\0'
                return LINE_OK;
            }
            return LINE_BAD; //如果都不符合，返回LINE_BAD
        }
    }
    return LINE_OPEN; //如果都不符合，返回LINE_OPEN
}

//循环读取客户数据，直到无数据可读或对方关闭连接
bool http_conn::read_once(){
    if(m_read_idx >= READ_BUFFER_SIZE){
        return false;
    }
    int bytes_read = 0;
    //LT模式
    if(m_TRIGMode == 0){
        bytes_read = recv(m_sockfdm,m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        //上述参数表示：文件描述符，缓冲区，缓冲区大小，接收标志,m_read_idx表示读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
        if (bytes_read <= 0)
        {
            return false;
        }
        return true;
    }
    //ET模式
    else{
        while(true){
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if(bytes_read == -1){
                if(errno == EAGAIN || errno == EWOULDBLOCK){ //EAGAIN表示暂时没有数据可读，EWOULDBLOCK表示操作会被阻塞
                    break;
                }
                return false;
            }
            else if(bytes_read == 0){
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}
//解析HTTP请求行，获得请求方法，目标URL，以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text){
    m_url = strpbrk(text," \t");//strpbrk()函数用于在字符串中查找第一次出现的指定字符
    if(!m_url){
        return BAD_REQUEST;
    }
    *m_url++ = '\0'; //将m_url指向的字符置为'\0'
    char *method = text;
    if(strcasecmp(method, "GET") == 0){ //strcasecmp()函数用于比较两个字符串，不区分大小写
        m_method = GET;
    }
    else if(strcasecmp(method,"POST")== 0){
        m_method = POST;
        cgi = 1; //启用POST,cgi表示是否启用的POST
    }
    else  return BAD_REQUEST;
    m_url += strspn(m_url, " \t"); //strspn()函数用于计算字符串中第一个不在指定字符串中出现的字符的位置
    m_version = strpbrk(m_url, " \t");//获得HTTP版本号
    if(!m_version){
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0){ //判断HTTP版本号是否为1.1
        return BAD_REQUEST;
    }
    if(strcasecmp(m_url,"http://",7) == 0){
        m_url += 7;
        m_url = strchr(m_url, '/');//strchr()函数用于在字符串中查找指定字符的第一次出现
    }
    if(strncasecmp(m_url, "https://", 8) == 0){  //strncasecmp()函数用于比较两个字符串的前n个字符，不区分大小写
        m_url += 8;                          //如果m_url的前8个字符是https://，则m_url指向第9个字符
        m_url = strchr(m_url, '/');          //查找m_url中第一次出现'/'的位置
    }
    if(!m_url || m_url[0] != '/'){
        return BAD_REQUEST;
    }
    //当URL为/时，显示判断界面
    if(strlen(m_url) == 1){
        strcat(m_url, "judge.html");
    }
    m_check_state = CHECK_STATE_HEADER; //状态转移，解析请求头
    return NO_REQUEST;
}

//解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text){
    //遇到空行，表示头部字段解析完
    if(text[0] == '\0'){
        if(m_content_length != 0){
            m_check_state = CHECK_STATE_CONTENT; //状态转移，解析消息体
            return NO_REQUEST;
        }
        return GET_REQUEST; //获得一个完整的HTTP请求
    }
    else if(strncasecmp(text,"Connection:",11)== 0){ //11表示Connection:的长度
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0){  //keep-alive表示客户端请求保持连接
            m_linger = true;
        }
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0){ //Content-Length表示消息体的长度
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text); //atol()函数用于将字符串转换为长整型
    }
    else if(strncasecmp(text, "Host:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else{
        LOG_INFO("oop!unknow header: %s", text); //输出错误信息
    }
    return NO_REQUEST;//继续解析
}

//解析HTTP请求的消息体
http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if(m_read_idx >= (m_content_length + m_checked_idx)){ //消息体长度为m_content_length
        text[m_content_length] = '\0';  //将消息体的最后一个字符置为'\0'
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}
//主状态机
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK; //从状态机的状态
    HTTP_CODE ret = NO_REQUEST; //HTTP请求的可能结果
    char *text = 0; //指向当前正在分析的字符
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK)){
        text = get_line(); //获取一行数据
        m_start_line = m_checked_idx; //更新m_start_line
        LOG_INFO("got 1 http line: %s", text); //输出日志
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{ //解析请求行
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:{ //解析请求头
                ret = parse_headers(text);
                if(ret == BAD_REQUEST){
                    return BAD_REQUEST;
                }
                else if(ret == GET_REQUEST){
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT:{ //解析消息体,消息体指的是POST请求中的内容
                ret = parse_content(text);
                if(ret == GET_REQUEST){
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}
//当得到一个完整、正确的HTTP请求时，分析目标文件的属性
http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file,doc_root); //网站根目录
    int len = strlen(doc_root);
    const char *p = strrchr(m_url, '/'); //strrchr()函数用于查找字符串中最后一次出现的指定字符
    //处理cgi,cgi表示是否启用的POST
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')){
        char flag = m_url[1];  //flag表示请求的是哪个cgi程序
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/"); //将m_url_real的第一个字符置为'/'
        strcat(m_url_real, m_url + 2); //将m_url_real和m_url + 2拼接
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1); //将m_url_real拼接到m_real_file中
        free(m_url_real);
    }
    //将用户名和密码提取出来
    char name[100], password[100];
    int i;
    //格式一般为user=123&passwd=123
    for(i = 5; m_string[i] != '&';++i)
        name[i - 5] = m_string[i];
    name[i - 5] = '\0';
    int j = 0;
    for(i = i + 10; m_string[i] != '\0'; ++i, ++j)
        password[j] = m_string[i];
    
    if(*(p+1) == '3'){
        //登录验证
        char *sql_insert = (char *)malloc(sizeof(char) * 200);
        strcpy(sql_insert,"INSERT INTO user(username,passwd) VALUES(");
        strcat(sql_insert,"'");
        strcat(sql_insert,name);
        strcat(sql_insert,"', '");
        strcat(sql_insert,password);
        strcat(sql_insert,"')");

        if(users.find(name) == users.end()){
            m_lock.lock();//加锁
            int res = mysql_query(mysql, sql_insert); //向user表中插入数据
            users.insert(pair<string,string>(name,password));   
            m_lock.unlock();
            //注册成功
            if(!res){
                strcpy(m_url, "/log.html");   //跳转到登录界面
            }
            else{ 
                strcpy(m_url, "/registerError.html");
            }
        }
        else{
            strcpy(m_url, "/registerError.html");
        }
        //如果用户名和密码正确，跳转到登录界面
        else if(*(p + 1) == '2'){
            //登录验证
            if(users.find(name) != users.end() && users[name] == password){
                strcpy(m_url, "/welcome.html");
            }
            else{
                strcpy(m_url, "/logError.html");
            }
        }
    }
    
    if(*(p + 1) == '0'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '1'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '5'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '6'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else if(*(p + 1) == '7'){
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));
        free(m_url_real);
    }
    else{
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}
//获取一行数据
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}
//写HTTP响应
bool http_conn::write()
{
    int temp = 0;

    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        /*将相应报文状态行，消息头，空行和响应正文发给浏览器端*/
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0) //如果writev()函数返回负值，表示写操作出错
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;      //已发送字节数
        bytes_to_send -= temp;        //剩余发送字节数
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

bool http_conn::add_response(const char *format, ...){
    if(m_write_idx >= WRITE_BUFFER_SIZE){
        return false;
    }
    va_list arg_list; //va_list是一个指向参数的指针
    va_start(arg_list, format); //va_start()宏用于初始化arg_list指针，format是可变参数列表的最后一个固定参数
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list); //vsnprintf()函数用于将可变参数格式化输出到一个字符数组
    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)){ //如果len大于等于WRITE_BUFFER_SIZE - 1 - m_write_idx
        va_end(arg_list); //va_end()宏用于清理arg_list指针
        return false;
    }
    m_write_idx += len; //更新m_write_idx
    va_end(arg_list); //清理arg_list指针
    LOG_INFO("request:%s", m_write_buf); //输出日志
    return true;
}
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

//根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
        case INTERNAL_ERROR:{     //服务器内部错误
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)){
                return false;
            }
            break;
        }
        case BAD_REQUEST:{
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)){
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:{ //客户端对资源没有足够的访问权限
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)){
                return false;
            }
            break;
        }
        case FILE_REQUEST:{ //文件请求
            add_status_line(200, ok_200_title);
            if(m_file_stat.st_size != 0){
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else{
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)){
                    return false;
                }
            }
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process(){
    HTTP_CODE read_ret = process_read(); //解析HTTP请求
    if(read_ret == NO_REQUEST){ //NO_REQUEST表示请求不完整，需要继续接收请求数据
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }
    bool write_ret = process_write(read_ret); //根据HTTP请求的结果，决定返回给客户端的内容
    if(!write_ret){
        close_conn(); //关闭连接
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode); //将m_sockfd注册到epoll内核事件表中
}