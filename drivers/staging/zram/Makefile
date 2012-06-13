XVM = sub-projects/allocators/xvmalloc-kmod
EXTRA_CFLAGS	:=	-Wall
zram-objs	:=	zram_drv.o zram_sysfs.o $(XVM)/xvmalloc.o

obj-$(CONFIG_ZRAM)	+=	zram.o
