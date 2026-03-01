//============================================================================
// Name        : rzv2l_Demo.cpp
// Author      : Micahel
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================

#include "app_screen.h"
#include "app_button.h"
#include "app_events.h"


static void rz_screen_objectdetection();
static void rz_screen_pose();
static void rz_screen_pretrainedapps();

App_Screen obj_det_scr;
App_Screen obj_pose_scr;
App_Screen obj_ptd_scr;

typedef struct AppButtons {
	char *label;
	evntFunc event;
	font_sz_t button_label_font_size;
} AppButtons;

#define APP_OBJ_NUM_BTNS 4
AppButtons OjectDetection[] = {
		{ .label = "YoloV2", 		.event = runYoloV2, 	.button_label_font_size = LARGE },
		{ .label = "TinyYoloV2", 	.event = runTinyYoloV2, .button_label_font_size = LARGE },
		{ .label = "YoloV3",		.event = runtYoloV3, 	.button_label_font_size = LARGE },
		{ .label = "TinyYoloV3", 	.event = runTinyYoloV3, .button_label_font_size = LARGE },
		{ .label = "YoloV5",		.event = runYoloV5, 	.button_label_font_size = LARGE },
		{ .label = "YoloVX",		.event = runYoloVX, 	.button_label_font_size = LARGE },

};

#define APP_POSE_NUM_BTNS 1
AppButtons Pose[] = {
		{ .label = "HRNet",		.event = runHRNet, 	.button_label_font_size = LARGE },
		{ .label = "None",		.event = NULL, 		.button_label_font_size = LARGE },
		{ .label = "None",		.event = NULL, 		.button_label_font_size = LARGE },
		{ .label = "None", 		.event = NULL, 		.button_label_font_size = LARGE },
		{ .label = "None", 		.event = NULL, 		.button_label_font_size = LARGE },
		{ .label = "None", 		.event = NULL, 		.button_label_font_size = LARGE },
};

#define APP_PTAPP_NUM_BTNS 6
AppButtons PretrainedApplications[] = {
		{ .label = "  Head\nCounter",		.event = runHeadCount,			.button_label_font_size = SMALL },
		{ .label = "  Line\nCrossing", 		.event = runLineCrossing, 		.button_label_font_size = SMALL },
		{ .label = "Falling",		 		.event = runFallDetection, 		.button_label_font_size = SMALL },
		{ .label = "AgeGender\nDetection", 	.event = runAgeGenderDetection, .button_label_font_size = SMALL },
		{ .label = "  Gaze\nTracking", 		.event = runGaze, 				.button_label_font_size = SMALL },
		{ .label = "  Hand\nGesture", 		.event = runGesture, 			.button_label_font_size = SMALL },
};
int rz_demo() {

	lv_disp_t * dispp = lv_disp_get_default();
	lv_theme_t * theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
											   false, LV_FONT_DEFAULT);
	lv_disp_set_theme(dispp, theme);

	rz_screen_objectdetection();
	rz_screen_pose();
	rz_screen_pretrainedapps();

	lv_obj_add_event_cb(obj_det_scr.screen, app_event_objectdetection, LV_EVENT_ALL, NULL);
	lv_obj_add_event_cb(obj_pose_scr.screen, app_event_pose, LV_EVENT_ALL, NULL);
	lv_obj_add_event_cb(obj_ptd_scr.screen, app_event_pretrainedapps, LV_EVENT_ALL, NULL);

	AppStart(&obj_ptd_scr);
	return 0;
}

static void rz_screen_objectdetection() {
	create_Screen( &obj_det_scr, "RZV2L \nDRP-AI Object Detection");
	for ( int i = 0; i < APP_OBJ_NUM_BTNS; i++ ) {
		register_button( &obj_det_scr, OjectDetection[i].label, OjectDetection[i].event, i, OjectDetection[i].button_label_font_size );
	}
}
static void rz_screen_pose() {
	create_Screen( &obj_pose_scr, "RZV2L \nDRP-AI Pose Estimation");
	for ( int i = 0; i < APP_POSE_NUM_BTNS; i++ ) {
		register_button( &obj_pose_scr, Pose[i].label, Pose[i].event, i, Pose[i].button_label_font_size );
	}
}
static void rz_screen_pretrainedapps(){
	create_Screen( &obj_ptd_scr, "RZV2L \nPreTrained Applications");
	for ( int i = 0; i < APP_PTAPP_NUM_BTNS; i++ ) {
		register_button( &obj_ptd_scr, PretrainedApplications[i].label, PretrainedApplications[i].event, i, PretrainedApplications[i].button_label_font_size );
	}
}

