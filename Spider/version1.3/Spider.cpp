#include "ThreadPool.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <netdb.h>
#include <sys/socket.h>
#include <time.h>
#include <queue>
#include <hash_set>

namespace __gnu_cxx {
    template<>
    struct hash<std::string>
    {
        hash<char*> h;
        size_t operator()(const std::string &s) const
        {
            return h(s.c_str());
        };
    };
}

using namespace std;
using namespace __gnu_cxx;

#define DEFAULT_PAGE_BUF_SIZE 1048576

pthread_mutex_t mutUrl, mutImg, mutGet;

queue<string> hrefUrl;
hash_set<string> visitedUrl;
hash_set<string> visitedImg;

int depth = 0;
int g_ImgCnt = 1;

//解析URL，解析出主机名，资源名
bool ParseURL(const string &url, string &host, string &resource)
{
    if(strlen(url.c_str()) > 2000) {
        return false;
    }
    const char *pos = strstr(url.c_str(), "http://");
    if(pos == NULL) 
        pos = url.c_str();
    else 
        pos += strlen("http://");
    if(strstr(pos, "/") == 0)   
        return false;
    char pHost[100];
    char pResource[2000];
    sscanf(pos, "%[^/]%s", pHost, pResource);
    host = pHost;
    resource = pResource;
    return true;
}

//使用Get请求，得到响应
bool GetHttpResponse(const string &url, char *&response, int &bytesRead)
{
    string host, resource;
    if(!ParseURL(url, host, resource)) {
        cout << "Can't parse thr URL" << endl;
        return false;
    }
    //建立socket
    struct hostent *hp = gethostbyname(host.c_str());
    if(hp == NULL) {
        cout << "Can't find host address" << endl;
        return false;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock == -1 || sock == -2) {
        cout << "Can't create sock" << endl;
        return false;
    }
    //建立服务器地址
    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(80);
    memcpy(&serveraddr.sin_addr, hp->h_addr, 4);

    //建立连接
    if(0 != connect(sock, (struct sockaddr*)&serveraddr, sizeof(serveraddr))) {
        cout << "Can't connect: " << url << endl;
        close(sock);
        return false;
    }

    //准备发送数据
    string request = "GET " + resource
                    + " HTTP/1.1\r\nHost:" + host
                    + "\r\nConnection:Close\r\n\r\n";
    
    //发送数据
    if((send(sock, request.c_str(), request.size(), 0)) < 0) {
        cout << "send error" << endl;
        close(sock);
        return false;
    }

    //接受数据
    int m_nContentLength = DEFAULT_PAGE_BUF_SIZE;
    pthread_mutex_lock(&mutGet);
    char *pageBuf = (char *)malloc(m_nContentLength);
    memset(pageBuf, 0, m_nContentLength);

    bytesRead = 0;
    int ret = 1;
    cout << "Read: ";
    while(ret > 0) {
        ret  = recv(sock, pageBuf + bytesRead, m_nContentLength - bytesRead, 0);
        if(ret > 0) {
            bytesRead += ret;
        }
        if(m_nContentLength - bytesRead < 100) {
            cout << "\nRealloc memorry" << endl;
            m_nContentLength *= 2;
            //重新分配内存
            pageBuf = (char*)realloc(pageBuf, m_nContentLength);
        }
        cout << ret << " ";
    }
    cout << endl;

    pageBuf[bytesRead] = '\0';
    response = pageBuf;
    pthread_mutex_unlock(&mutGet);
    close(sock);
    return true;
}

//提取所有的URL以及图片URL
void HTMLParse(string &htmlResponse, vector<string> &imgurls, const string &host) 
{
    //找所有连接，加入queue中
    const char *p =htmlResponse.c_str();
    const char *tag = "href=\"";
    const char *pos = strstr(p, tag);
    ofstream ofile("./source/url.txt", ios::app);
    while(pos) {
        pos += strlen(tag);
        const char * nextQ = strstr(pos, "\"");
        if(nextQ) {
            char * url = new char[nextQ - pos + 1];
            //固定大小的会发生缓冲区溢出的危险
            sscanf(pos, "%[^\"]", url);
            //转换成string类型，可以自动释放内存
            string surl = url;

            pthread_mutex_lock(&mutUrl);
            if(visitedUrl.find(surl) == visitedUrl.end()) {
                visitedUrl.insert(surl);
                ofile << surl << endl;
                hrefUrl.push(surl);
            }
            pthread_mutex_unlock(&mutUrl);

            pos = strstr(pos, tag);
            delete [] url;      //释放申请的内存
        }
    }
    ofile << endl << endl;
    ofile.close();

    tag = "<img ";
    const char *att1 = "src=\"";
    const char *att2 = "lazy-src=\"";
    const char *pos0 = strstr(p, tag);
    while(pos0) {
        pos0 += strlen(tag);
        const char * pos2 = strstr(pos0, att2);
        if(!pos2 || pos2 > strstr(pos0, ">")) {
            pos = strstr(pos0, att1);
            if(!pos) {
                pos0 = strstr(att1, tag);
                continue;
            }
            else {
                pos = pos + strlen(att1);
            }
        }
        else {
            pos = pos2 + strlen(att2);
        }
        const char * nextQ = strstr(pos, "\"");
        if(nextQ) {
            char * url = new char[nextQ - pos + 1];
            sscanf(pos, "%[^\"]", url);
            cout << url << endl;
            string imgUrl = url;

            pthread_mutex_lock(&mutImg);
            if(visitedImg.find(imgUrl) == visitedImg.end()) {
                visitedImg.insert(imgUrl);
                imgurls.push_back(imgUrl);
            }
            pthread_mutex_unlock(&mutImg);

            pos0 = strstr(pos0, tag);
            delete [] url;
        }
    }
    cout << "end of Parse this html" << endl;
}

