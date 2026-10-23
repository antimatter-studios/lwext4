
#Release
#Debug
BUILD_TYPE = Release

# The version comes from the release tag (cmake/version.cmake)
COMMON_DEFINITIONS =                                      \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE)                      \

define generate_common
	rm -R -f build_$(1)
	mkdir build_$(1)
	cd build_$(1) && cmake -G"Unix Makefiles"           \
	$(COMMON_DEFINITIONS)                               \
	$(2)                                                \
	-DCMAKE_TOOLCHAIN_FILE=../toolchain/$(1).cmake ..
endef

generic:
	$(call generate_common,$@)

cortex-m0:
	$(call generate_common,$@)
	
cortex-m0+:
	$(call generate_common,$@)
	
cortex-m3:
	$(call generate_common,$@)
	
cortex-m4:
	$(call generate_common,$@)
	
cortex-m4f:
	$(call generate_common,$@)
	
cortex-m7:
	$(call generate_common,$@)

arm-sim:
	$(call generate_common,$@)

avrxmega7: 
	$(call generate_common,$@)

msp430:
	$(call generate_common,$@)
	
mingw:
	$(call generate_common,$@,-DWIN32=1)
	
lib_only:
	rm -R -f build_lib_only
	mkdir build_lib_only
	cd build_lib_only && cmake $(COMMON_DEFINITIONS) -DLIB_ONLY=TRUE ..

all: 
	generic

clean:
	rm -R -f build_*
	rm -R -f ext_images

	
include fs_test.mk


	

	
	
