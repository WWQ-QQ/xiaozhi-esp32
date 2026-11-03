#include "herling_display.h"

#include "application.h"
#include "assets.h"
#include "display/lvgl_display/lvgl_image.h"

#include <esp_log.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>
#include <ctime>

#define TAG "HerlingDisplay"

namespace {

// ── 深色拟物配色（对应 HTML 效果图 v2）──
constexpr uint32_t kBg = 0x0C0E10;        // 场景底
constexpr uint32_t kPanelTop = 0x24272C;  // 卡片顶部
constexpr uint32_t kWellInner = 0x07090A; // 凹槽最深
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

// ── 本周课表（周一~周六），节次 + 科目 + 起止时间（真实数据）──
struct Lesson {
    int n;
    const char* sub;
    int s;  // 开始分钟
    int e;  // 结束分钟
};

const Lesson kSchedule[7][6] = {
    {},                                                                      // 0 周日
    {{2, "数学·高二16班", 610, 650}, {4, "数学·高二15班", 730, 770}},        // 周一
    {{1, "数学·高二16班", 560, 600}, {4, "数学·高二15班", 730, 770}},        // 周二
    {{3, "数学·高二15班", 680, 720}, {4, "数学·高二16班", 730, 770}},        // 周三
    {{3, "数学·高二15班", 680, 720}, {4, "数学·高二16班", 730, 770}},        // 周四
    {{4, "数学·高二16班", 730, 770}, {5, "数学·高二15班", 780, 820}},        // 周五
    {},                                                                      // 周六
};

const int kScheduleCount[7] = {0, 2, 2, 2, 2, 2, 0};

// ── 真实待办 ──
struct Todo {
    const char* txt;
    bool done;
};
const Todo kTodos[] = {
    {"等 Hermes 回后台数据基线", false},
    {"唤醒词「你好赫灵」落地", false},
    {"音色保留 xiaohe·转达 Hermes", false},
    {"sudo pm2 startup 开机自启", false},
    {"电源模块 30 秒断电测试", false},
    {"shim :8010 纵深防御", false},
    {"改 root 密码（安全）", false},
    {"设备白名单放行", true},
};
constexpr int kTodoCount = sizeof(kTodos) / sizeof(kTodos[0]);

const char* kWeekCn[] = {"日", "一", "二", "三", "四", "五", "六"};

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
    chat_gif_.reset();
}

void HerlingDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    // 复用基类对话 UI（emoji_box_ / status_bar_ / bottom_bar_ / top_bar_ 等）
    SpiLcdDisplay::SetupUI();

    DisplayLockGuard lock(this);
    auto* screen = lv_screen_active();

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

    // 手势：左右滑动换页
    lv_obj_add_event_cb(root_, GestureEventCb, LV_EVENT_GESTURE, this);

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

    lv_obj_t* list = lv_obj_create(page);
    lv_obj_set_size(list, LV_HOR_RES - 24, LV_VER_RES - 80);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, 24);

    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);
    int wday = tm_now ? tm_now->tm_wday : 0;
    int cnt = kScheduleCount[wday];

    if (cnt == 0) {
        lv_obj_t* empty = lv_label_create(list);
        lv_label_set_text(empty, "今日无课");
        lv_obj_set_style_text_color(empty, lv_color_hex(kTextDim), 0);
        return;
    }

    int hm = tm_now ? (tm_now->tm_hour * 60 + tm_now->tm_min) : 0;
    for (int i = 0; i < cnt; i++) {
        const Lesson& l = kSchedule[wday][i];
        bool active = hm >= l.s && hm < l.e;

        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, LV_HOR_RES - 24, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(row,
                                  active ? lv_color_hex(0x3A2410) : lv_color_hex(kPanelTop), 0);

        char buf[32];
        lv_obj_t* n = lv_label_create(row);
        snprintf(buf, sizeof(buf), "第%d节", l.n);
        lv_label_set_text(n, buf);
        lv_obj_set_style_text_color(n, active ? lv_color_hex(kAmber) : lv_color_hex(kTextDim), 0);

        lv_obj_t* sub = lv_label_create(row);
        lv_label_set_text(sub, l.sub);
        lv_obj_set_style_text_color(sub, active ? lv_color_white() : lv_color_hex(kText), 0);

        lv_obj_t* t = lv_label_create(row);
        snprintf(buf, sizeof(buf), "%02d:%02d", l.s / 60, l.s % 60);
        lv_label_set_text(t, buf);
        lv_obj_set_style_text_color(t, active ? lv_color_hex(kAmber) : lv_color_hex(kTextDim), 0);
    }
}

