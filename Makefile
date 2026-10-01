obj-m += bc250_memory.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all: modules

modules:
	make -C $(KDIR) M=$(PWD) LLVM=1 modules

clean:
	make -C $(KDIR) M=$(PWD) clean

reload: all
	-sudo rmmod bc250_memory 2>/dev/null
	sudo insmod bc250_memory.ko
