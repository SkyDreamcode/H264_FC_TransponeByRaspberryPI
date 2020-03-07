#include "video_transpond.h"
#include "libar8020.h"
#include "ringfifo.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>

#define VIDEO_TRAN_PORT (6613)

volatile static int b_restart_listen_video_transpond_client_flag;
volatile static int video_transpond_sockfd;

static int createTcpSocket()
{
    int sockfd;
    int on = 1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0)
        return -1;

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));

    return sockfd;
}

static int bindSocketAddr(int sockfd, const char* ip, int port)
{
    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);

    if(bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr)) < 0)
        return -1;

    return 0;
}

static int acceptClient(int sockfd, char* ip, int* port)
{
    int clientfd;
    socklen_t len = 0;
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    len = sizeof(addr);

    clientfd = accept(sockfd, (struct sockaddr *)&addr, &len);
    if(clientfd < 0)
        return -1;
    
    strcpy(ip, inet_ntoa(addr.sin_addr));
    *port = ntohs(addr.sin_port);

    return clientfd;
}

#define DATA_SIZE (5*1024*1024)
static void send_h264_data(int send_file_fd, int clientSockfd)
{
	PORT port0;
	int r_cnt;
	int ret;
	int w_cnt;
	char buf0[DATA_SIZE];
	struct timeval begin, end, end1;

	Video_Port_Open(&port0,NULL);
	printf("open usb driver success\n");
	while(1)
	{
		gettimeofday(&begin, NULL);
		r_cnt = Video_Port_Rec(port0,buf0,DATA_SIZE);
		//gettimeofday(&begin, NULL);
		if(r_cnt > 0)
		{
		    //Storing files is just for demonstration. 
		    //Users should use their own decoder instead of saving files.
		    gettimeofday(&end, NULL);
			long delta_time =  1000000l*(end.tv_sec - begin.tv_sec) + (end.tv_usec-begin.tv_usec);
			printf("recv from usb size:%d,delta-time:%ld us\n", r_cnt, delta_time);
		    w_cnt = 0;
		    do{
		        ret = send(clientSockfd, &buf0[w_cnt], 1024, 0);
		        //ret = RingBufferPut(&buf0[w_cnt], r_cnt - w_cnt);
		        if(ret > 0)
		            w_cnt += ret;
		        else
		            usleep(500);

		    }while(w_cnt < r_cnt);
			gettimeofday(&end1, NULL);
			delta_time =  1000000l*(end1.tv_sec - end.tv_sec) + (end1.tv_usec-end.tv_usec);
			printf("send delta-time:%ld us\n", r_cnt, delta_time);
			
		}
		else if(r_cnt == 0)
		{
		    //usleep(500);
			//printf("recv data is 0 from usb\n");
			continue;
		}
		//memset(buf0, 0, 500000);
	}
}

static pthread_t send_to_terminal_id;
static pthread_t recv_from_usb_id;
static pthread_t udp_send_to_terminal_id;
static pthread_t recv_from_file_id;

