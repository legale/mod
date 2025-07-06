SUBDIRS := htable minheap nlmon syslog2 timeutil uevent
COV_DIRS := htable minheap nlmon syslog2 timeutil

.PHONY: test coverage clean

# Run tests in all modules
test:
	@set -e; for d in $(SUBDIRS); do \
		printf '\n==> $$d\n'; \
		if [ "$$d" = "uevent" ]; then \
			$(MAKE) -C $$d check; \
		else \
			$(MAKE) -C $$d test; \
		fi; \
	done

# Run coverage where supported
coverage:
	@set -e; for d in $(COV_DIRS); do \
		printf '\n==> $$d (coverage)\n'; \
		$(MAKE) -C $$d coverage; \
	done

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done
