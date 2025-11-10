ifeq ($(SKIP_WISDOM),1)
all:
	$(MAKE) -C Ricochet
else
all: Shared_files/harmonizer.wisdom
	$(MAKE) -C Ricochet
endif

install: all
	$(MAKE) -C Ricochet install

clean:
	$(MAKE) -C Ricochet clean
	rm -f Shared_files/*.o
	rm -f Shared_files/harmonizer.wisdom

Shared_files/harmonizer.wisdom:
	@echo "Generating harmonizer.wisdom file, this might take a while..."
	fftwf-wisdom -v -n -x -o $@ \
	rof128 rob128 rof192 rob192 rof256 rob256 rof384 rob384 rof512 rob512 rof640 rob640 rof768 rob768 \
	rof1024 rob1024 rof1280 rob1280 rof1536 rob1536 rof2048 rob2048 rof2176 rob2176 rof2304 rob2304 \
	rof2432 rob2432 rof2560 rob2560 rof3072 rob3072 rof4096 rob4096