static int fd[2];
static void *recv_from_usb_thread(void *arg)
{
	PORT port0;
	int r_cnt;
	int ret;
	int w_cnt;
	char buf0[DATA_SIZE];
	struct timeval begin, end;

	printf("thread recv_from_usb_thread\n");
	Video_Port_Open(&port0,NULL);
	while(1)
	{
		//gettimeofday(&begin, NULL);
		while(b_restart_listen_video_transpond_client_flag == 0)
		{
			usleep(1000000);
		}
		r_cnt = Video_Port_Rec(port0,buf0,DATA_SIZE);

		if(r_cnt > 0)
		{
		    //Storing files is just for demonstration. 
		    //Users should use their own decoder instead of saving files.
		    //printf("write data to pipe r_cnt : %d\n", r_cnt);
		    w_cnt = 0;
		    do{
		        //ret = send(clientSockfd, &buf0[w_cnt], 1024, 0);
		        //ret = RingBufferPut(&buf0[w_cnt], r_cnt - w_cnt);
		        ret = write(fd[1], &buf0[w_cnt], r_cnt - w_cnt);
		        if(ret > 0)
		            w_cnt += ret;
		        else if(ret < 0)
					printf("write data to pipe is error\n ");
				else
		            usleep(500);

		    }while(w_cnt < r_cnt);
			//gettimeofday(&end, NULL);
			//long delta_time =  1000000l*(end.tv_sec - begin.tv_sec) + (end.tv_usec-begin.tv_usec);
			//printf("recv from usb size:%d,delta-time:%ld us\n", r_cnt, delta_time);
		}
		else if(r_cnt == 0)
		{
		    
			//printf("recv data is 0 from usb\n");
			usleep(500);
			//continue;
		}


		usleep(100);
	}
}
int clientSockfd;
#define TCP_SEND_SIZE (1024)
static void *send_to_terminal_thread(void *arg)
{
	int client_fd;
	char buf0[TCP_SEND_SIZE];
	int r_cnt;
	int ret;
	int w_cnt;
	struct timeval begin, end;

	client_fd = *(int *)arg;

	printf("tcp  thread send_to_terminal_thread, client_fd:%d\n", client_fd);
	while(1)
	{
		//gettimeofday(&begin, NULL);
		while(b_restart_listen_video_transpond_client_flag == 0)
		{
			usleep(1000000);
		}

		r_cnt = read(fd[0], buf0, TCP_SEND_SIZE);
		//r_cnt = RingBufferGet(buf0,TCP_SEND_SIZE);	
		if(r_cnt > 0)
		{
			w_cnt = 0;
			do{
		        ret = send(video_transpond_sockfd, &buf0[w_cnt], r_cnt, MSG_NOSIGNAL);
		        //ret = send(client_fd, buf0, 1024, 0);
		        if(ret > 0)
		            w_cnt += ret;
		        else if(ret < 0){
					perror("video tcp send error\n");
					close(video_transpond_sockfd);
					b_restart_listen_video_transpond_client_flag = 0;
					break;
					//return;
		        	}

			}while(w_cnt < r_cnt);
				//printf("send video to app:%d\n", r_cnt);
			//gettimeofday(&end, NULL);
			//long delta_time =  1000000l*(end.tv_sec - begin.tv_sec) + (end.tv_usec-begin.tv_usec);
			//printf("send to terminal size:%d,delta-time:%ld us\n", r_cnt, delta_time);
		}
		else{
			printf("tcp read data from pipe is <=0\n");
		}
		usleep(1000);
	}
	
}

//#define UDP_SEND_SIZE (60000)
#define UDP_SEND_SIZE (5120)

int udp_server_fd;
struct sockaddr_in addr;

