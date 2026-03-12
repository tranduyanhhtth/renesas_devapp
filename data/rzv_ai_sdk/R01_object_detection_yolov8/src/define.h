/*
 * Original Code (C) Copyright Edgecortix, Inc. 2022
 * Modified Code (C) Copyright Renesas Electronics Corporation 2023
 *
 *  *1 DRP-AI TVM is powered by EdgeCortix MERA(TM) Compiler Framework.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */
/*******************************************************************************
* File Name    : define.h
* Version      : v1.00
* Description  : RZ/V AI SDK Sample Application for Object Detection (YOLOv8n)
*******************************************************************************/

#ifndef DEFINE_MACRO_H
#define DEFINE_MACRO_H

/*Uncomment to display the camera framerate on application window. */
// #define DISP_CAM_FRAME_RATE
/*****************************************
* includes
******************************************/
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <vector>
#include <map>
#include <fstream>
#include <iomanip>
#include <cstring>
#include <float.h>
#include <atomic>
#include <semaphore.h>
#include <math.h>
#include <fstream>
#include <sys/time.h>
#include <climits>
#include <builtin_fp16.h>
/*Camera control and GUI control*/
#include <opencv2/opencv.hpp>
#include <opencv2/highgui.hpp>
#ifdef DISP_CAM_FRAME_RATE
#include <numeric>
#endif

/*****************************************
* Static Variables for YOLOv8n
* Following variables need to be changed to customize the AI model
*  - model_dir = directory name of DRP-AI TVM[*1] Model Object files
******************************************/
/* Model Binary */
const static std::string model_dir = "yolov8n_onnx";
/* Pre-processing Runtime Object */
const static std::string pre_dir = model_dir + "/preprocess";
/* Class labels to be classified */
const static std::string label_list = "label.txt";
/* Empty since labels will be loaded from label_list file */
static std::vector<std::string> label_file_map = {};

/*****************************************
* Macro for YOLOv8n
******************************************/

/*Camera Capture Information */
#ifdef V2H
#define INPUT_CAM_NAME              "USB Camera"

/*Wayland Display Image Information*/
#define IMAGE_OUTPUT_WIDTH          (1920)
#define IMAGE_OUTPUT_HEIGHT         (1080)

/*Image:: Information for drawing on image*/
#define CHAR_SCALE_LARGE            (1.0)
#define CHAR_SCALE_SMALL            (0.9)
#define CHAR_SCALE_VERY_SMALL       (0.6)
#else /* for V2L */
/*DRP-AI memory area offset for model objects*/
/*Offset value depends on the size of memory area used by DRP-AI Pre-processing Runtime Object files*/
#define DRPAI_MEM_OFFSET            (0x38E0000)
#define INPUT_CAM_NAME              "MIPI Camera"

/*Wayland Display Image Information*/
#define IMAGE_OUTPUT_WIDTH          (1280)
#define IMAGE_OUTPUT_HEIGHT         (720)

/*Image:: Text information to be drawn on image*/
#define CHAR_SCALE_LARGE            (0.7)
#define CHAR_SCALE_SMALL            (0.6)
#define CHAR_SCALE_VERY_SMALL       (0.43)
#endif  /* V2H */
/*RESIZE_SCALE=((OUTPUT_WIDTH/IMAGE_WIDTH > OUTPUT_HEIGHT/IMAGE_HEIGHT) ?
        OUTPUT_HEIGHT/IMAGE_HEIGHT : OUTPUT_WIDTH/IMAGE_WIDTH)*/
#define RESIZE_SCALE                (1.5)

/* Number of class to be detected */
#define NUM_CLASS                   (6)
/* YOLOv8 is anchor-free.
 * Total detection points = 80x80 + 40x40 + 20x20 = 8400 */
