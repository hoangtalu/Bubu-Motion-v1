#include "fortune_system.h"

#include "display.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <mutex>

#include <esp_random.h>
#include <lvgl.h>

extern const lv_font_t lv_font_montserrat_vn_22;

namespace FortuneSystem {
void Close(bool invoke_callback);
}

static void FinishAnimationCb(lv_anim_t* a);

namespace {

constexpr uint32_t COLOR_BACKGROUND = 0x050812;
constexpr uint32_t COLOR_TEXT = 0xFFFFFF;
constexpr uint32_t MESSAGE_SCROLL_PX_PER_SEC = 110;
constexpr uint32_t MESSAGE_MIN_DURATION_MS = 3500;
constexpr uint32_t MESSAGE_MAX_DURATION_MS = 14000;
constexpr int16_t MESSAGE_PANEL_SIZE = 240;

constexpr std::array<const char*, 36> kMessages = {{
    "Hôm nay mọi thứ sẽ sáng hơn một chút, nếu bạn chịu mở mắt.",
    "Có người đang nhớ bạn. Hoặc đang đòi nợ. Khó phân biệt.",
    "Bỏ qua drama. Năng lượng của bạn không miễn phí.",
    "Một cơ hội nhỏ đang đến. Nhớ đứng dậy mà đón.",
    "Bạn không trễ. Bạn đang đi đúng nhịp của mình.",
    "Đừng cố hiểu hết mọi thứ hôm nay. Một phần để mai.",
    "Ngủ thêm chút nữa cũng là một kế hoạch.",
    "Tài lộc có ghé qua. Có thể dưới dạng một tin nhắn.",
    "Đừng mở lại tin nhắn cũ. Vũ trụ không khuyến khích tự hại.",
    "Bạn đang làm tốt hơn bạn nghĩ. Nhắc lại cho chắc.",
    "Nói ít đi một chút, vận may thích người bí ẩn.",
    "Một bước nhỏ cũng tính là tiến lên.",
    "Thử đổi hướng đi hôm nay. Có khi đổi cả mood.",
    "Bạn sẽ cười vì một chuyện rất nhỏ. Đó là dấu hiệu tốt.",
    "Có một niềm vui đang đợi ở chỗ bạn ít ngờ nhất.",
    "Bật chế độ nhẹ nhàng. Hôm nay không cần quá gồng.",
    "Đừng để deadline cướp mất tinh thần của bạn.",
    "Một bữa ăn ngon có thể giải được nhiều chuyện.",
    "Mọi thứ rồi sẽ ổn. Chậm một chút cũng vẫn là ổn.",
    "Bạn có duyên với điều tốt. Chỉ là nó hay đến trễ.",
    "Đừng tin hết lời người khác nói. Tin vừa đủ thôi.",
    "Hôm nay hợp để bắt đầu lại, nhưng đừng bắt đầu với người cũ.",
    "Vận may đang ở gần. Nhìn kỹ phía trước.",
    "Một câu nói đúng lúc sẽ đổi cả ngày của bạn.",
    "Đừng quên uống nước. Quẻ tốt cũng cần cơ thể khỏe.",
    "Một người lạ có thể giúp bạn. Có thể là shipper.",
    "Chuyện khó rồi sẽ qua. Cái qua nhanh nhất là sự lo lắng.",
    "Bớt soi mình quá mức. Bạn không phải bản lỗi.",
    "Ai đó đang nghĩ tốt về bạn. Hãy nhận lấy.",
    "Hôm nay thích hợp để tha thứ cho chính mình.",
    "Đừng ép bản thân phải hoàn hảo. Chỉ cần thật.",
    "Cười lên, rồi đi tiếp. Câu trả lời hay tự tới.",
    "Bạn sắp gặp một tin vui nho nhỏ.",
    "Đi chậm một chút không làm bạn thua.",
    "Đừng đoán quá nhiều. Đôi khi quẻ đẹp nhất là sự bình yên.",
    "Bạn đang ở đúng chỗ để có một điều bất ngờ.",
}};

constexpr const char* kLuckyPrefix = "Con số may mắn hôm nay: ";

Display* s_display = nullptr;
lv_obj_t* s_panel = nullptr;
lv_obj_t* s_label = nullptr;
bool s_open = false;
FortuneSystem::FinishedCallback s_finished_cb = nullptr;
std::mutex s_mutex;

static const char* NextMessage() {
    constexpr size_t kCount = kMessages.size();
    static int last_index = -1;

    size_t index = 0;
    if (kCount <= 1) {
        index = 0;
    } else {
        do {
            index = static_cast<size_t>(esp_random() % kCount);
        } while (static_cast<int>(index) == last_index);
    }
    last_index = static_cast<int>(index);

    if ((esp_random() % 100U) < 12U) {
        static char lucky[64];
        const int value = 1 + static_cast<int>(esp_random() % 100U);
        std::snprintf(lucky, sizeof(lucky), "%s%d.", kLuckyPrefix, value);
        return lucky;
    }

    return kMessages[index];
}

static void LabelXAnimCb(void* var, int32_t v) {
    lv_obj_set_x(static_cast<lv_obj_t*>(var), v);
}

static void EnsurePanelLocked() {
    if (s_panel != nullptr || s_display == nullptr) {
        return;
    }

    s_panel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_panel, MESSAGE_PANEL_SIZE, MESSAGE_PANEL_SIZE);
    lv_obj_center(s_panel);
    lv_obj_set_style_radius(s_panel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    s_label = lv_label_create(s_panel);
    lv_obj_set_style_text_color(s_label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_vn_22, 0);
    lv_label_set_long_mode(s_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_label, LV_SIZE_CONTENT);
    lv_obj_set_height(s_label, LV_SIZE_CONTENT);
}

static void StartMarqueeLocked(const char* text) {
    if (s_panel == nullptr || s_label == nullptr) {
        return;
    }

    lv_anim_del(s_label, LabelXAnimCb);
    lv_label_set_text(s_label, text);
    lv_obj_update_layout(s_label);

    const int16_t panel_w = lv_obj_get_width(s_panel);
    const int16_t panel_h = lv_obj_get_height(s_panel);
    const int16_t label_w = lv_obj_get_width(s_label);
    const int16_t label_h = lv_obj_get_height(s_label);
    const int16_t center_y = static_cast<int16_t>((panel_h - label_h) / 2);
    const int32_t start_x = panel_w;
    const int32_t end_x = -label_w;
    uint32_t duration = static_cast<uint32_t>((panel_w + label_w) * 1000U / MESSAGE_SCROLL_PX_PER_SEC);
    duration = std::max(MESSAGE_MIN_DURATION_MS, std::min(MESSAGE_MAX_DURATION_MS, duration));

    lv_obj_set_y(s_label, center_y);
    lv_obj_set_x(s_label, start_x);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_label);
    lv_anim_set_exec_cb(&anim, LabelXAnimCb);
    lv_anim_set_values(&anim, start_x, end_x);
    lv_anim_set_time(&anim, duration);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_set_ready_cb(&anim, FinishAnimationCb);
    lv_anim_start(&anim);
}

}  // namespace