struct udp_client_info{
	struct sockaddr_in client_addr;
	int udp_server_fd;
};
static void*udp_send_to_terminal_thread(void*arg)
{
	struct udp_client_info client_info;
	char buf0[UDP_SEND_SIZE];
	int r_cnt;
	int ret;
	int w_cnt;
	struct timeval begin, end;

	memcpy(&client_info, (struct udp_client_info*)arg, sizeof(struct udp_client_info));

#if 0
	int local_fd = open("./local.h264",  O_RDWR|O_CREAT, S_IRUSR | S_IWUSR);
	if(local_fd < 0){
		perror("open create local.h264:");
		return;
	}
#endif
	printf("thread send_to_terminal_thread,udp_server_fd:%d addr_ip:%s .sin_port:%d\n", client_info.udp_server_fd, inet_ntoa(client_info.client_addr.sin_addr),client_info.client_addr.sin_port);
	while(1)
	{
		//gettimeofday(&begin, NULL);
		r_cnt = read(fd[0], buf0, UDP_SEND_SIZE);
		//r_cnt = RingBufferGet(buf0,TCP_SEND_SIZE);	
		if(r_cnt > 0)
		{
			w_cnt = 0;
			printf("read data from pipe r_cnt=%d  ", r_cnt);
			//write(local_fd,  buf0, r_cnt);
			sendto( client_info.udp_server_fd, buf0, r_cnt, 0, (struct sockaddr *)(&client_info.client_addr), sizeof(client_info.client_addr));
			#if 0
			do{
		        //ret = sendto(udp_server_fd, &buf0[w_cnt], UDP_SEND_SIZE, 0, (struct sockaddr *)(arg), sizeof(struct sockaddr_in));
		        ret = sendto( client_info.udp_server_fd, &buf0[w_cnt], r_cnt, 0, (struct sockaddr *)(&client_info.client_addr), sizeof(client_info.client_addr));
				
			//ret = sendto( client_info.udp_server_fd, buf2, 20, 0, (struct sockaddr *)(&client_info.client_addr), sizeof(client_info.client_addr));
				//ret = sendto( udp_server_fd, buf0, UDP_SEND_SIZE, 0, (struct sockaddr *)(&addr), sizeof(addr));
				//sendto(udp_server_fd, buf0, 512, 0,  (struct sockaddr*)&addr, sizeof(addr));		
		
		        //ret = send(client_fd, buf0, 1024, 0);
		        //ret = send(client_fd, buf0, 1024, 0);
		        if(ret > 0)
		            w_cnt += ret;
		        else if(ret < 0){
					
					perror("udp sendto send errorr_cnt::::");
					return;
		        	}

			}while(w_cnt < r_cnt);
				#endif
			//gettimeofday(&end, NULL);
			//long delta_time =  1000000l*(end.tv_sec - begin.tv_sec) + (end.tv_usec-begin.tv_usec);
			//printf("send to terminal size:%d,delta-time:%ld us\n", r_cnt, delta_time);
		}
		else{
			//usleep(10);
			printf("read data size is 0 from pipe \n");
		}
		usleep(1000);
	}
}

#define UDP_SEND_FILE_SIZE (1024)
static void *recv_from_file_thread(void *arg)
{
	int file_fd, local_fd;;
	char buf[UDP_SEND_FILE_SIZE] = {0};
	int ret;
	struct udp_client_info client_info;
	char buf0[UDP_SEND_FILE_SIZE] = {0};
	int r_cnt, i;
	//int ret;
	int w_cnt;
	struct timeval begin, end;
#if 1
	file_fd = open("./recv.h264",  O_RDWR, S_IRUSR | S_IWUSR);
	if(file_fd < 0){
		perror("open recv.h264:");
		return;
	}
#endif
	local_fd = open("./local.h264",  O_RDWR|O_CREAT, S_IRUSR | S_IWUSR);
	if(local_fd < 0){
		perror("open create local.h264:");
		return;
	}

	printf("open file success\n");
	memcpy(&client_info, (struct udp_client_info*)arg, sizeof(struct udp_client_info));
	printf("thread recv_from_file_thread,udp_server_fd:%d addr_ip:%s .sin_port:%d\n", client_info.udp_server_fd, inet_ntoa(client_info.client_addr.sin_addr),client_info.client_addr.sin_port);

	while(1)
	{
		//gettimeofday(&begin, NULL);
		#if 1
		//ret = read(file_fd, buf, UDP_SEND_FILE_SIZE);
		ret = read(fd[0], buf, UDP_SEND_FILE_SIZE);
		if(ret == 0){
			lseek(file_fd, 0, SEEK_SET);
			printf("restart read file\n");
			continue;
		}else if(ret < 0){
			perror("read:");
		}
		#endif
		//write(local_fd, buf, ret);
		ret = sendto( client_info.udp_server_fd, buf, ret, 0, (struct sockaddr *)(&client_info.client_addr), sizeof(client_info.client_addr));
		if(ret < 0)
		{
			perror("udp sendto error:");
		}
		#if 0
		else if(UDP_SEND_FILE_SIZE != ret){
			printf("%d  ", ret);
		}
		#endif
		usleep(1000);
		#if 0
		for(i = 0; i < 1000; i++){
			memset(buf0, i, UDP_SEND_FILE_SIZE);
			ret = sendto( client_info.udp_server_fd, buf0, UDP_SEND_FILE_SIZE, 0, (struct sockaddr *)(&client_info.client_addr), sizeof(client_info.client_addr));        
			if(ret < 0)
			{
				perror("udp sendto error:");
			}
			usleep(1000);
		}
		#endif
		//gettimeofday(&end, NULL);
		//long delta_time =  1000000l*(end.tv_sec - begin.tv_sec) + (end.tv_usec-begin.tv_usec);
		//printf("send to terminal size:%d,delta-time:%ld us\n", ret, delta_time);
	}
	close(file_fd);
	close(local_fd);
}


