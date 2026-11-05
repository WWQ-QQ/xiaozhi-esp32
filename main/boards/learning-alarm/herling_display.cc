#include "herling_display.h"

#include "application.h"
#include "assets.h"
#include "display/lvgl_display/lvgl_image.h"
#include "display/lvgl_display/lvgl_theme.h"

#include <esp_log.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#define TAG "HerlingDisplay"

namespace {

// ── 深色拟物配色（对应 HTML 效果图 v2）──
constexpr uint32_t kBg = 0x0C0E10;        // 场景底
constexpr uint32_t kAmber = 0xFF8A1E;     // 唯一强调色
constexpr uint32_t kText = 0xCFD4D9;      // 主文本
constexpr uint32_t kTextDim = 0x7C838B;   // 次级文本

// ── 鲸鱼娘状态 → assets 分区 GIF 文件名 ──
const char* EmotionToGif(const char* emotion) {
    if (emotion == nullptr) return "idle.gif";
    if (strcmp(emotion, "neutral") == 0) return "idle.gif";
    if (strcmp(emotion, "idle") == 0) return "idle.gif";
    if (strcmp(emotion, "waving") == 0) return "waving.gif";
    if (strcmp(emotion, "listening") == 0) return "waving.gif";
    if (strcmp(emotion, "waiting") == 0) return "waiting.gif";
    if (strcmp(emotion, "review") == 0) return "review.gif";
    if (strcmp(emotion, "thinking") == 0) return "review.gif";
    if (strcmp(emotion, "failed") == 0) return "failed.gif";
    if (strcmp(emotion, "error") == 0) return "failed.gif";
    if (strcmp(emotion, "sleepy") == 0) return "idle.gif";
    return "idle.gif";  // 未知情绪回退待机
}

// 课表/待办是 L5 场景内容，已外置到 L2 数据层（面板 schedule.json / todos.json），
// 终端固件不再硬编码。课表页/待办页/时钟页「下一节」显示占位，待数据层接口接入。
const char* kWeekCn[] = {"日", "一", "二", "三", "四", "五", "六"};

// 滑动翻页最小横向位移（px）。不用 LV_EVENT_GESTURE 的速度门槛，轻滑即可触发。
constexpr int kSwipeThreshold = 30;

const char* kPageTitles[4] = {
    "赫灵 · 时钟", "赫灵 · 课表", "赫灵 · 待办", "赫灵 · 设置"};

}  // namespace

HerlingDisplay::HerlingDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                               int width, int height, int offset_x, int offset_y, bool mirror_x,
                               bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

HerlingDisplay::~HerlingDisplay() {
    pet_gif_.reset();
}

void HerlingDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    // 复用基类对话 UI（emoji_box_ / status_bar_ / bottom_bar_ / top_bar_ 等）
    SpiLcdDisplay::SetupUI();

    // 默认深色主题（治白屏）。SetTheme 内部自锁，故在持锁前调用，避免 lvgl_port_lock 嵌套死锁。
    auto* dark_theme = LvglThemeManager::GetInstance().GetTheme("dark");
    if (dark_theme != nullptr) {
        SetTheme(dark_theme);
    }

    DisplayLockGuard lock(this);
    auto* screen = lv_screen_active();

    // 关掉 screen 滚动，确保左右滑动手势不被滚动消费（仿 chd-esp32s3-box）
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(screen, LV_DIR_NONE);

    // ── 仪表盘覆盖层 root_ ──
    root_ = lv_obj_create(screen);
    lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kBg), 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_scrollbar_mode(root_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // 顶部标题（左上）
    title_label_ = lv_label_create(root_);
    lv_label_set_text(title_label_, kPageTitles[kPageClock]);
    lv_obj_set_style_text_color(title_label_, lv_color_hex(kText), 0);
    lv_obj_align(title_label_, LV_ALIGN_TOP_LEFT, 12, 8);

    // 圆点指示器（底部居中）
    dot_container_ = lv_obj_create(root_);
    lv_obj_set_size(dot_container_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dot_container_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dot_container_, 0, 0);
    lv_obj_set_style_pad_all(dot_container_, 0, 0);
    lv_obj_set_style_pad_column(dot_container_, 6, 0);
    lv_obj_set_flex_flow(dot_container_, LV_FLEX_FLOW_ROW);
    lv_obj_align(dot_container_, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_scrollbar_mode(dot_container_, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < kPageCount; i++) {
        dots_[i] = lv_obj_create(dot_container_);
        lv_obj_set_size(dots_[i], 6, 6);
        lv_obj_set_style_radius(dots_[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dots_[i], lv_color_hex(kTextDim), 0);
        lv_obj_set_style_border_width(dots_[i], 0, 0);
        lv_obj_set_style_pad_all(dots_[i], 0, 0);
    }

    // 四个页面容器
    for (int i = 0; i < kPageCount; i++) {
        pages_[i] = lv_obj_create(root_);
        lv_obj_set_size(pages_[i], LV_HOR_RES, LV_VER_RES - 44);
        lv_obj_set_style_radius(pages_[i], 0, 0);
        lv_obj_set_style_bg_opa(pages_[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pages_[i], 0, 0);
        lv_obj_set_style_pad_all(pages_[i], 12, 0);
        lv_obj_set_scrollbar_mode(pages_[i], LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(pages_[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(pages_[i], LV_ALIGN_TOP_MID, 0, 40);
        if (i != kPageClock) lv_obj_add_flag(pages_[i], LV_OBJ_FLAG_HIDDEN);
    }

    BuildClockPage();
    BuildSchedulePage();
    BuildTodoPage();
    BuildSettingsPage();

    // 滑动换页：自建按下/抬起位移判定（阈值轻、轻滑即翻页）。
    // 用输入设备级事件（lv_indev_add_event_cb）而非对象级 lv_obj_add_event_cb——
    // 对象级 PRESSED/RELEASED 只派发给命中对象且默认不冒泡到 screen，收不到；
    // LV_EVENT_GESTURE 又有速度门槛，慢滑不触发。indev 级事件绕开命中/冒泡，轻滑即翻页。
    lv_indev_t* touch_indev = nullptr;
    for (lv_indev_t* it = lv_indev_get_next(nullptr); it != nullptr; it = lv_indev_get_next(it)) {
        if (lv_indev_get_type(it) == LV_INDEV_TYPE_POINTER) {
            touch_indev = it;
            break;
        }
    }
    if (touch_indev != nullptr) {
        lv_indev_add_event_cb(touch_indev, TouchEventCb, LV_EVENT_PRESSED, this);
        lv_indev_add_event_cb(touch_indev, TouchEventCb, LV_EVENT_RELEASED, this);
    }

    // 时钟刷新定时器（每秒）
    lv_timer_create(ClockTimerCb, 1000, this);

    // 初始状态：待机 → 仪表盘
    RefreshMode();
}

void HerlingDisplay::BuildClockPage() {
    auto* page = pages_[kPageClock];

    // 左侧信息栏
    lv_obj_t* info = lv_obj_create(page);
    lv_obj_set_size(info, LV_HOR_RES / 2, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(info, 0, 0);
    lv_obj_set_style_pad_all(info, 0, 0);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(info, 6, 0);
    lv_obj_align(info, LV_ALIGN_LEFT_MID, 8, 0);

    // 时间 HH:MM（noto 20px 放大 2x 至视觉约 40px，避免依赖未编译的 montserrat_48）
    clock_time_label_ = lv_label_create(info);
    lv_label_set_text(clock_time_label_, "--:--");
    lv_obj_set_style_text_color(clock_time_label_, lv_color_hex(kAmber), 0);
    lv_obj_set_style_transform_scale(clock_time_label_, 512, 0);  // 256 = 100%，512 = 2x

    // 日期
    clock_date_label_ = lv_label_create(info);
    lv_label_set_text(clock_date_label_, "----.--.--");
    lv_obj_set_style_text_color(clock_date_label_, lv_color_hex(kText), 0);

    // 星期
    clock_week_label_ = lv_label_create(info);
    lv_label_set_text(clock_week_label_, "星期-");
    lv_obj_set_style_text_color(clock_week_label_, lv_color_hex(kTextDim), 0);

    // 鲸鱼娘状态
    clock_pet_state_label_ = lv_label_create(info);
    lv_label_set_text(clock_pet_state_label_, "待机");
    lv_obj_set_style_text_color(clock_pet_state_label_, lv_color_hex(kAmber), 0);

    // 下一节课
    clock_next_label_ = lv_label_create(info);
    lv_label_set_text(clock_next_label_, "今日无课");
    lv_obj_set_style_text_color(clock_next_label_, lv_color_hex(kTextDim), 0);

    // 右侧鲸鱼娘小图
    pet_image_ = lv_img_create(page);
    lv_obj_align(pet_image_, LV_ALIGN_RIGHT_MID, -8, 0);
}

void HerlingDisplay::BuildSchedulePage() {
    auto* page = pages_[kPageSchedule];
    lv_obj_t* title = lv_label_create(page);
    lv_label_set_text(title, "今日课表");
    lv_obj_set_style_text_color(title, lv_color_hex(kTextDim), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    // 课表数据已外置到 L2 数据层（面板 schedule.json），终端不硬编码。
    // 待数据层只读接口接入后，这里改为动态渲染。
    lv_obj_t* hint = lv_label_create(page);
    lv_label_set_text(hint, "课表数据待接入");
    lv_obj_set_style_text_color(hint, lv_color_hex(kTextDim), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);
}

void HerlingDisplay::BuildTodoPage() {
    auto* page = pages_[kPageTodo];
    lv_obj_t* title = lv_label_create(page);
    lv_label_set_text(title, "全部待办");
    lv_obj_set_style_text_color(title, lv_color_hex(kTextDim), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    // 待办数据已外置到 L2 数据层（面板 todos.json），终端不硬编码。
    // 待数据层只读接口接入后，这里改为动态渲染。
    lv_obj_t* hint = lv_label_create(page);
    lv_label_set_text(hint, "待办数据待接入");
    lv_obj_set_style_text_color(hint, lv_color_hex(kTextDim), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);
}

void HerlingDisplay::BuildSettingsPage() {
    auto* page = pages_[kPageSettings];
    lv_obj_t* title = lv_label_create(page);
    lv_label_set_text(title, "设置");
    lv_obj_set_style_text_color(title, lv_color_hex(kTextDim), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* hint = lv_label_create(page);
    lv_label_set_text(hint, "音量 / 亮度 / 网络 · 待接入");
    lv_obj_set_style_text_color(hint, lv_color_hex(kTextDim), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 0);
}

void HerlingDisplay::ShowPage(Page page) {
    if (page >= kPageCount) return;
    current_page_ = page;
    for (int i = 0; i < kPageCount; i++) {
        if (i == page) {
            lv_obj_remove_flag(pages_[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pages_[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_label_set_text(title_label_, kPageTitles[page]);
    for (int i = 0; i < kPageCount; i++) {
        lv_obj_set_style_bg_color(dots_[i],
                                  i == page ? lv_color_hex(kAmber) : lv_color_hex(kTextDim), 0);
    }
}

void HerlingDisplay::RefreshMode() {
    // 仪表盘始终显示（含待机/配网/启动/对话）。鲸鱼娘常驻时钟页小图，不再跳转基类
    // emoji_box_ 大图（用户确认：形象在桌面即可）。
    if (root_ != nullptr) lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    if (bottom_bar_ != nullptr) lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);

    // 鲸鱼娘表情由设备状态驱动（而非服务端 emotion 字符串——应用层在 Listening/Connecting/
    // Idle 均下发 "neutral"，无法区分状态，表情恒定不变）。待机 idle，聆听挥手，说话思考，
    // 连接等待。
    auto state = Application::GetInstance().GetDeviceState();
    const char* gif = EmotionToGif("neutral");
    if (state == kDeviceStateListening) {
        gif = EmotionToGif("listening");
    } else if (state == kDeviceStateSpeaking) {
        gif = EmotionToGif("thinking");
    } else if (state == kDeviceStateConnecting) {
        gif = EmotionToGif("waiting");
    }

    UpdateClockLabels();
    // 鲸鱼娘动画懒加载：assets 就绪前会失败，下次状态刷新重试
    if (pet_image_ != nullptr) {
        PlayPetGif(pet_gif_, pet_image_, gif, 256);
    }
}

void HerlingDisplay::UpdateClockLabels() {
    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);
    if (tm_now == nullptr || tm_now->tm_year < 2025 - 1900) return;

    char buf[64];
    if (clock_time_label_ != nullptr) {
        snprintf(buf, sizeof(buf), "%02d:%02d", tm_now->tm_hour, tm_now->tm_min);
        lv_label_set_text(clock_time_label_, buf);
    }
    if (clock_date_label_ != nullptr) {
        snprintf(buf, sizeof(buf), "%d.%02d.%02d", tm_now->tm_year + 1900, tm_now->tm_mon + 1,
                 tm_now->tm_mday);
        lv_label_set_text(clock_date_label_, buf);
    }
    if (clock_week_label_ != nullptr) {
        snprintf(buf, sizeof(buf), "星期%s", kWeekCn[tm_now->tm_wday]);
        lv_label_set_text(clock_week_label_, buf);
    }

    if (clock_next_label_ != nullptr) {
        // 课表已外置到 L2 数据层，终端不再本地推算「下一节」。
        // 待数据层只读接口接入后，由接口返回下一节课信息。
        lv_label_set_text(clock_next_label_, "课表待接入");
    }
}

void HerlingDisplay::PlayPetGif(std::unique_ptr<LvglGif>& gif, lv_obj_t* img,
                                const char* gif_name, int scale) {
    if (img == nullptr) return;
    if (gif && pet_gif_name_ != nullptr && strcmp(pet_gif_name_, gif_name) == 0)
        return;  // 同名已在播，跳过防闪烁
    auto& assets = Assets::GetInstance();
    void* ptr = nullptr;
    size_t size = 0;
    if (!assets.GetAssetData(gif_name, ptr, size)) {
        ESP_LOGW(TAG, "Pet GIF asset %s not found (assets not ready?)", gif_name);
        return;
    }

    if (gif) {
        gif->Stop();
        gif.reset();
    }

    // LvglRawImage 仅构造 lv_img_dsc_t，LvglGif 构造后只依赖 img_dsc->data
    // （assets 分区 mmap 常驻指针），不依赖 raw 对象本身，故构造完即 delete。
    auto* raw = new LvglRawImage(ptr, size);
    gif = std::make_unique<LvglGif>(raw->image_dsc());
    delete raw;
    if (!gif->IsLoaded()) {
        ESP_LOGW(TAG, "Failed to load pet GIF %s", gif_name);
        gif.reset();
        return;
    }

    gif->SetFrameCallback([img, gif = gif.get()]() {
        lv_image_set_src(img, gif->image_dsc());
    });
    lv_image_set_src(img, gif->image_dsc());
    lv_image_set_scale(img, scale);
    gif->Start();
    pet_gif_name_ = gif_name;
}

void HerlingDisplay::SetEmotion(const char* emotion) {
    // 鲸鱼娘表情由设备状态驱动（见 RefreshMode），忽略服务端 emotion 字符串并收敛到状态驱动。
    (void)emotion;
    DisplayLockGuard lock(this);
    RefreshMode();
}

void HerlingDisplay::SetStatus(const char* status) {
    SpiLcdDisplay::SetStatus(status);  // 基类内部已加锁
    DisplayLockGuard lock(this);
    RefreshMode();
}

void HerlingDisplay::TouchEventCb(lv_event_t* e) {
    auto* self = static_cast<HerlingDisplay*>(lv_event_get_user_data(e));
    lv_indev_t* indev = lv_event_get_indev(e);
    if (indev == nullptr) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        self->press_x_ = pt.x;
        self->press_y_ = pt.y;
        return;
    }

    // 松开：按位移判定，轻滑即翻页（LV_EVENT_GESTURE 要求快速甩动速度，慢滑不触发）
    int dx = pt.x - self->press_x_;
    int dy = pt.y - self->press_y_;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx > kSwipeThreshold && adx > ady) {
        // 右滑（dx>0）→ 上一页；左滑（dx<0）→ 下一页
        Page target = static_cast<Page>(
            (self->current_page_ + (dx > 0 ? kPageCount - 1 : 1)) % kPageCount);
        ESP_LOGI(TAG, "Swipe %s -> page %d (dx=%d, dy=%d)", dx > 0 ? "right" : "left",
                 (int)target, dx, dy);
        DisplayLockGuard lock(self);
        self->ShowPage(target);
    }
}

void HerlingDisplay::ClockTimerCb(lv_timer_t* timer) {
    auto* self = static_cast<HerlingDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) return;
    DisplayLockGuard lock(self);
    self->UpdateClockLabels();
}
