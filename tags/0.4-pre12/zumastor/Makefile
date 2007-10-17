all: zumastor.8.gz

clean:
	rm -rf ./usr

zumastor.8.gz: ./man/zumastor.8
	gzip -c ./man/zumastor.8 > zumastor.8.gz
	mkdir -p ./usr/share/man/man8
	mv zumastor.8.gz ./usr/share/man/man8

install:
	./scripts/install