#define RING_BUFFER (10*1024*1024)

static void *video_transpond_thread(void *arg)
{
	int client_fd;
	int serverSockfd, ret;
	//char send_buffer[500000] = {0};
	int send_file_fd;
	
	//RingBufferInit(RING_BUFFER);

	if(pipe(fd) < 0){
		printf("create pipe is error\n");
		return;
	}
	printf("the pipe init success\n");
	#if 1
	serverSockfd = createTcpSocket();
    if(serverSockfd < 0)
    {
        printf("failed to create tcp socket\n");
        return;
    }	
	ret = bindSocketAddr(serverSockfd, "0.0.0.0", VIDEO_TRAN_PORT);
    if(ret < 0)
    {
        printf("failed to bind addr\n");
        return;
    }
    ret = listen(serverSockfd, 10);
    if(ret < 0)
    {
        printf("failed to listen\n");
        return;
    }

	

	    int clientSockfd;
        char clientIp[40];
        int clientPort;
		
        clientSockfd = acceptClient(serverSockfd, clientIp, &clientPort);
        if(clientSockfd < 0)
        {
            printf("failed to accept client\n");
            return;
        }
		printf("clientSockfd : %d  accept client;client ip:%s,client port:%d\n", clientSockfd, clientIp, clientPort);
#else
		udp_server_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if(udp_server_fd < 0){
			perror("udp socket create is error\n");
			return;
		}
		struct sockaddr_in local;
		local.sin_family = AF_INET;
		local.sin_port = htons(atoi("6666"));
		local.sin_addr.s_addr = inet_addr("192.168.1.97");
		//local.sin_addr.s_addr = htonl(INADDR_ANY);
		if(bind(udp_server_fd, (struct sockaddr*)&local, sizeof(local)) < 0){
			perror("bind");
			return;
		}
		int clientfd;
		socklen_t len = 0;
		//struct sockaddr_in addr;
		char buf[1024];

		memset(&addr, 0, sizeof(addr));
		len = sizeof(addr);
		printf("udp start recv data from client\n");
		if(0 < recvfrom(udp_server_fd, buf, sizeof(buf) -1, 0, (struct sockaddr*)&addr, &len))
		{
			
			printf("udp udp_server_fd:%d,client ip:%s,client port:%d\n", udp_server_fd,inet_ntoa(addr.sin_addr), addr.sin_port);
			
		}else{
			perror("udp recvfrom:");
		}
		printf("udp recv data:%s\n", buf);

		//char buf2[20] = "hello word22222";
		//sendto(udp_server_fd, buf2, 20, 0,  (struct sockaddr*)&addr, sizeof(addr));
#endif

		#if 1
		//send_h264_data(send_file_fd, clientSockfd);
		if(0 != pthread_create(&recv_from_usb_id, NULL,recv_from_usb_thread, NULL)){
			printf("thread recv_from_usb_thread create is error\n");
			return;
		}
		#else
		
		#endif
		
		#if 1
		if(0 != pthread_create(&send_to_terminal_id, NULL, send_to_terminal_thread, (void *)(&clientSockfd))){
			printf("thread send_to_terminal_thread create is error\n");
			return;			
		}
		#else
		struct udp_client_info client_info;
		memcpy(&(client_info.client_addr), &addr, sizeof(addr));
		client_info.udp_server_fd = udp_server_fd;
		//char buf1[20] = "hello word11111";
		//sendto(client_info.udp_server_fd, buf1, 20, 0,  (struct sockaddr*)&(client_info.client_addr), sizeof(client_info.client_addr));
		//if(0 != pthread_create(&udp_send_to_terminal_id, NULL, udp_send_to_terminal_thread, (void *)(&addr))){
		#if 0
		if(0 != pthread_create(&udp_send_to_terminal_id, NULL, recv_from_file_thread, (void *)(&client_info))){
			printf("thread udp_send_to_terminal_thread create is error\n");
			return;			
		}
		#else
		if(0 != pthread_create(&udp_send_to_terminal_id, NULL, udp_send_to_terminal_thread, (void *)(&client_info))){
			printf("thread udp_send_to_terminal_thread create is error\n");
			return;			
		}
		#endif
		#endif
		
		usleep(1000);
	
}

