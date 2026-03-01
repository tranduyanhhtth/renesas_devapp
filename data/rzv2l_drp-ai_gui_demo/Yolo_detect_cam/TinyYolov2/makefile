################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include src/subdir.mk
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C++_DEPS)),)
-include $(C++_DEPS)
endif
ifneq ($(strip $(CC_DEPS)),)
-include $(CC_DEPS)
endif
ifneq ($(strip $(CPP_DEPS)),)
-include $(CPP_DEPS)
endif
ifneq ($(strip $(CXX_DEPS)),)
-include $(CXX_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
ifneq ($(strip $(C_UPPER_DEPS)),)
-include $(C_UPPER_DEPS)
endif
endif

-include ../makefile.defs

OPTIONAL_TOOL_DEPS := \
$(wildcard ../makefile.defs) \
$(wildcard ../makefile.init) \
$(wildcard ../makefile.targets) \


BUILD_ARTIFACT_NAME := sample_app_tinyyolov2_cam
BUILD_ARTIFACT_EXTENSION :=
BUILD_ARTIFACT_PREFIX :=
BUILD_ARTIFACT := $(BUILD_ARTIFACT_PREFIX)$(BUILD_ARTIFACT_NAME)$(if $(BUILD_ARTIFACT_EXTENSION),.$(BUILD_ARTIFACT_EXTENSION),)

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all:
	+@$(MAKE) --no-print-directory main-build && $(MAKE) --no-print-directory post-build

# Main-build Target
main-build: sample_app_tinyyolov2_cam

# Tool invocations
sample_app_tinyyolov2_cam: $(OBJS) $(USER_OBJS) makefile $(OPTIONAL_TOOL_DEPS)
	@echo 'Building target: $@'
	@echo 'Invoking: Cross G++ Linker'
	aarch64-poky-linux-g++ --sysroot=/opt/poky/rzv2l/sysroots/aarch64-poky-linux -o "sample_app_tinyyolov2_cam" $(OBJS) $(USER_OBJS) $(LIBS) -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lopencv_highgui -ljpeg -lwebp -ltiff -lz -ltbb -lgtk-3 -lpng16 -lgdk-3 -lcairo -llzma -lrt -lcairo-gobject -lxkbcommon -lwayland-cursor -lwayland-egl -lwayland-client -lepoxy -lfribidi -lharfbuzz -lfontconfig -lglib-2.0 -lgobject-2.0 -lgdk_pixbuf-2.0 -lgmodule-2.0 -lpangocairo-1.0 -latk-1.0 -lgio-2.0 -lpango-1.0 -lfreetype -lpixman-1 -luuid -lpcre -lmount -lresolv -lexpat -lpangoft2-1.0 -lblkid -lpthread -ldl
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) sample_app_tinyyolov2_cam
	-@echo ' '

post-build:
	-cp sample_app_tinyyolov2_cam ../../exe/ObjectDetection
	-@echo ' '

.PHONY: all clean dependents main-build post-build

-include ../makefile.targets
