#include "fc_transpond.h"
#include "libar8020.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>


#define TCP_FC_TRANSPARENT_PORT (6623)
#define SINGLE_FC_TRANSPARENT_PORT (6633)



typedef struct{
	int new_client_socket;
	PORT fc_data_usb_port;
	struct sockaddr_in new_client_addr;
}st_socket_info;

st_socket_info st_new_client_info;

static pthread_t recv_fcdata_from_uav_id;
static pthread_t send_fcdata_to_uav_id;
static pthread_t listen_client_connect_pthread_id;

pthread_mutex_t socket_mutex;
pthread_mutex_t usb_port_mutex;

static char test_buf[100];
static int test_len;

static struct timeval timeout;




volatile int b_restart_listen_client_flag;

#define TCP_WRITE_TO_APP (1024)

static char read_from_usb_buf[1024];


static pthread_t single_fc_transparent_listen_thread_id;
static pthread_t single_fc_transparent_thread_id;
static pthread_t single_fc_recv_fcdata_from_uav_id;
static pthread_t single_fc_send_fcdata_to_uav_id;


volatile int b_restart_listen_single_fc_client_flag;
volatile int b_single_fc_send_flags;
int client_single_fc_socket;
volatile int read_usb_ret;




static int send_bypass_cmd_to_uav(PORT usb_fd)
{
	char sbuf[10]={0};
	int cmdid = 0x87;
	int ret;
	
	sbuf[0] = 0xff; 				//head
	sbuf[1] = 0x5a; 				//head
	sbuf[2] = (char)cmdid;			//cmdid H
	sbuf[3] = (char)(cmdid >> 8);	//cmdid L
	sbuf[4] = 1;					//reserve
	sbuf[5] = 0;					//reserve
	sbuf[6] = 0x00; 				//msg_len
	sbuf[7] = 0x00; 				//msg_len
	sbuf[8] = 0x00; 				//checksum
	sbuf[9] = 0;					//checksum

	ret = Cmd_Bypass_Send(usb_fd,sbuf,10);
	if(ret < 0){
		printf("cmd bypass send error\n");
		return -1;
	}

	return ret;
}

static unsigned int data_check(char *buffer,int len)
{
    int i;
    unsigned int sum = 0;
    for(i = 0;i < len;i ++)
        sum += (unsigned char)buffer[i];
    
    return sum;
}


