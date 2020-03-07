#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>


#define min(x, y) ((x) < (y) ? (x) : (y)) 

#include "ringfifo.h"

//#include "AssistantDef.h"
//#include "AssistantCmd.h"
#include <assert.h>

unsigned int write_point;
unsigned int read_point;
unsigned int buffer_length;
pthread_mutex_t lock;
char *buffer;
void LockBuf();
void unLockBuf();


int RingBufferInit(unsigned      int size)
{

    buffer_length = size;  
   	write_point = read_point = 0;  
    pthread_mutex_init(&lock, NULL); 
	/*malloc this buffer*/
    buffer = (char *) malloc(size);  
    if (!buffer){
        free(buffer);
		printf("malloc this buffer error!\n");
		assert(-1);
    	}
    else{
        memset(buffer, 0, size); 
		printf("malloc this size:%dK success!\n",size/1024);
    	}
	
}
int RingBufferDeinit(){

	if(buffer) free(buffer);
}

unsigned int RingBufferGet(char *DestBuf,unsigned int len)
{
	unsigned int l; 

	LockBuf();
	/*calaulate the read buffer length*/
	len = min(len, write_point - read_point);  
	l = min(len, buffer_length - (read_point & (buffer_length - 1)));	
	/*copy data to dest buffer*/
	memcpy(DestBuf, buffer + (read_point & (buffer_length - 1)), l);  
	memcpy(DestBuf + l, buffer, len - l);  
	read_point += len; 
	unLockBuf();

	return len; 

}
unsigned int RingBufferPut(char *SourBuf,unsigned int len)
{
    unsigned int l; 

	LockBuf();
	/*calaulate the write buffer length*/
    len = min(len, buffer_length - write_point + read_point);  
    l = min(len, buffer_length - (write_point & (buffer_length - 1))); 
	/*copy data form source buffer*/
    memcpy(buffer + (write_point & (buffer_length - 1)), SourBuf, l);  
    memcpy(buffer, SourBuf + l, len - l);  
    write_point += len;  
	unLockBuf();
	
    return len; 
}

void LockBuf()
{
	pthread_mutex_lock(&lock);
}

void unLockBuf()
{
	pthread_mutex_unlock(&lock);
}


unsigned int GetWritePoint()
{

	return write_point;
}

unsigned int GetReadPoint()
{

	return read_point;
}


/*Read file function*/
/*read cfg or parmeter file to buf*/
char* ReadFileToBuf(char *name)
{
	FILE *fd;
	int ret,filelen;
	char *content;

	/*file info*/
    fd = fopen(name, "r");
    if(fd == NULL)
    {
        printf("ERROR: open %s file error.\n", name);
        goto ERROR;
    }
	/*set the poisont as end and get file length*/
	ret = fseek(fd,0,SEEK_END);
	if(ret)
	{
        printf("ERROR: set file postion at end error: %s file.\n", name);
        goto ERROR;

	}
	/*get file content*/
	filelen = ftell(fd) + 1;
	content = (char *)calloc(1, filelen * sizeof(char));
    if(content == NULL)
    {   
        printf("ERROR: calloc %d bytes for cfg error.\n", filelen);
        goto ERROR;
    } 
	/*read file*/
	fseek(fd, 0, SEEK_SET);
	ret = fread(content,1,filelen,fd);
	if(ret != filelen -1){
		printf("ERROR: read file:%s %d bytes error,but file length is %d.\n",name,ret,filelen);
        goto ERROR;

	}
	/*close file*/
	if(fd) fclose(fd);

	return content;

ERROR:
	if(content) free(content);
	if(fd) fclose(fd);
	return NULL;

}


