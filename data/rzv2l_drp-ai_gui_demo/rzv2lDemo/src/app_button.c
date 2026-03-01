/*
 * app_button.c
 *
 *  Created on: Apr 12, 2023
 *      Author: zkmike
 */



#include "app_button.h"


ButtonClass * Create_Button( ButtonClass *B, lv_obj_t* row, const char* name,  evntFunc rz_event, font_sz_t fontsz ) {


	B->rz_Button = lv_btn_create(row);
	lv_obj_set_width(B->rz_Button, 194);
	lv_obj_set_height(B->rz_Button, 55);
	// Button Position is set by Parent row
	lv_obj_set_align(B->rz_Button, LV_ALIGN_CENTER);
	lv_obj_set_flex_flow(B->rz_Button, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(B->rz_Button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
	lv_obj_add_flag(B->rz_Button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
	lv_obj_clear_flag(B->rz_Button, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
	lv_obj_set_style_bg_color(B->rz_Button, lv_color_hex(0x212223), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(B->rz_Button, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

	B->rz_label = lv_label_create(B->rz_Button);
	lv_obj_set_width(B->rz_label, LV_SIZE_CONTENT);   /// 1
	lv_obj_set_height(B->rz_label, LV_SIZE_CONTENT);    /// 1
	// Label Position is set by Parent rz_Button
	lv_obj_set_align(B->rz_label, LV_ALIGN_CENTER);
	lv_obj_set_flex_flow(B->rz_label, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(B->rz_label, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
	lv_label_set_text(B->rz_label, name );
	lv_obj_set_style_text_color(B->rz_label, lv_color_hex(0x0034FB), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_opa(B->rz_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
	if ( fontsz == SMALL ) {
		lv_obj_set_style_text_font(B->rz_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
	} else {
		lv_obj_set_style_text_font(B->rz_label, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
	}

	lv_obj_add_event_cb(B->rz_Button, rz_event, LV_EVENT_PRESSED, NULL);
}