static void*recv_fcdata_from_uav_thread(void *arg)
{
	fd_set socketwrite;

	char buf[TCP_WRITE_TO_APP] = {0};
	int ret = 0, w_ret = 0, i, sel_ret = 0;
	int clientSockfd;



	timeout.tv_sec = 0;
	timeout.tv_usec = 1000;

	
	while(1)
	{
		while(!b_restart_listen_client_flag)
		{
			usleep(1000000);
		}
		#if 0
		pthread_mutex_lock(&usb_port_mutex);
		send_bypass_cmd_to_uav(st_new_client_info.fc_data_usb_port);
		usleep(20000);
		read_usb_ret = Cmd_Bypass_Rec(st_new_client_info.fc_data_usb_port,buf, TCP_WRITE_TO_APP);
		pthread_mutex_unlock(&usb_port_mutex);
		#endif
		//send(recv_from_uav.client_fd, test_buf, test_len, 0);
		//test_len = 0;
		if(0 < read_usb_ret){
			#if 0
			for(i = 0; i < read_usb_ret; i++){
				printf("%x ", buf[i]);
			}
			printf("--ret:%d\n", ret);
			#endif
			//printf("%s [%d] \n", __func__, __LINE__);
			FD_ZERO(&socketwrite);
			//printf("%s [%d] \n", __func__, __LINE__);
			FD_SET(st_new_client_info.new_client_socket, &socketwrite);
			//printf("%s [%d] \n", __func__, __LINE__);
			sel_ret = select(st_new_client_info.new_client_socket + 1, NULL, &socketwrite, NULL, &timeout);
			//printf("%s [%d] ret = %d\n", __func__, __LINE__, sel_ret);
			//printf("recv_fcdata_from_uav_thread select ret = %d\n", ret);
			//if(0 > select(st_new_client_info.new_client_socket + 1, NULL, &socketwrite, NULL, &timeout)){
			if(sel_ret > 0){
				if(FD_ISSET(st_new_client_info.new_client_socket, &socketwrite) > 0){
					do{
						//printf("%s [%d] ret=%d, socket=%d\n", __func__, __LINE__, ret, st_new_client_info.new_client_socket);
						w_ret = send(st_new_client_info.new_client_socket, read_from_usb_buf, read_usb_ret, MSG_NOSIGNAL);
						//printf("%s [%d] w_ret= %d\n", __func__, __LINE__, w_ret);
						if(w_ret > 0)
							ret = ret - w_ret;
						else if(w_ret < 0){
							perror("transparent video fc send error: ");
							//pthread_exit(NULL);
							close(st_new_client_info.new_client_socket);
							b_restart_listen_client_flag = 0;
							break;
						}
					}while(ret > 0);
					
				}else{
					printf("%s [%d] \n", __func__, __LINE__);
				}
			}
			else{
				perror("recv_fcdata_from_uav_thread select:");
				pthread_exit(NULL);
			}
		}else if(0 == read_usb_ret){
			usleep(100000);
			printf("read data frm usb is 0\n");
			continue;
		}else{
			printf("read data from usb is error\n");
			//perror("Cmd_Bypass_Rec read");
		}

		usleep(100000);
	}
}
#define TCP_READ_FROM_APP (500)
static void*send_fcdata_to_uav_thread(void *arg)
{
	fd_set socketread;
	char buf[TCP_READ_FROM_APP] = {0};
	char sbuf[TCP_READ_FROM_APP] = {0};
	int ret = 0, i;


	timeout.tv_sec = 0;
	timeout.tv_usec = 1000;

	unsigned int sum_check;
	int cmdid = 0x86;
	sbuf[0] = 0xff; 				//head
	sbuf[1] = 0x5a; 				//head
	sbuf[2] = (char)cmdid;			//cmdid H
	sbuf[3] = (char)(cmdid >> 8);	//cmdid L
	sbuf[4] = 0;					//reserve
	sbuf[5] = 0;					//reserve
	
	while(1)
	{
		while(!b_restart_listen_client_flag)
		{
			usleep(1000000);
		}
		//printf("%s [%d] \n", __func__, __LINE__);
		FD_ZERO(&socketread);
		//printf("%s [%d] \n", __func__, __LINE__);
		FD_SET(st_new_client_info.new_client_socket, &socketread);
		//printf("%s [%d] \n", __func__, __LINE__);
		ret = select(st_new_client_info.new_client_socket + 1, &socketread, NULL, NULL, &timeout);
		//printf("%s [%d] \n", __func__, __LINE__);
		if(ret < 0){
			perror("select error:");
			usleep(10000);
			continue;
		}
		//printf("send_fcdata_to_uav_thread select ret = %d\n", ret);
		if(FD_ISSET(st_new_client_info.new_client_socket, &socketread) >0){
			//test_len = recv(send_to_uav.client_fd, test_buf, 100, 0);
			ret = recv(st_new_client_info.new_client_socket, buf, TCP_READ_FROM_APP, 0);
			if(ret < 0){
				perror("recv error:");
				pthread_exit(NULL);
			}else if(0 == ret){
				usleep(100000);
				continue;
			}
			#if 1
			for(i = 0; i < ret; i++){
				printf("%x ", buf[i]);
			}
			printf("--ret:%d\n", ret);
			#endif
			sum_check = data_check(buf,ret);
			sbuf[6] = ret; 	//msg_len
			sbuf[7] = 0x00; 				//msg_len
			sbuf[8] = sum_check;			//checksum
			sbuf[9] = sum_check >> 8;		//checksum
			memcpy(&sbuf[10], buf, ret);
			
			pthread_mutex_lock(&usb_port_mutex);
			ret = Cmd_Bypass_Send(st_new_client_info.fc_data_usb_port, sbuf, ret + 10);
			#if 1
			printf("send--ret:%d\n", ret);
			#endif			
			pthread_mutex_unlock(&usb_port_mutex);
		}
		usleep(1000000);
	}
}

