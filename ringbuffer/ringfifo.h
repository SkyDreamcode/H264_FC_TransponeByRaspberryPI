#ifndef _RINGBUFFER_H_  
#define _RINGBUFFER_H_  



int RingBufferInit(unsigned      int size);
int RingBufferDeinit();
unsigned int RingBufferGet(char *DestBuf,unsigned int len);	
unsigned int RingBufferPut(char *SourBuf,unsigned int len);

unsigned int GetWritePoint();
unsigned int GetReadPoint();

/*Get file content to buffer*/
char* ReadFileToBuf(char *name);



#endif


