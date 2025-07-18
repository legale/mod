SUBDIRS := htable list minheap netlink_getlink nlmon syslog2 timeutil uevent leak_detector_c udeque hashtable-linux-kernel
COV_DIRS := htable list minheap netlink_getlink nlmon syslog2 timeutil leak_detector_c udeque hashtable-linux-kernel

.PHONY: test coverage clean

# Run tests in all modules
test:
	@set -e; for d in $(SUBDIRS); do \
	printf "\n==> $$d\n"; \
		if [ "$$d" = "uevent" ]; then \
			$(MAKE) -C $$d check; \
		else \
			$(MAKE) -C $$d test; \
		fi; \
	done

# Run coverage where supported
coverage:
	@fail=""; \
	for d in $(COV_DIRS); do \
	printf "\n==> $$d (coverage)\n"; \
	if ! $(MAKE) -C $$d coverage; then \
	fail="$$fail $$d"; \
	fi; \
	done; \
	if [ -n "$$fail" ]; then \
	echo "Coverage failed in modules:" $$fail; \
	exit 1; \
	fi

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
