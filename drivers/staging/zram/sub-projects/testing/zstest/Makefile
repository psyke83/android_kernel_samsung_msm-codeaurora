KERNELDIR ?= "/lib/modules/$(shell uname -r)/build"

EXTRA_CFLAGS	:=	-g -Wall

obj-m		+=	zstest.o

all:
	make -C $(KERNELDIR) M=$(PWD) modules

clean:
	make -C $(KERNELDIR) M=$(PWD) clean
