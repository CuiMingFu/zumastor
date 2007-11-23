TET_EXECUTE = ../../ts_exec
INSTALL_DIR = $(TET_EXECUTE)/ts/replicate

$(INSTALL_DIR)/replicate: replicate.sh clean
	cp replicate.sh $@
	chmod 755 $@

clean:
	rm -f $(INSTALL_DIR)/replicate