static int transparent_thread_create(void)
{

	if(0 > pthread_create(&recv_fcdata_from_uav_id , NULL, recv_fcdata_from_uav_thread,NULL)){
		perror("pthread_create recv_fcdata_from_uav_thread error：");
		return -1;
	}
#if 1
	if(0 > pthread_create(&send_fcdata_to_uav_id, NULL, send_fcdata_to_uav_thread, NULL)){
		perror("pthread_create send_fcdata_to_uav_thread error:");
		return -1;
	}
#endif

return 0;
}

static void*listen_client_connect_pthread(void *arg)
{
	int socket_fd = 0, new_client_fd, *p;
	//int ret = 0;
	//struct sockaddr_in client_addr;

	#if 0
	socket_fd = *((int*)(arg));
	printf("socket_fd=%d\n", socket_fd);
	#endif
	int sockfd = 0, client_fd = 0;
	int on = 1, ret = 0;
	struct sockaddr_in addr, client_addr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		perror("socket create error:");
		pthread_exit(NULL);
	}

	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(TCP_FC_TRANSPARENT_PORT);
	addr.sin_addr.s_addr = inet_addr("0.0.0.0");

	if(bind(sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr)) < 0){
		perror("sock bind:");
		close(sockfd);
		pthread_exit(NULL);
	}
	while(1)
	{
		while(b_restart_listen_client_flag)
		{
			usleep(1000000);
		}
		
		printf("video_fc_thread restart listen client connect\n");
	    ret = listen(sockfd, 10);
	    if(ret < 0)
	    {
	        perror("failed to listen ");
			close(sockfd);
	        pthread_exit(NULL);
	    }


		memset(&client_addr, 0, sizeof(client_addr));
		int len = sizeof(client_addr);
		new_client_fd = accept(sockfd, (struct sockaddr *)(&client_addr), &len);
		if(new_client_fd < 0){
			perror("accept error:");
			close(sockfd);
			pthread_exit(NULL);
		}
		st_new_client_info.new_client_socket = new_client_fd;
		memcpy(&(st_new_client_info.new_client_addr), &client_addr, sizeof(client_addr));
		b_restart_listen_client_flag = 1;
		printf("accept video_fc new_client:client_fd:%d client ip : %s, client port: %d \n", new_client_fd, inet_ntoa(client_addr.sin_addr), client_addr.sin_port);
	}

	
}

static int transparent_socket_thread_create(void)
{
#if 0
	int sockfd = 0, client_fd = 0;
	int on = 1, ret = 0;
	struct sockaddr_in addr, client_addr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd < 0){
		perror("socket create error:");
		return -1;
	}

	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(TCP_FC_TRANSPARENT_PORT);
	addr.sin_addr.s_addr = inet_addr("0.0.0.0");

	if(bind(sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr)) < 0){
		perror("sock bind:");
		close(sockfd);
		return -1;
	}


	printf("sockfd1= %d\n", sockfd);
#endif
	if(0 > pthread_create(&listen_client_connect_pthread_id, NULL, listen_client_connect_pthread, NULL)){
		perror("pthread listen_client_connect_pthread error");
		//close(sockfd);
		return -1;
	}


	return 0;
}





