.default: default
.PHONY: cmake default debug release clean package install

DEFAULT_BUILD := Build
NEED_CMAKE := $(wildcard Build)
ifeq ("$(NEED_CMAKE)","")
EXTRA_DEP := cmake
else
EXTRA_DEP :=
endif

default: $(EXTRA_DEP)
	$(MAKE) --no-print-directory -C $(DEFAULT_BUILD)

debug: $(EXTRA_DEP)
	$(MAKE) --no-print-directory -C Debug

release: $(EXTRA_DEP)
	$(MAKE) --no-print-directory -C Release

clean:
	@rm -rf Build Debug Release

package: release
	@echo NOT YET FINISHED...

install: release
	@echo NOT YET FINISHED...

cmake:
	@mkdir -p Build
	@rm -f Build/CMakeCache.txt
	@cd Build && cmake -DCMAKE_BUILD_TYPE=Optimize ..
	@cd ..
	@mkdir -p Debug
	@rm -f Debug/CMakeCache.txt
	@cd Debug && cmake -DCMAKE_BUILD_TYPE=Debug ..
	@cd ..
	@mkdir -p Release
	@rm -f Release/CMakeCache.txt
	@cd Release && cmake -DCMAKE_BUILD_TYPE=Release ..
	@cd ..

