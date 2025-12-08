#include "lvgl_driver.h"
#include "esp_heap_caps.h"

static const char *TAG_LVGL = "LVGL";

    

lv_disp_draw_buf_t disp_buf;                                                 // contains internal graphic buffer(s) called draw buffer(s)
lv_disp_drv_t disp_drv;                                                      // contains callback functions
lv_indev_drv_t indev_drv;

void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}


void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 +1, offsety2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

/*Read the touchpad*/
void example_touchpad_read( lv_indev_drv_t * drv, lv_indev_data_t * data )
{
    uint16_t touchpad_x[5] = {0};
    uint16_t touchpad_y[5] = {0};
    uint8_t touchpad_cnt = 0;

    /* Read touch controller data */
    esp_lcd_touch_read_data(drv->user_data);

    /* Get coordinates */
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(drv->user_data, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 5);

    // printf("CCCCCCCCCCCCC=%d  \r\n",touchpad_cnt);
    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PR;
        // printf("X=%u Y=%u num=%d \r\n", data->point.x, data->point.y,touchpad_cnt);
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
   
}
/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
void example_lvgl_port_update_callback(lv_disp_drv_t *drv)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;

    switch (drv->rotated) {
    case LV_DISP_ROT_NONE:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    case LV_DISP_ROT_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISP_ROT_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISP_ROT_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
lv_disp_t *disp;
void LVGL_Init(void)
{
    ESP_LOGI(TAG_LVGL, "Initialize LVGL library");
    lv_init();
    
    // 打印详细的内存状态
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t total_spiram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    
    ESP_LOGI(TAG_LVGL, "========== 内存状态 ==========");
    ESP_LOGI(TAG_LVGL, "PSRAM 总容量: %d KB (%d MB)", total_spiram / 1024, total_spiram / 1024 / 1024);
    ESP_LOGI(TAG_LVGL, "PSRAM 空闲: %d KB", free_spiram / 1024);
    ESP_LOGI(TAG_LVGL, "Internal RAM 空闲: %d KB", free_internal / 1024);
    ESP_LOGI(TAG_LVGL, "==============================");
    
    // 计算显存大小
    size_t buf_size = LVGL_BUF_LEN * sizeof(lv_color_t);
    ESP_LOGI(TAG_LVGL, "显存缓冲区大小: %d KB x 2 = %d KB", buf_size / 1024, buf_size * 2 / 1024);

    // 优先从 PSRAM 分配显存
    lv_color_t *buf1 = NULL;
    lv_color_t *buf2 = NULL;
    
    if (free_spiram >= buf_size * 2) {
        buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf1) {
            ESP_LOGI(TAG_LVGL, "显存缓冲区1 从 PSRAM 分配成功");
        }
        buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (buf2) {
            ESP_LOGI(TAG_LVGL, "显存缓冲区2 从 PSRAM 分配成功");
        }
    }
    
    // 如果 PSRAM 分配失败，回退到 Internal RAM
    if (!buf1) {
        ESP_LOGW(TAG_LVGL, "PSRAM 分配失败，尝试 Internal RAM...");
        buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (buf1) {
            ESP_LOGW(TAG_LVGL, "显存缓冲区1 从 Internal RAM 分配成功");
        }
    }
    if (!buf2) {
        ESP_LOGW(TAG_LVGL, "PSRAM 分配失败，尝试 Internal RAM...");
        buf2 = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (buf2) {
            ESP_LOGW(TAG_LVGL, "显存缓冲区2 从 Internal RAM 分配成功");
        }
    }
    
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG_LVGL, "无法分配显存缓冲区！");
        ESP_LOGE(TAG_LVGL, "请检查: 1. PSRAM 是否正确配置 2. 内存是否足够");
    }
    assert(buf1);
    assert(buf2);
    
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, LVGL_BUF_LEN);                              // initialize LVGL draw buffers

    // 打印分配后的内存状态
    size_t free_spiram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t free_internal_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG_LVGL, "分配后 - PSRAM 空闲: %d KB, Internal RAM 空闲: %d KB", 
             free_spiram_after / 1024, free_internal_after / 1024);

    ESP_LOGI(TAG_LVGL, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);                                                                        // Create a new screen object and initialize the associated device
    disp_drv.hor_res = EXAMPLE_LCD_WIDTH;             
    disp_drv.ver_res = EXAMPLE_LCD_HEIGHT;                                                     // Horizontal pixel count
    // disp_drv.rotated = LV_DISP_ROT_90; // 图像旋转                                                            // Vertical axis pixel count
    disp_drv.flush_cb = example_lvgl_flush_cb;                                                          // Function : copy a buffer's content to a specific area of the display
    disp_drv.drv_update_cb = example_lvgl_port_update_callback;                                         // Function : Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. 
    disp_drv.draw_buf = &disp_buf;                                                                      // LVGL will use this buffer(s) to draw the screens contents
    disp_drv.user_data = panel_handle;                
    ESP_LOGI(TAG_LVGL,"Register display indev to LVGL");                                                  // Custom display driver user data
    disp = lv_disp_drv_register(&disp_drv);     
    
    lv_indev_drv_init ( &indev_drv );
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_touchpad_read;
    indev_drv.user_data = tp;
    lv_indev_drv_register( &indev_drv );

    /********************* LVGL *********************/
    ESP_LOGI(TAG_LVGL, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

}