static void *single_fc_transparent_listen_thread(void *arg)
{
	int single_fc_socket;

	single_fc_socket = socket(AF_INET, SOCK_STREAM, 0);
	if(0 > single_fc_socket){
		perror("socket");
		pthread_exit(NULL);
	}


	int on = 1;
	setsockopt(single_fc_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&on, sizeof(on));

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SINGLE_FC_TRANSPARENT_PORT);
	addr.sin_addr.s_addr = inet_addr("0.0.0.0");

	if(bind(single_fc_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0){
		perror("bind");
		pthread_exit(NULL);
	}
	
	while(1)
	{
		while(b_restart_listen_single_fc_client_flag)
		{
			usleep(1000*1000);
		}
		if(listen(single_fc_socket, 5) < 0){
			perror("listen error");
			pthread_exit(NULL);
		}

		struct sockaddr_in single_fc_addr;
		memset(&single_fc_addr, 0, sizeof(single_fc_addr));
		int len = sizeof(single_fc_addr);
		client_single_fc_socket = accept(single_fc_socket, (struct sockaddr*)&single_fc_addr, &len);
		if(client_single_fc_socket < 0){
			perror("accept");
			close(single_fc_socket);
		}
	
		printf("accept new_single_fc_client:single_fc_socket:%d single_fc ip : %s, single_fc port: %d \n", \
			client_single_fc_socket, inet_ntoa(single_fc_addr.sin_addr), single_fc_addr.sin_port);
		b_restart_listen_single_fc_client_flag = 1;
	}
}

static void*single_fc_recv_fcdata_from_uav_thread(void *arg)
{
	fd_set socketwrite;

	char buf[TCP_WRITE_TO_APP] = {0};
	int ret = 0, w_ret = 0, i, sel_ret = 0;
	int clientSockfd;



	timeout.tv_sec = 0;
	timeout.tv_usec = 1000;

	
	while(1)
	{
		while(!b_restart_listen_single_fc_client_flag)
		{
			usleep(1000000);
		}
		if(read_usb_ret > 0){
			//send(recv_from_uav.client_fd, test_buf, test_len, 0);
			//test_len = 0;
			#if 0
			for(i = 0; i < ret; i++){
				printf("%x ", buf[i]);
			}
			printf("--ret:%d\n", ret);
			#endif
			FD_ZERO(&socketwrite);
			FD_SET(client_single_fc_socket, &socketwrite);
			sel_ret = select(client_single_fc_socket + 1, NULL, &socketwrite, NULL, &timeout);

			if(sel_ret > 0){
				if(FD_ISSET(client_single_fc_socket, &socketwrite) > 0){			
					do{
						//printf("%s [%d] ret=%d, socket=%d\n", __func__, __LINE__, ret, st_new_client_info.new_client_socket);
						w_ret = send(client_single_fc_socket, read_from_usb_buf, read_usb_ret, MSG_NOSIGNAL);
						//printf("%s [%d] w_ret= %d\n", __func__, __LINE__, w_ret);
						if(w_ret > 0)
							ret = ret - w_ret;
						else if(w_ret < 0){
							perror("single_fc send error: ");
							//pthread_exit(NULL);
							b_restart_listen_single_fc_client_flag = 0;
							break;
						}
					}while(ret > 0);		
				}else{
					printf("%s [%d] \n", __func__, __LINE__);
				}
			}
			else{
				perror("single_fc_recv_fcdata_from_uav_thread select:");
				pthread_exit(NULL);
			
			}
		}
		usleep(100000);
	}
}

