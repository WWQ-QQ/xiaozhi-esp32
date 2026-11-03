#ifndef HERLING_DISPLAY_H
#define HERLING_DISPLAY_H

#include "display/lcd_display.h"
#include "display/lvgl_display/gif/lvgl_gif.h"

#include <memory>

// 赫灵自定义仪表盘 Display（继承 SpiLcdDisplay）。
//
// 两模式并存：
//   · 仪表盘模式（待机 Idle）：时钟/课表/待办/设置四页，左右滑动换页，
//     时钟页左侧时间 + 右侧鲸鱼娘（idle 动画）。显示 root_，隐藏基类对话 UI。
//   · 对话模式（非 Idle）：复用基类 emoji_box_（鲸鱼娘大图，由 SetEmotion 驱动
//     状态动画）+ status_bar_（状态文字）+ bottom_bar_（字幕）+ top_bar_（电池/网络）。
//     隐藏 root_。
//
// RefreshMode() 按设备状态在两种模式间切换。
class HerlingDisplay : public SpiLcdDisplay {
public:
    HerlingDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                   int width, int height, int offset_x, int offset_y, bool mirror_x,
                   bool mirror_y, bool swap_xy);
    ~HerlingDisplay() override;

    void SetupUI() override;
    void SetStatus(const char* status) override;
    void SetEmotion(const char* emotion) override;

private:
    enum Page : uint8_t {
        kPageClock = 0,
        kPageSchedule = 1,
        kPageTodo = 2,
        kPageSettings = 3,
        kPageCount = 4,
    };

    // ── 仪表盘覆盖层 ──
    lv_obj_t* root_ = nullptr;                    // 全屏仪表盘容器（深色）
    lv_obj_t* title_label_ = nullptr;             // 左上动态标题「赫灵 · 时钟」
    lv_obj_t* pages_[kPageCount] = {};            // 四个页面容器
    lv_obj_t* dots_[kPageCount] = {};             // 四个圆点指示器
    lv_obj_t* dot_container_ = nullptr;

    // ── 时钟页 ──
    lv_obj_t* clock_time_label_ = nullptr;        // HH:MM
    lv_obj_t* clock_date_label_ = nullptr;        // 年月日
    lv_obj_t* clock_week_label_ = nullptr;        // 星期
    lv_obj_t* clock_next_label_ = nullptr;        // 下一节课
    lv_obj_t* clock_pet_state_label_ = nullptr;   // 鲸鱼娘状态

    // ── 鲸鱼娘（时钟页小图 idle + 对话模式大图复用基类 emoji_image_）──
    lv_obj_t* pet_image_ = nullptr;
    std::unique_ptr<LvglGif> pet_gif_;            // 时钟页 idle 动画
    std::unique_ptr<LvglGif> chat_gif_;           // 对话模式状态动画

    Page current_page_ = kPageClock;

    void BuildClockPage();
    void BuildSchedulePage();
    void BuildTodoPage();
    void BuildSettingsPage();

    void ShowPage(Page page);
    void RefreshMode();
    void UpdateClockLabels();
    void PlayPetGif(std::unique_ptr<LvglGif>& gif, lv_obj_t* img, const char* gif_name,
                    int scale);

    static void GestureEventCb(lv_event_t* e);
    static void ClockTimerCb(lv_timer_t* timer);
};

#endif  // HERLING_DISPLAY_H
