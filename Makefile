#compiler
CC := /home/work/xuyw/src/raspberrypi/tools-master/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian-x64/bin/arm-linux-gnueabihf-gcc
#generate target file
TARGET := single_fc_no3
#source file path  
SUBDIRS := ./
#header file path
INCLUDES := -I ./
INCLUDES += -I ./lib/
INCLUDES += -I ./ringbuffer/




#corresponding target file
C_OBJS := transpond.o
C_OBJS += video_transpond.o
C_OBJS += ./lib/libar8020.o
C_OBJS += ./ringbuffer/ringfifo.o
C_OBJS += ./fc_transpond.o




#.so file
#pthread compile link
LIB += -lpthread

all: $(TARGET)
$(TARGET):$(C_OBJS)
	$(CC) -o $@ $^ $(INCLUDES) $(LIB) $(LDFLAGS)
$(C_OBJS):%o:%c
	$(CC) -o $@ -c $< $(INCLUDES) $(LIB)

clean:
	rm -f ${TARGET} ${C_OBJS}

