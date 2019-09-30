######################################
#
######################################
#source file
#源文件，自动找所有.c和.cpp文件，并将目标定义为同名.o文件
SOURCE  := $(wildcard *.c)
OBJS    := $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCE)))
  
#target you can change test to what you want
#目标文件名，输入任意你想要的执行文件名
TARGET  := test
  
#compile and lib parameter
#编译参数
CC      := arm-none-linux-gnueabi-gcc
LIBS    :=-L./libb
COM_DYLIB		:=./libb/libglib-2.0.so ./libb/libbluetooth.so 
COM_STLIB		:=./libb/libbluetooth.a ./lib/libuu.a ./src/shared/libaat.a
LDFLAGS := $(COM_DYLIB) $(COM_STLIB)
DEFINES :=
INCLUDE := -I./include -I./include/glib-2.0 -I./
UV		:=
CFLAGS  := -g -Wall -lm -lpthread $(DEFINES) $(INCLUDE)
CXXFLAGS:= $(CFLAGS)
  
  
#i think you should do anything here
#下面的基本上不需要做任何改动了
.PHONY : 
	everything objs clean veryclean rebuild
  
everything : $(TARGET)
  
all : $(TARGET)
  
objs : $(OBJS)
  
rebuild: 
	veryclean everything
   
clean:
	rm -fr *.so
	rm -fr *.o
	
veryclean : clean
	rm -fr $(TARGET)
  
$(TARGET) : $(OBJS)
	$(CC) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)