#include "LVGL_Driver.h"
#include "ST77916.h"  // 包含SPI配置宏定义
#include "freertos/task.h"

static const char *TAG_LVGL = "LVGL";

lv_disp_draw_buf_t disp_buf;                                                 // contains internal graphic buffer(s) called draw buffer(s)
lv_disp_drv_t disp_drv;                                                      // contains callback functions
lv_indev_drv_t indev_drv;

void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

// ============================================================================
// LVGL显示刷新回调函数
// ============================================================================
// 功能：将LVGL绘制缓冲区的内容传输到LCD屏幕
// 重要说明：
//   1. 此函数在LVGL需要刷新屏幕时被调用
//   2. 必须尽快完成并调用lv_disp_flush_ready()，否则会阻塞LVGL
//   3. 如果传输失败，仍然要通知LVGL继续，避免死锁
//   4. 不要在此函数中添加延迟，会导致SPI队列堆积
// 
// 参数：
//   - drv: LVGL显示驱动指针
//   - area: 需要刷新的屏幕区域
//   - color_map: 颜色数据缓冲区
void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    
    // 计算传输的数据大小（用于调试）
    int width = offsetx2 - offsetx1 + 1;
    int height = offsety2 - offsety1 + 1;
    size_t data_size = width * height * sizeof(lv_color_t);
    
    // 详细日志：说明正在传输什么数据
    ESP_LOGD(TAG_LVGL, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    ESP_LOGD(TAG_LVGL, "LVGL刷新回调: 传输屏幕区域数据");
    ESP_LOGD(TAG_LVGL, "  区域: (%d,%d) -> (%d,%d)", offsetx1, offsety1, offsetx2, offsety2);
    ESP_LOGD(TAG_LVGL, "  尺寸: %d x %d 像素", width, height);
    ESP_LOGD(TAG_LVGL, "  数据大小: %zu 字节 (%zu KB)", data_size, data_size / 1024);
    ESP_LOGD(TAG_LVGL, "  最大限制: %d 字节 (%d KB)", 
             ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE, ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE / 1024);
    ESP_LOGD(TAG_LVGL, "  数据来源: LVGL绘制缓冲区 (color_map指针: %p)", (void*)color_map);
    
    // 检查数据大小是否超过最大传输限制
    // 如果超过，需要手动分块传输
    // 注意：即使ESP-LCD声称支持自动分块，实际测试中发现可能不会自动分块
    // 所以我们需要手动实现分块传输以确保稳定性
    if (data_size > ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE) {
        ESP_LOGW(TAG_LVGL, "⚠️  数据超过限制，需要分块传输");
        ESP_LOGW(TAG_LVGL, "  超过: %zu 字节 (限制: %d 字节)", 
                 data_size - ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE, ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE);
        
        // 手动分块传输：按行分块
        // 计算每块的最大高度（以字节为单位，留一些余量）
        size_t max_bytes_per_chunk = ESP_PANEL_HOST_SPI_MAX_TRANSFER_SIZE - 1024;  // 留1KB余量
        int max_height_per_chunk = max_bytes_per_chunk / (width * sizeof(lv_color_t));
        if (max_height_per_chunk < 1) {
            max_height_per_chunk = 1;  // 至少传输一行
        }
        
        int total_chunks = (height + max_height_per_chunk - 1) / max_height_per_chunk;
        ESP_LOGI(TAG_LVGL, "开始分块传输: %zu 字节 -> %d 块 (每块最多 %d 行, %zu 字节)", 
                 data_size, total_chunks, max_height_per_chunk, max_bytes_per_chunk);
        
        esp_err_t ret = ESP_OK;
        int current_y = offsety1;
        int chunk_index = 0;
        bool has_error = false;
        
        while (current_y <= offsety2) {
            // 计算当前块的高度
            int chunk_height = (current_y + max_height_per_chunk <= offsety2) ? 
                              max_height_per_chunk : (offsety2 - current_y + 1);
            int chunk_y_end = current_y + chunk_height - 1;
            
            // 计算当前块的数据指针偏移
            int y_offset = current_y - offsety1;
            lv_color_t *chunk_data = color_map + (y_offset * width);
            
            // 传输当前块
            size_t chunk_data_size = chunk_height * width * sizeof(lv_color_t);
            ESP_LOGD(TAG_LVGL, "  传输块 [%d/%d]: 区域 (%d,%d) -> (%d,%d), 大小: %zu 字节 (%d 行)", 
                     chunk_index + 1, total_chunks, offsetx1, current_y, offsetx2, chunk_y_end,
                     chunk_data_size, chunk_height);
            
            ret = esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, current_y, offsetx2 + 1, chunk_y_end + 1, chunk_data);
            
            if (ret != ESP_OK) {
                has_error = true;
                ESP_LOGW(TAG_LVGL, "  ✗ 分块传输失败 [块%d/%d]: %s", 
                         chunk_index + 1, total_chunks, esp_err_to_name(ret));
                ESP_LOGW(TAG_LVGL, "     区域: (%d,%d) -> (%d,%d), 大小: %zu 字节", 
                         offsetx1, current_y, offsetx2, chunk_y_end, chunk_data_size);
                // 即使失败也继续传输下一块，避免整个屏幕卡死
            } else {
                ESP_LOGD(TAG_LVGL, "  ✓ 块 [%d/%d] 传输成功", chunk_index + 1, total_chunks);
            }
            
            current_y += chunk_height;
            chunk_index++;
        }
        
        // 记录分块传输结果
        if (!has_error) {
            ESP_LOGI(TAG_LVGL, "✓ 分块传输完成: %d 块全部成功", chunk_index);
        } else {
            ESP_LOGW(TAG_LVGL, "✗ 分块传输部分失败: %d 块中有错误", chunk_index);
        }
        ESP_LOGD(TAG_LVGL, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
        
        // 无论成功或失败，都通知LVGL继续
        lv_disp_flush_ready(drv);
        return;
    }
    
    // 数据大小在限制内，直接传输
    ESP_LOGD(TAG_LVGL, "数据在限制内，直接传输");
    // 将缓冲区内容复制到显示器的指定区域
    // 注意：esp_lcd_panel_draw_bitmap是异步的，会立即返回
    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    
    // 如果传输失败，记录错误但不阻塞（避免死锁）
    if (ret != ESP_OK) {
        // 只记录警告，不阻塞LVGL
        // 频繁的错误日志可能会影响性能，所以使用LOGW级别
        ESP_LOGW(TAG_LVGL, "✗ LCD绘制失败: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG_LVGL, "  区域: %d,%d -> %d,%d (大小: %dx%d, 数据: %zu 字节)", 
                 offsetx1, offsety1, offsetx2, offsety2, width, height, data_size);
        ESP_LOGW(TAG_LVGL, "  可能原因: 1) SPI队列满 2) 传输数据过大 3) SPI频率过高 4) 硬件连接问题");
        // 即使失败也通知LVGL继续，避免卡死
        // 如果频繁失败，可能需要：
        //   1. 降低SPI频率
        //   2. 增大max_transfer_sz
        //   3. 增大trans_queue_depth
        //   4. 检查硬件连接
    } else {
        ESP_LOGD(TAG_LVGL, "✓ 传输成功");
    }
    ESP_LOGD(TAG_LVGL, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    // 重要：必须立即通知LVGL刷新完成，不要添加任何延迟
    // 延迟会导致SPI队列堆积，造成更多传输失败
    // 即使传输失败，也要通知LVGL继续，避免整个UI卡死
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
    
    // 增大缓冲区以减少刷新频率，降低SPI队列压力
    // 缓冲区越大，刷新频率越低，但占用内存越多
    // 可以调整这个值：1/4 = 更大缓冲区（更流畅，但占用更多内存）
    //                 1/8 = 中等缓冲区（平衡）
    //                 1/10 = 较小缓冲区（节省内存，但可能更频繁刷新）
    // 建议：如果有足够PSRAM，使用1/4或1/6；如果内存紧张，使用1/8
    size_t buf_size = (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 4);  // 改为1/4，进一步增大缓冲区
    lv_color_t *buf1 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (buf1 == NULL) {
        // 如果PSRAM分配失败，尝试普通内存
        buf1 = (lv_color_t *)malloc(buf_size * sizeof(lv_color_t));
    }
    assert(buf1);
    
    lv_color_t *buf2 = heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (buf2 == NULL) {
        // 如果PSRAM分配失败，尝试普通内存
        buf2 = (lv_color_t *)malloc(buf_size * sizeof(lv_color_t));
    }
    assert(buf2);
    
    ESP_LOGI(TAG_LVGL, "LVGL缓冲区大小: %d KB (每个缓冲区)", buf_size * sizeof(lv_color_t) / 1024);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, buf_size);                              // initialize LVGL draw buffers

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
