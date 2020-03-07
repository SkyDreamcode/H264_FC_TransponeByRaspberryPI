#include "stdio.h"
#include "video_transpond.h"
#include "fc_transpond.h"
int main(int argc, char *argv[])
{
	int ret = 0;

	if(Usb_Init() < 0)
    {
        printf("usb init err\n");        
        return;
    }	
	printf("usb init1\n");

	fc_transparent();

	video_transpond();

	printf("system init end\n");

	while(1){
		usleep(1000*1000);
	}
	printf("the process exit\n");
	
	return 0;
}
