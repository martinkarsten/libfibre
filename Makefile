help:
	@echo "USAGE: (see src/Makefile for additional targets)"
	@echo "$(MAKE) all      build library + test program"
	@echo "$(MAKE) lib      build library"
	@echo "$(MAKE) clean    clean everything"
	@echo "$(MAKE) dep      build dependencies"

ifeq ($(shell uname -s),FreeBSD)
NPROC=$(shell sysctl kern.smp.cpus|cut -c16- || echo 1)
else
NPROC=$(shell nproc || echo 1)
endif

.DEFAULT:
	nice -10 $(MAKE) -C src -j $(NPROC) $@
	nice -10 $(MAKE) -C apps -j $(NPROC) $@

lib:
	nice -10 $(MAKE) -C src -j $(NPROC) all

extra: all
	nice -10 $(MAKE) -C apps -j $(NPROC) $@

-include Makefile.local # development/testing targets, not for release
