#ifndef HERLING_DISPLAY_H
#define HERLING_DISPLAY_H

#include "display/lcd_display.h"
#include "display/lvgl_display/gif/lvgl_gif.h"

#include <memory>

// 赫灵自定义仪表盘 Display（继承 SpiLcdDisplay）。
//
// 仪表盘始终显示（含待机/配网/启动/对话）：时钟/课表/待办/设置四页，左右滑动换页，
// 时钟页左侧时间 + 右侧鲸鱼娘小图。对话时鲸鱼娘小图由 SetEmotion 驱动切换状态 GIF，
// 不跳转基类 emoji_box_ 大图。基类对话 UI 隐藏，仅复用其内部对象。
//
// 课表/待办为 L5 场景内容，已外置到 L2 数据层，终端不硬编码
// （见 01_Projects/学习闹钟/学习闹钟 - 分层架构纲领.md）。
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
    lv_obj_t* tabview_ = nullptr;                 // 官方分页组件：承载四页 + 内建滑动翻页
    lv_obj_t* pages_[kPageCount] = {};            // 四个页面容器（即 tabview 的 tab 内容对象）
    lv_obj_t* dots_[kPageCount] = {};             // 四个圆点指示器
    lv_obj_t* dot_container_ = nullptr;

    // ── 时钟页 ──
    lv_obj_t* clock_time_label_ = nullptr;        // HH:MM
    lv_obj_t* clock_date_label_ = nullptr;        // 年月日
    lv_obj_t* clock_week_label_ = nullptr;        // 星期
    lv_obj_t* clock_next_label_ = nullptr;        // 下一节课
    lv_obj_t* clock_pet_state_label_ = nullptr;   // 鲸鱼娘状态

    // ── 鲸鱼娘（常驻时钟页小图，对话时随 agent 状态切换 GIF，不再切大图）──
    lv_obj_t* pet_image_ = nullptr;
    std::unique_ptr<LvglGif> pet_gif_;            // 小图状态动画（idle/waving/review…）
    const char* pet_gif_name_ = nullptr;          // 当前播放 GIF 名，同名跳过防闪烁

    void BuildClockPage();
    void BuildSchedulePage();
    void BuildTodoPage();
    void BuildSettingsPage();

    void RefreshMode();
    void UpdateClockLabels();
    void PlayPetGif(std::unique_ptr<LvglGif>& gif, lv_obj_t* img, const char* gif_name,
                    int scale);

    static void TabChangedCb(lv_event_t* e);
    static void ClockTimerCb(lv_timer_t* timer);
};

#endif  // HERLING_DISPLAY_H