#define NUM_ANCHORS                 (8400)
/* YOLOv8 output features per detection point: 4 (cx,cy,w,h) + 6 (class scores) */
#define NUM_FEATURES                (10)
/* Thresholds */
#define TH_PROB                     (0.25f)
#define TH_NMS                      (0.45f)
/* Size of input image to the model (YOLOv8n: 640x640) */
#define MODEL_IN_W                  (640)
#define MODEL_IN_H                  (640)

/* Number of DRP-AI output elements: NUM_FEATURES * NUM_ANCHORS */
const static uint32_t INF_OUT_SIZE  = (uint32_t)(NUM_FEATURES) * (uint32_t)(NUM_ANCHORS);

/*****************************************
* Macro for Application
******************************************/
/*Maximum DRP-AI Timeout threshold*/
#define DRPAI_TIMEOUT               (5)

/*Camera Capture Image Information*/
#define CAM_IMAGE_WIDTH             (640)
#define CAM_IMAGE_HEIGHT            (480)
#define CAM_IMAGE_CHANNEL_BGR       (3)
/*Camera Capture Information */
#define CAPTURE_STABLE_COUNT        (8)

/*DRP-AI Input image information*/
/*** DRP-AI input is assigned to the buffer having the size of CAM_IMAGE_WIDTH^2 */
#define DRPAI_IN_WIDTH              (CAM_IMAGE_WIDTH) 
#define DRPAI_IN_HEIGHT             (CAM_IMAGE_WIDTH)
#define IMAGE_OUTPUT_CHANNEL_BGRA   (4)

/*DRP-AI Input image information*/
#define DRPAI_OUT_WIDTH             (IMAGE_OUTPUT_WIDTH)
#define DRPAI_OUT_HEIGHT            (IMAGE_OUTPUT_HEIGHT)

/*Image:: Information for drawing on image*/
#define CHAR_THICKNESS              (2)
#define CHAR_SCALE_BB               (0.4)
#define CHAR_THICKNESS_BB           (1)
#define LINE_HEIGHT                 (30) /*in pixel*/
#define LINE_HEIGHT_OFFSET          (20) /*in pixel*/
#define TEXT_WIDTH_OFFSET           (10) /*in pixel*/
#define WHITE_DATA                  (0xFFFFFFu) /* in RGB */
#define BLACK_DATA                  (0x000000u) /* in RGB */
#define GREEN_DATA                  (0x00FF00u) /* in RGB */
#define RGB_FILTER                  (0x0000FFu) /* in RGB */
#define BOX_LINE_SIZE               (1)  /*in pixel*/
#define BOX_DOUBLE_LINE_SIZE        (1)  /*in pixel*/
#define ALIGHN_LEFT                 (1)
#define ALIGHN_RIGHT                (2)
/*For termination method display*/
#define TEXT_START_X                (1440) 

/* DRPAI_FREQ is the frequency settings  */
/* for DRP-AI.                           */
/* Basically use the default values      */

#define DRPAI_FREQ                  (2)
/* DRPAI_FREQ can be set from 1 to 127   */
/* 1,2: 1GHz                             */
/* 3: 630MHz                             */
/* 4: 420MHz                             */
/* 5: 315MHz                             */
/* ...                                   */
/* 127: 10MHz                            */
/* Calculation Formula:                  */
/*     1260MHz /(DRPAI_FREQ - 1)         */
/*     (When DRPAI_FREQ = 3 or more.)    */

/*Timer Related*/
#define CAPTURE_TIMEOUT         (20)  /* seconds */
#define AI_THREAD_TIMEOUT       (20)  /* seconds */
#define IMAGE_THREAD_TIMEOUT    (20)  /* seconds */
#define DISPLAY_THREAD_TIMEOUT  (20)  /* seconds */
#define KEY_THREAD_TIMEOUT      (5)   /* seconds */
#define TIME_COEF               (1)
/*Waiting Time*/
#define WAIT_TIME               (1000) /* microseconds */

/*Array size*/
#define SIZE_OF_ARRAY(array) (sizeof(array)/sizeof(array[0]))

#endif