static void *video_transpond_listen_client_thread(void *arg)
{
	int client_fd;
	int serverSockfd, ret;
	//char send_buffer[500000] = {0};
	int send_file_fd;
	
	//RingBufferInit(RING_BUFFER);

	if(pipe(fd) < 0){
		printf("create pipe is error\n");
		return;
	}
	printf("the pipe init success\n");

	serverSockfd = createTcpSocket();
    if(serverSockfd < 0)
    {
        printf("failed to create tcp socket\n");
        return;
    }	
	ret = bindSocketAddr(serverSockfd, "0.0.0.0", VIDEO_TRAN_PORT);
    if(ret < 0)
    {
        printf("failed to bind addr\n");
        return;
    }


	

    int clientSockfd;
    char clientIp[40];
    int clientPort;

	while(1)
	{
		while(b_restart_listen_video_transpond_client_flag)
		{
			usleep(1000000);
		}
		printf("video tranpond listen client restart\n");
		ret = listen(serverSockfd, 10);
	    if(ret < 0)
	    {
	        printf("video transpond listen err, %s\n", strerror(errno));
			pthread_exit(NULL);
	        return;
	    }
        video_transpond_sockfd = acceptClient(serverSockfd, clientIp, &clientPort);
        if(video_transpond_sockfd < 0)
        {
            printf("failed to accept client\n");
            return;
        }
		printf("video tranpond clientSockfd : %d  accept client;client ip:%s,client port:%d\n", video_transpond_sockfd, clientIp, clientPort);

		b_restart_listen_video_transpond_client_flag = 1;
	}
}

static pthread_t video_transpond_thread_id;
static pthread_t listen_client_connect_thread_id;
int video_transpond(void)
{
	#if 0
	if(0 != pthread_create(&video_transpond_thread_id, NULL, video_transpond_thread , NULL)){
		perror("video_transpond_thread create error");
		return -1;
	}
	#endif
	
	if(0 != pthread_create(&listen_client_connect_thread_id, NULL, video_transpond_listen_client_thread, NULL)){
		printf("video_transpond_listen_client_thread create fail: %s\n", strerror(errno));
		return -1;
	}
	if(0 != pthread_create(&recv_from_usb_id, NULL,recv_from_usb_thread, NULL)){
		printf("thread recv_from_usb_thread create is error\n");
		return;
	}
	if(0 != pthread_create(&send_to_terminal_id, NULL, send_to_terminal_thread, (void *)(&clientSockfd))){
		printf("thread send_to_terminal_thread create is error\n");
		return; 		
	}
	

	return 0;
}

int video_transpond_deinit(void)
{
	pthread_join(video_transpond_thread_id, NULL);
	return 0;
}

