help:
	@echo "USAGE:"
	@echo "$(MAKE) all      build library + test programs"
	@echo "$(MAKE) lib      build library"
	@echo "$(MAKE) apps     build test programs"
	@echo "$(MAKE) clean    clean everything"

ifeq ($(shell uname -s),FreeBSD)
NPROC=$(shell sysctl kern.smp.cpus|cut -c16- || echo 1)
else
NPROC=$(shell nproc || echo 1)
endif
ifeq ($(strip $(MAKEFLAGS)),)
MAKEFLAGS=-j $(NPROC)
endif

.PHONY: lib apps extra

.DEFAULT:
	+nice -10 $(MAKE) -C src $@
	+nice -10 $(MAKE) -C apps $@

lib:
	+nice -10 $(MAKE) -C src all

apps:
	+nice -10 $(MAKE) -C apps all

extra: all
	+nice -10 $(MAKE) -C apps $@

clean: cleandoc
	nice -10 $(MAKE) -C src clean
	nice -10 $(MAKE) -C apps clean

doc:
	doxygen Doxyfile

cleandoc:
	rm -rf html latex

-include Makefile.local # development/testing targets, not for release