void HerlingDisplay::BuildTodoPage() {
    auto* page = pages_[kPageTodo];
    lv_obj_t* title = lv_label_create(page);
    lv_label_set_text(title, "全部待办");
    lv_obj_set_style_text_color(title, lv_color_hex(kTextDim), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* list = lv_obj_create(page);
    lv_obj_set_size(list, LV_HOR_RES - 24, LV_VER_RES - 80);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, 24);

    for (int i = 0; i < kTodoCount; i++) {
        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, LV_HOR_RES - 24, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);

        lv_obj_t* box = lv_obj_create(row);
        lv_obj_set_size(box, 14, 14);
        lv_obj_set_style_radius(box, 4, 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_bg_color(box, kTodos[i].done ? lv_color_hex(0x3A2A14)
                                                      : lv_color_hex(kWellInner),
                                  0);

        lv_obj_t* txt = lv_label_create(row);
        lv_label_set_text(txt, kTodos[i].txt);
        lv_obj_set_style_text_color(txt,
                                    kTodos[i].done ? lv_color_hex(kTextDim) : lv_color_hex(kText),
                                    0);
        if (kTodos[i].done) {
            lv_obj_set_style_text_decor(txt, LV_TEXT_DECOR_STRIKETHROUGH, 0);
        }
    }
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
    bool idle = (Application::GetInstance().GetDeviceState() == kDeviceStateIdle);
    if (idle) {
        // 仪表盘模式
        if (root_ != nullptr) lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        if (emoji_box_ != nullptr) lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        if (bottom_bar_ != nullptr) lv_obj_add_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
        UpdateClockLabels();
        // 鲸鱼娘 idle 动画懒加载：assets 就绪前会失败，每次回待机态重试
        if (pet_image_ != nullptr) {
            PlayPetGif(pet_gif_, pet_image_, "idle.gif", 256);
        }
    } else {
        // 对话模式：隐藏仪表盘，露出基类 emoji_box_/status_bar_/bottom_bar_
        if (root_ != nullptr) lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        if (emoji_box_ != nullptr) lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        if (bottom_bar_ != nullptr) lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_HIDDEN);
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
        int wday = tm_now->tm_wday;
        int cnt = kScheduleCount[wday];
        int hm = tm_now->tm_hour * 60 + tm_now->tm_min;
        int next = -1;
        for (int i = 0; i < cnt; i++) {
            if (kSchedule[wday][i].s > hm) {
                next = i;
                break;
            }
        }
        if (next >= 0) {
            const Lesson& l = kSchedule[wday][next];
            snprintf(buf, sizeof(buf), "下一节 %s %02d:%02d", l.sub, l.s / 60, l.s % 60);
            lv_label_set_text(clock_next_label_, buf);
        } else if (cnt > 0) {
            lv_label_set_text(clock_next_label_, "今日课程已结束");
        } else {
            lv_label_set_text(clock_next_label_, "今日无课");
        }
    }
}

void HerlingDisplay::PlayPetGif(std::unique_ptr<LvglGif>& gif, lv_obj_t* img,
                                const char* gif_name, int scale) {
    if (img == nullptr) return;
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
}

void HerlingDisplay::SetEmotion(const char* emotion) {
    auto state = Application::GetInstance().GetDeviceState();
    if (state == kDeviceStateIdle) {
        // 待机：仪表盘时钟页鲸鱼娘保持 idle
        DisplayLockGuard lock(this);
        if (pet_image_ != nullptr) {
            PlayPetGif(pet_gif_, pet_image_, "idle.gif", 256);
        }
        return;
    }

    // 对话模式：鲸鱼娘大图复用基类 emoji_image_
    DisplayLockGuard lock(this);
    if (emoji_image_ != nullptr) {
        PlayPetGif(chat_gif_, emoji_image_, EmotionToGif(emotion), 256);
        lv_obj_remove_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }
}

void HerlingDisplay::SetStatus(const char* status) {
    SpiLcdDisplay::SetStatus(status);  // 基类内部已加锁
    DisplayLockGuard lock(this);
    RefreshMode();
}

void HerlingDisplay::GestureEventCb(lv_event_t* e) {
    auto* self = static_cast<HerlingDisplay*>(lv_event_get_user_data(e));
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT) {
        DisplayLockGuard lock(self);
        self->ShowPage(static_cast<Page>((self->current_page_ + 1) % kPageCount));
    } else if (dir == LV_DIR_RIGHT) {
        DisplayLockGuard lock(self);
        self->ShowPage(static_cast<Page>((self->current_page_ + kPageCount - 1) % kPageCount));
    }
}

void HerlingDisplay::ClockTimerCb(lv_timer_t* timer) {
    auto* self = static_cast<HerlingDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) return;
    DisplayLockGuard lock(self);
    if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
        self->UpdateClockLabels();
    }
}
