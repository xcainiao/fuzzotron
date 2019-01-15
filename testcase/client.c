#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>

#define MYPORT  8887
#define BUFFER_SIZE 4096

#define MSG_NOSIGNAL 0x2000

int main()
{
    int sock_cli = socket(AF_INET,SOCK_STREAM, 0);

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(MYPORT);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip

    if (connect(sock_cli, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("connect");
        exit(1);
    }

    char sendbuf[BUFFER_SIZE];

    memset(sendbuf, 97, sizeof(sendbuf));
    
    int res;

    res = send(sock_cli, "12345", strlen("12345"), 0); ///发送
    printf("%d\n", res);

    res = send(sock_cli, sendbuf, sizeof(sendbuf), 0); ///发送
    printf("%d\n", res);
    
    sleep(2);
    
    printf("123\n");

    res = send(sock_cli, "12345", strlen("12345"), MSG_NOSIGNAL); ///发送
    printf("%d\n", res);

    printf("123\n");

    res = send(sock_cli, "12345", strlen("12345"), MSG_NOSIGNAL); ///发送
    printf("%d\n", res);

    res = send(sock_cli, "12345", strlen("12345"), MSG_NOSIGNAL); ///发送
    printf("%d\n", res);

    close(sock_cli);
    /*
    while (fgets(sendbuf, sizeof(sendbuf), stdin) != NULL)
    {
        send(sock_cli, sendbuf, strlen(sendbuf),0); ///发送
        if(strcmp(sendbuf,"exit\n")==0)
            break;

        memset(sendbuf, 0, sizeof(sendbuf));
    }
    close(sock_cli);
   // */

    return 0;
}