static void*single_fc_send_fcdata_to_uav_thread(void *arg)
{
	fd_set socketread;
	char buf[TCP_READ_FROM_APP] = {0};
	char sbuf[TCP_READ_FROM_APP] = {0};
	int ret = 0, i;


	timeout.tv_sec = 0;
	timeout.tv_usec = 1000;

	unsigned int sum_check;
	int cmdid = 0x86;
	sbuf[0] = 0xff; 				//head
	sbuf[1] = 0x5a; 				//head
	sbuf[2] = (char)cmdid;			//cmdid H
	sbuf[3] = (char)(cmdid >> 8);	//cmdid L
	sbuf[4] = 0;					//reserve
	sbuf[5] = 0;					//reserve
	
	while(1)
	{
		while(!b_restart_listen_single_fc_client_flag)
		{
			usleep(1000000);
		}
		//printf("%s [%d] \n", __func__, __LINE__);
		FD_ZERO(&socketread);
		//printf("%s [%d] \n", __func__, __LINE__);
		FD_SET(client_single_fc_socket, &socketread);
		//printf("%s [%d] \n", __func__, __LINE__);
		ret = select(client_single_fc_socket + 1, &socketread, NULL, NULL, &timeout);
		//printf("%s [%d] \n", __func__, __LINE__);
		if(ret < 0){
			perror("select error:");
			usleep(10000);
			continue;
		}
		//printf("send_fcdata_to_uav_thread select ret = %d\n", ret);
		if(FD_ISSET(client_single_fc_socket, &socketread) >0){
			//test_len = recv(send_to_uav.client_fd, test_buf, 100, 0);
			ret = recv(client_single_fc_socket, buf, TCP_READ_FROM_APP, 0);
			if(ret < 0){
				perror("recv error:");
				pthread_exit(NULL);
			}else if(0 == ret){
				usleep(100000);
				continue;
			}
			#if 1
			for(i = 0; i < ret; i++){
				printf("%x ", buf[i]);
			}
			printf("--ret:%d\n", ret);
			#endif
			sum_check = data_check(buf,ret);
			sbuf[6] = ret; 	//msg_len
			sbuf[7] = 0x00; 				//msg_len
			sbuf[8] = sum_check;			//checksum
			sbuf[9] = sum_check >> 8;		//checksum
			memcpy(&sbuf[10], buf, ret);
			
			pthread_mutex_lock(&usb_port_mutex);
			ret = Cmd_Bypass_Send(st_new_client_info.fc_data_usb_port, sbuf, ret + 10);
			#if 1
			printf("send--ret:%d\n", ret);
			#endif			
			pthread_mutex_unlock(&usb_port_mutex);
		}
		usleep(1000000);
	}
}


static int single_fc_transparent_thread_create(void)
{
	#if 1
	if(0 != pthread_create(&single_fc_transparent_listen_thread_id, NULL, single_fc_transparent_listen_thread, NULL)){
		perror("single_fc_transparent_listen_thread create error");
		return -1;
	}
	#endif
	if(0 > pthread_create(&single_fc_recv_fcdata_from_uav_id , NULL, single_fc_recv_fcdata_from_uav_thread,NULL)){
		perror("pthread_create recv_fcdata_from_uav_thread error：");
		return -1;
	}
	
	if(0 > pthread_create(&single_fc_send_fcdata_to_uav_id, NULL, single_fc_send_fcdata_to_uav_thread, NULL)){
		perror("pthread_create send_fcdata_to_uav_thread error:");
		return -1;
	}

	
	return 0;
}

static pthread_t read_fc_data_from_usb_thread_id;
static void *read_fc_data_from_usb_thread(void *arg)
{
	while(1)
	{
		pthread_mutex_lock(&usb_port_mutex);
		send_bypass_cmd_to_uav(st_new_client_info.fc_data_usb_port);
		usleep(20000);
		read_usb_ret = Cmd_Bypass_Rec(st_new_client_info.fc_data_usb_port, read_from_usb_buf, TCP_WRITE_TO_APP);
		pthread_mutex_unlock(&usb_port_mutex);
		usleep(20000);
	}
}

static int read_fc_data_from_usb_thread_create(void)
{
	if(0 != pthread_create(&read_fc_data_from_usb_thread_id, NULL, read_fc_data_from_usb_thread, NULL)){
		perror("read_fc_data_from_usb_thread error");
		return -1;
	}
	
	return 0;
}

