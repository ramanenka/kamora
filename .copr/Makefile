top := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
outdir ?= $(top)

.PHONY: srpm
srpm:
	command -v git >/dev/null || dnf -y install git-core
	command -v rpmbuild >/dev/null || dnf -y install rpm-build
	RPM_TOPDIR=$$(mktemp -d) $(top)/packaging/build-rpm.sh srpm "$(outdir)"
