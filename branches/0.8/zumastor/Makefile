all: zumastor.8.gz etc/zumastor/version

clean:
	rm -rf ./usr

etc/zumastor/version:
	@echo zumastor `cat VERSION_STRING || echo unknown` built on `date` by `whoami`@`hostname` > $@

zumastor.8.gz: ./man/zumastor.8
	gzip -c --best ./man/zumastor.8 > zumastor.8.gz
	mkdir -p ./usr/share/man/man8
	mv zumastor.8.gz ./usr/share/man/man8
install:
	./scripts/install