int fc_transparent(void)
{


	#if 1
	if(0 != Cmd_Port_Open(&st_new_client_info.fc_data_usb_port,NULL)){
		printf("C201-D Cmd_Port_Open error\n");
		return -1;
	}
	
	#else
	int fd = open("/dev/artosyn_port0", O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0){
		perror("open file artosyn_port0 error:");
		return -1;
	}
	char buf[512] = {0};
	int ret = 0, i;
	char sbuf[10]={0};
	int cmdid = 0x87;
	//int ret;
	#if 0
	sbuf[0] = 0xff; 				//head
	sbuf[1] = 0x5a; 				//head
	sbuf[2] = (char)cmdid;			//cmdid H
	sbuf[3] = (char)(cmdid >> 8);	//cmdid L
	sbuf[4] = 1;					//reserve
	sbuf[5] = 0;					//reserve
	sbuf[6] = 0x00; 				//msg_len
	sbuf[7] = 0x00; 				//msg_len
	sbuf[8] = 0x00; 				//checksum
	sbuf[9] = 0;					//checksum
	#else
	unsigned int sum_check;
	cmdid = 0x86;
	sbuf[0] = 0xff; 				//head
	sbuf[1] = 0x5a; 				//head
	sbuf[2] = (char)cmdid;			//cmdid H
	sbuf[3] = (char)(cmdid >> 8);	//cmdid L
	sbuf[4] = 0;					//reserve
	sbuf[5] = 0;					//reserve
	
	
	#define DEFAULT_SEND_LEN (10)
	for(i=0;i<DEFAULT_SEND_LEN;i++)
	sbuf[i+10] = i;
	sum_check = data_check(sbuf+10,DEFAULT_SEND_LEN);
	sbuf[6] = DEFAULT_SEND_LEN; 	//msg_len
	sbuf[7] = 0x00; 				//msg_len
	sbuf[8] = sum_check;			//checksum
	sbuf[9] = sum_check >> 8;		//checksum
	#endif

	while(1)
	{
		#if 0
		ret = write(fd, sbuf, 10);
		if(ret < 0){
			perror("write artosyn_port0 error");
			return -1;
		}
		printf("send cmd is %d\n", ret);
		usleep(1000000);
		
		ret = read(fd, buf, 512);
		if(ret > 0){
			for(i=0; i < ret; i++){
				printf("%x ", buf[i]);
			}
			printf("end flag\n");
		}else if(ret == 0){
			printf("read artosyn_port0 data is 0\n");
		}else{
			perror("read artosyn_port0 data less 0:");
		}
		usleep(1000000);
		#endif

        
        //send to mode
        ret = write(fd,sbuf,10+DEFAULT_SEND_LEN);
        if(ret < 0)
        {
            printf("send failed\n");
        }
        else
        {
            printf("send ok\n");
        }
        usleep(2000000);		
	}
	#endif

	if(pthread_mutex_init(&usb_port_mutex, NULL) != 0){
		perror("pthread_mutex_init usb_port_mutex error");
		goto cmd_port_close;
	}

	if(read_fc_data_from_usb_thread_create() != 0){
		printf("socket create error between app and raspberry\n");
		goto destory_usb_mutex;
	}
	
	if(transparent_socket_thread_create() != 0){
		printf("socket create error between app and raspberry\n");
		goto destory_usb_mutex;
	}
	
	if(0 != transparent_thread_create()){
		printf("transparent thread create is error\n");
		goto destory_usb_mutex;
	}

	if(0 != single_fc_transparent_thread_create()){
		printf("single_fc_transparent_thread_create error\n");
		return -1;
	}

	return 0;
	
destory_usb_mutex:
	pthread_mutex_destroy(&usb_port_mutex);

cmd_port_close:
	Cmd_Port_Close(st_new_client_info.fc_data_usb_port);	

	return -1;

}

int fc_transparent_deinit(void)
{
	
	pthread_mutex_destroy(&usb_port_mutex);
	Cmd_Port_Close(st_new_client_info.fc_data_usb_port);
}


int fc_transparent_pthread_join(void)
{
	pthread_join(listen_client_connect_pthread_id, NULL);
	pthread_join(recv_fcdata_from_uav_id, NULL);
	pthread_join(send_fcdata_to_uav_id, NULL);
	pthread_join(single_fc_transparent_listen_thread_id, NULL);
}

