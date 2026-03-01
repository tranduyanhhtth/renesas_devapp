################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
CPP_SRCS += \
../ascii.cpp \
../box.cpp \
../camera.cpp \
../image.cpp \
../mouse.cpp \
../sample_app_yolo_cam.cpp \
../wayland.cpp 

CPP_DEPS += \
./ascii.d \
./box.d \
./camera.d \
./image.d \
./mouse.d \
./sample_app_yolo_cam.d \
./wayland.d 

OBJS += \
./ascii.o \
./box.o \
./camera.o \
./image.o \
./mouse.o \
./sample_app_yolo_cam.o \
./wayland.o 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.cpp subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross G++ Compiler'
	aarch64-poky-linux-g++ -DTINYYOLOV3 -I/opt/poky/rzv2l/sysroots/aarch64-poky-linux/usr/include/opencv4 -O0 -g3 -Wall -c -fmessage-length=0 --sysroot=/opt/poky/rzv2l/sysroots/aarch64-poky-linux -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


clean: clean--2e-

clean--2e-:
	-$(RM) ./ascii.d ./ascii.o ./box.d ./box.o ./camera.d ./camera.o ./image.d ./image.o ./mouse.d ./mouse.o ./sample_app_yolo_cam.d ./sample_app_yolo_cam.o ./wayland.d ./wayland.o

.PHONY: clean--2e-

