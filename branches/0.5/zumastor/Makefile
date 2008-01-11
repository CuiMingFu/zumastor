all: zumastor.8.gz nblock_write etc/zumastor/version

clean:
	rm -rf ./usr nblock_write

etc/zumastor/version:
	@echo zumastor revision `cat SVNREV || svnversion .` built on `date` by `whoami`@`hostname` > $@

zumastor.8.gz: ./man/zumastor.8
	gzip -c ./man/zumastor.8 > zumastor.8.gz
	mkdir -p ./usr/share/man/man8
	mv zumastor.8.gz ./usr/share/man/man8
nblock_write: nblock_write.c
	gcc nblock_write.c -o nblock_write
install:
	./scripts/install
	cp nblock_write ${DESTDIR}/lib/zumastor
