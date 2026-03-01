# RZV2L GUI DRP-AI HMI Demo

This project runs the DRP-AI Demos and the DRP-AI Pre-trained Applications. This avoid the need to use a terminal to use a terminal to start the applications. The DRP-AI demos and Applications have been modifed to work with the GUI framework. See below for more information.

![](https://github.com/renesas-rz/rzv2l_drp-ai_gui_demo/blob/master/images/gui_screeshot_object.png)

## Running the applications

### Requirements
- Eclipse IDE is needed to Build the project.
- SDL Needs to be installed during Yocto Build.
  - In the Yocto configuration file "build/conf/local.conf"
  - Add `IMAGE_INSTALL_append = " libsdl2-dev"`
- Install the RZV2L SDK generated from Yocto
- Due to it hub size constraints the following files cannot be uploaded
  - yolov3_cam_weight.dat
  - hrnet_cam_weight.dat

  
### Getting Started
- Step 1) Clone this repository
- Step 2) in a terminal cd to the repository
- Step 3) Setup the SDK i.e. "source /opt/poky/3.1.31/environment-setup-aarch64-poky-linux" NOTE: version 3.1.31 may differ
- Step 4) Start eclipse in the current terminal "eclipse &". NOTE: If you start eclipse outside the terminal in Step 3 SDK will not work.
- Step 5) In the Eclipse Workspace Dialog specify the directory where the repository is located refer to Step 3
- Step 6) Import Project using General -> Existing Projects into Workspace
- Step 7) Right click on each project and select Build.


#### Deployment
Each of the Applications and their DRP-AI files must be layed out as follows on the RZV2L board as follows.
```
├── AIDemos
│   ├── Elderly_fall_detection
│   │   ├── 03_Elderly_fall_detection
│   │   ├── hrnet_cam
│   │   └── tinyyolov2_cam
│   ├── Face_recognition
│   │   ├── Face_recognition
│   │   └── resnet50_cam
│   ├── Head_count_cam
│   │   ├── head_count_cam_app
│   │   └── yolov3_cam
│   └── Line_Crossing
│       ├── Line_crossing_object_counting
│       └── tinyyolov2_cam
├── ObjectDetection
│   ├── coco-labels-2014_2017.txt
│   ├── sample_app_tinyyolov2_cam
│   ├── sample_app_tinyyolov3_cam
│   ├── sample_app_yolov2_cam
│   ├── sample_app_yolov3_cam
│   ├── tinyyolov2_cam
│   ├── tinyyolov3_cam
│   ├── yolov2_cam
│   └── yolov3_cam
├── PoseEstimation
│   └── app_hrnet_pre-tinyyolov2_cam
│       ├── hrnet_cam
│       ├── sample_app_hrnet_pre-tinyyolov2_cam
│       └── tinyyolov2_cam
└── rzv2lDemo
```

## RZV2L HMI Application

The HMI Application ***rzv2lDemo*** demo was developed with the LVGL GUI Software Framework. This framework rquires SDL installed in the Yocto SDK and BSP.  

## DRP-AI Applications

All these demos were modified to respond to Mouse or Touch to close the application. The Camera code is modified to support both MIPI and USB Camera. 

NOTE: DO NOT CONNECT BOTH CAMERAS TO THE BOARD.

### DRP-AI Yolo Demos

The DRP-AI Yolo Application "sample_yolo_cam"  project creates demos for YoloV2, YoloV3, TinyYoloV2, and TinyYoloV3 to be used with the Camera. 

NOTE: The YoloV5 and YOloVx are still in development.

### DRP-AI HRNET

Only Mouse/touch UI funcitonalit and Camera modification was added. 

###  DRP-AI Pre-trained Applications

The DRP-Ai Pre-trained demo's modifications include the mouse/touch response to close the demo and the camera modifcation mentioned in the other demo.

