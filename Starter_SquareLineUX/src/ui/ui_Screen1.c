// This file was generated by SquareLine Studio
// SquareLine Studio version: SquareLine Studio 1.4.1
// LVGL version: 8.3.11
// Project name: StarterUI

#include "ui.h"

void ui_Screen1_screen_init(void)
{
ui_Screen1 = lv_obj_create(NULL);
lv_obj_clear_flag( ui_Screen1, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

ui_ButtonIncrementCount = lv_btn_create(ui_Screen1);
lv_obj_set_width( ui_ButtonIncrementCount, 153);
lv_obj_set_height( ui_ButtonIncrementCount, 50);
lv_obj_set_x( ui_ButtonIncrementCount, -179 );
lv_obj_set_y( ui_ButtonIncrementCount, -51 );
lv_obj_set_align( ui_ButtonIncrementCount, LV_ALIGN_CENTER );
lv_obj_add_flag( ui_ButtonIncrementCount, LV_OBJ_FLAG_SCROLL_ON_FOCUS );   /// Flags
lv_obj_clear_flag( ui_ButtonIncrementCount, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

ui_Label1 = lv_label_create(ui_Screen1);
lv_obj_set_width( ui_Label1, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( ui_Label1, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( ui_Label1, -115 );
lv_obj_set_y( ui_Label1, -160 );
lv_obj_set_align( ui_Label1, LV_ALIGN_CENTER );
lv_label_set_text(ui_Label1,"Starter App for 7\" Panel is working!");
lv_obj_set_style_text_font(ui_Label1, &lv_font_montserrat_20, LV_PART_MAIN| LV_STATE_DEFAULT);

ui_LabelCount = ui_LabelCount_create(ui_Screen1);
lv_obj_set_x( ui_LabelCount, -179 );
lv_obj_set_y( ui_LabelCount, 25 );

ui_Label3 = lv_label_create(ui_Screen1);
lv_obj_set_width( ui_Label3, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( ui_Label3, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( ui_Label3, -179 );
lv_obj_set_y( ui_Label3, -50 );
lv_obj_set_align( ui_Label3, LV_ALIGN_CENTER );
lv_label_set_text(ui_Label3,"Click Me!");
lv_obj_set_style_text_color(ui_Label3, lv_color_hex(0xFCF9F9), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_text_opa(ui_Label3, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(ui_Label3, &lv_font_montserrat_18, LV_PART_MAIN| LV_STATE_DEFAULT);

lv_obj_add_event_cb(ui_ButtonIncrementCount, ui_event_ButtonIncrementCount, LV_EVENT_ALL, NULL);

}
