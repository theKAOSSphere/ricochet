######################################
# Buildroot package fragment for Ricochet LV2 plugin
# Drop this in your Buildroot package directory and adapt as needed.
######################################

# local source
# Use the package basename as the variable prefix so Buildroot's
# generic-package picks them up correctly. The package file is
# `ricochet.mk` so the expected prefix is RICOCHET.
RICOCHET_SITE_METHOD = local
RICOCHET_SITE = $($(PKG)_PKGDIR)/

# version (bump to force rebuild if needed)
RICOCHET_VERSION = 1

# dependencies
RICOCHET_DEPENDENCIES = armadillo fftw-single
ifneq ($(BR2_arm)$(BR2_aarch64)$(BR2_x86_64),y)
RICOCHET_DEPENDENCIES += host-fftw-single
endif

# bundles produced
RICOCHET_BUNDLES = ricochet.lv2

# toolchain PATH trimmed to avoid inheriting Windows host entries with spaces
RICOCHET_TOOLCHAIN_PATH = $(HOST_DIR)/bin:$(HOST_DIR)/sbin:$(HOST_DIR)/usr/bin:$(HOST_DIR)/usr/sbin:/usr/bin:/bin

# call make in the package directory (ricochet top-level)
RICOCHET_TARGET_MAKE = PATH="$(RICOCHET_TOOLCHAIN_PATH)" \
	CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" AR="$(TARGET_AR)" LD="$(TARGET_LD)" \
	PKG_CONFIG="$(TARGET_PKG_CONFIG)" \
	CFLAGS="$(TARGET_CFLAGS)" CXXFLAGS="$(TARGET_CXXFLAGS)" \
	LDFLAGS="$(TARGET_LDFLAGS)" \
	$(MAKE) NOOPT=true -C $(@D)

# wisdom selection by target architecture
ifdef BR2_cortex_a35
RICOCHET_WISDOM_FILE = harmonizer.wisdom.dwarf
else ifdef BR2_cortex_a53
RICOCHET_WISDOM_FILE = harmonizer.wisdom.duox
else ifdef BR2_arm
RICOCHET_WISDOM_FILE = harmonizer.wisdom.duo
else ifdef BR2_x86_64
RICOCHET_WISDOM_FILE = harmonizer.wisdom.x86_64
else ifdef BR2_cortex_a72
RICOCHET_WISDOM_FILE = harmonizer.wisdom.raspberrypi4
endif

ifeq ($(BR2_PAWPAW),y)
define RICOCHET_PREBUILD_STEP
	touch $(@D)/Shared_files/harmonizer.wisdom
endef
else ifeq ($(BR2_arm)$(BR2_aarch64)$(BR2_x86_64),y)
define RICOCHET_PREBUILD_STEP
	cp $(@D)/Shared_files/$(RICOCHET_WISDOM_FILE) $(@D)/Shared_files/harmonizer.wisdom
endef
endif

define RICOCHET_BUILD_CMDS
	$(RICOCHET_PREBUILD_STEP)
	$(RICOCHET_TARGET_MAKE)
endef

# install (pass DESTDIR to plugin install)
define RICOCHET_INSTALL_TARGET_CMDS
	$(RICOCHET_TARGET_MAKE) install DESTDIR=$(TARGET_DIR)
endef

# Extra information: ricochet requires fftw3f (and fftwf-wisdom at build time)
# and libarmadillo. Ensure buildroot has these dev packages available.

# Import generic-package rules
$(eval $(generic-package))
