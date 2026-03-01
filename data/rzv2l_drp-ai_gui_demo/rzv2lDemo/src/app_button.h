/*
 * app_button.h
 *
 *  Created on: Apr 11, 2023
 *      Author: zkmike
 */

#ifndef SRC_APP_BUTTON_H_
#define SRC_APP_BUTTON_H_

#include "lvgl/lvgl.h"

typedef enum {
	SMALL,
	LARGE
} font_sz_t;

typedef struct ButtonClass {
	lv_obj_t * rz_Button;
	lv_obj_t * rz_label;
} ButtonClass;

typedef void (*evntFunc)(lv_event_t* event );

ButtonClass * Create_Button(  ButtonClass *B, lv_obj_t* row, const char* name,  evntFunc rz_event, font_sz_t fontsz );

#endif /* SRC_APP_BUTTON_H_ */