static void FinishAnimationCb(lv_anim_t* a) {
    (void)a;
    FortuneSystem::Close(true);
}

namespace FortuneSystem {

void Begin(Display* display) {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_display = display;
    if (s_display == nullptr) {
        return;
    }
    DisplayLockGuard ui_lock(s_display);
    EnsurePanelLocked();
}

void Open(FinishedCallback on_finished) {
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_display == nullptr) {
        return;
    }

    s_finished_cb = on_finished;

    DisplayLockGuard ui_lock(s_display);
    EnsurePanelLocked();
    if (s_panel == nullptr || s_label == nullptr) {
        return;
    }

    s_open = true;
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_panel);
    StartMarqueeLocked(NextMessage());
}

void Close(bool invoke_callback) {
    FinishedCallback cb = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_display != nullptr && s_label != nullptr) {
            DisplayLockGuard ui_lock(s_display);
            lv_anim_del(s_label, LabelXAnimCb);
            if (s_panel != nullptr) {
                lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
            }
        }
        s_open = false;
        if (invoke_callback) {
            cb = s_finished_cb;
        }
        s_finished_cb = nullptr;
    }

    if (cb != nullptr) {
        cb();
    }
}

bool IsOpen() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_open;
}

bool HandleTap(uint16_t x, uint16_t y) {
    (void)x;
    (void)y;
    if (!IsOpen()) {
        return false;
    }
    Close(true);
    return true;
}

bool HandleLongPress(uint16_t x, uint16_t y) {
    (void)x;
    (void)y;
    if (!IsOpen()) {
        return false;
    }
    Close(true);
    return true;
}

}  // namespace FortuneSystem
