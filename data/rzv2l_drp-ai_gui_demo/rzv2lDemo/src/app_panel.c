/*
 * app_panel.c
 *
 *  Created on: Apr 12, 2023
 *      Author: zkmike
 */

#include "app_panel.h"
#define WIDTH	524
#define HEIGHT  314

void app_CreatePanel ( PanelClass * P, lv_obj_t* ScreenLayout ) {


	P->panel = lv_obj_create(ScreenLayout);
	lv_obj_set_width(P->panel, WIDTH);
	lv_obj_set_height(P->panel, HEIGHT);
	// Position is set by Parent ScreenLayout
	lv_obj_set_align(P->panel, LV_ALIGN_CENTER);
	lv_obj_set_flex_flow(P->panel, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(P->panel, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_clear_flag(P->panel, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
	lv_obj_set_style_bg_color(P->panel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(P->panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_color(P->panel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_opa(P->panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

	P->maxButtons = NUM_ROWS * NUM_COLS;
	P->maxCols = NUM_ROWS;
	P->maxCols = NUM_COLS;
	P->cntButtons = 0;


	for ( int r = 0; r < NUM_ROWS; r++ ) {
		lv_obj_t* ButtonRow = lv_obj_create(P->panel);
		lv_obj_set_width(ButtonRow, 457);
		lv_obj_set_height(ButtonRow, 105);
		// Position is set by Parent Panel Object P->panel
		lv_obj_set_align(ButtonRow, LV_ALIGN_CENTER);
		lv_obj_set_flex_flow(ButtonRow, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(ButtonRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_clear_flag(ButtonRow, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
		lv_obj_set_style_bg_color(ButtonRow, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_bg_opa(ButtonRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_border_color(ButtonRow, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_border_opa(ButtonRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
		lv_obj_set_style_border_width(ButtonRow, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
		P->rows[r] = ButtonRow;

	}

}


void app_addButton( PanelClass *Panel, const char* name, evntFunc event , font_sz_t font_size) {
	int row = 0;

	if ( Panel->cntButtons < Panel->maxButtons ) {
		row = (int)( Panel->cntButtons / Panel->maxCols );
		Create_Button( &(Panel->buttons[ Panel->cntButtons ]), Panel->rows[row], name, event, font_size );
		Panel->cntButtons += 1;
	}
}