//把URL转化为文件名
string ToFileName(const string &url) 
{
    string fileName;
    fileName.resize(url.size());
    int k = 0;
    for(int i = 0; i < (int)url.size(); ++i) {
        char ch = url[i];
        if(ch != '\\' && ch != '/' && ch != ':' && ch != '*'
                        && ch != '?' && ch != '"' && ch != '<'
                        && ch != '>' && ch != '|')
            fileName[k++] = ch;
    }
    return fileName.substr(0, k) + ".txt";
}

//创建多级目录
bool CreateDir(const char *sPathName)
{
    char DirName[512];
    strcpy(DirName, sPathName);
    int i, len = strlen(DirName);
    if(DirName[len - 1] != '/')
        strcat(DirName, "/");
    
    len = strlen(DirName);
    for(i = 1; i < len; ++i) {
        if(DirName[i] == '/') {
            DirName[i] = 0;
            if(access(DirName, 0) != 0) {
                if(mkdir(DirName, 0755) == -1) {
                    perror("mkdir error");
                    return false;
                }
            }
            DirName[i] = '/';
        }
    }
    return true;
}

//下载图片到img文件夹
void DownLoadImg(vector<string> &imgurls, const string &url)
{
    //生成保存该url下图片的文件
    string foldname = ToFileName(url);
    foldname = "./source/img/" + foldname;
    if(!CreateDir(foldname.c_str()))
        cout << "Can't create directory:" << foldname << endl;
    char *image = NULL;
    int byteRead;
    for(int i = 0; i < imgurls.size(); ++i) {
        //判断是否为图片，bmp，jpg，jpeg，gif
        string str = imgurls[i];
        int pos = str.find_last_of(".");
        if(pos == string::npos) {
            continue;
        }
        else {
            string ext = str.substr(pos + 1, str.size() - pos - 1);
            if(ext != "bmp" && ext != "jpg" && ext != "jpeg" && ext != "gif" && ext != "png")
                continue;
        }
        //下载其中的内容
        if(GetHttpResponse(imgurls[i], image, byteRead)) {
            if(strlen(image) == 0) {
                continue;
            }
            const char *p = image;
            const char *pos = strstr(p, "\r\n\r\n") + strlen("\r\n\r\n");
            int index = imgurls[i].find_last_of("/");
            if(index != string::npos) {
                string imgname = imgurls[i].substr(index, imgurls[i].size());
                ofstream ofile(foldname + imgname, ios::binary);
                if(!ofile.is_open())
                    continue;
                cout << g_ImgCnt++ << foldname + imgname << endl;
                ofile.write(pos, byteRead - (pos - p));
                ofile.close();
            }
            free(image);
            image = NULL;
        }
    }
}

//广度遍历
void* BFS(void *arg)
{
    string url = static_cast<const char*>(arg);
    char * response = NULL;
    int bytes;
    //获取网页的相应，放入response中
    if(!GetHttpResponse(url, response, bytes)) {
        cout << "The url is wrong! ignore." << endl;
        return NULL;
    }
    string httpResponse = response;
    free(response);
    response = NULL;
    string filename = ToFileName(url);
    ofstream ofile("./source//html/" + filename);
    if(ofile.is_open()) {
        //保存该网页的文本内容
        ofile << httpResponse << endl;
        ofile.close();
    }
    vector<string> imgurls;
    //解析该网页的所有图片连接，放入imgurls里面
    HTMLParse(httpResponse, imgurls, url);
    //下载所有的图片资源
    DownLoadImg(imgurls, url);
}

int main()
{

    //创建文件夹，保存图片和网页文本文件
    CreateDir("./source/img");
    CreateDir("./source/html");
    //遍历的起始地址

    cout << "Please enter a right url!(\"www.guyuehome.com\")" << endl;
    string temp;
    getline(cin, temp);
    string urlStart = "http://" + temp + "/";

    //创建线程池
    threadpool_t *thp = threadpool_create(5, 20, 50);
    cout << "threadpool init ... ..." << endl;

    //使用广度遍历
    //提取网页中的超链接放入hrefurl中
    //提取图片链接，下载图片
    threadpool_add_task(thp, BFS, (void*)(urlStart.c_str()));
    sleep(5);
    //BFS(urlStart);
    //访问过的网址保存下来
    visitedUrl.insert(urlStart);
    while(hrefUrl.size() != 0 && visitedUrl.size() <= 0xfff) {
        //从队列的最开始取出一个网址
        string url = hrefUrl.front();
        cout <<"Get: " + url << endl;
        threadpool_add_task(thp, BFS, (void*)(url.c_str()));
        //BFS(url);
        hrefUrl.pop();
    }
     /* 销毁 */
    int ect = threadpool_destroy(thp);

    return 0;
}