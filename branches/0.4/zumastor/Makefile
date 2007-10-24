all: zumastor.8.gz nblock_write

clean:
	rm -rf ./usr nblock_write

zumastor.8.gz: ./man/zumastor.8
	gzip -c ./man/zumastor.8 > zumastor.8.gz
	mkdir -p ./usr/share/man/man8
	mv zumastor.8.gz ./usr/share/man/man8
nblock_write: nblock_write.c
	gcc nblock_write.c -o nblock_write
install:
	./scripts/install
	cp nblock_write ${DESTDIR}/lib/zumastor
