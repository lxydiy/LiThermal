#include <my_main.h>

// 右上角电量卡片：电池图标 + 图标内百分比
#define BATTERY_CARD_X        (-12) // 相对右上角的 x 偏移，负值越大越靠左
#define BATTERY_CARD_SHOW_Y   2     // 完全可见
#define BATTERY_CARD_HIDE_Y   (-32)
#define BATTERY_CARD_WIDTH    64    // 左侧预留充电标识的位置
#define BATTERY_CARD_HEIGHT   26

// 电池图标尺寸
#define BATTERY_BODY_W       46
#define BATTERY_BODY_H       20
#define BATTERY_BORDER       2
#define BATTERY_CAP_W        3
#define BATTERY_CAP_H        10
#define BATTERY_CARD_PAD     4    // 内容距卡片左边缘
// 充电标识位于电池图标左侧，插入充电器显示、拔出后隐藏
#define BATTERY_BOLT_W       12
#define BATTERY_BOLT_GAP     3
#define BATTERY_BOLT_X       BATTERY_CARD_PAD
#define BATTERY_BODY_X       (BATTERY_BOLT_X + BATTERY_BOLT_W + BATTERY_BOLT_GAP)
#define BATTERY_CAP_X        (BATTERY_BODY_X + BATTERY_BODY_W)

// 电量颜色阈值：低于该百分比显示红色，否则绿色
#define BATTERY_LOW_THRESHOLD 30

// 电压范围（mV）：3.0V = 0%, 4.2V = 100%
#define BATTERY_VOLT_EMPTY   3000
#define BATTERY_VOLT_FULL    4200

// 采样平滑与确认次数，抑制 ADC 噪声导致的百分比跳动
#define BATTERY_SAMPLE_ALPHA   0.25f
#define BATTERY_CONFIRM_COUNT  2

extern "C" const lv_img_dsc_t bolt;

static MyCard card_Battery;
static lv_obj_t *img_bolt = NULL;       // 充电闪电图标
static lv_obj_t *battery_fill = NULL;   // 电池内部填充（宽度/颜色随电量变化）
static lv_obj_t *lbl_percent = NULL;    // 图标内百分比文字
static bool expanded = false;

/// @brief 电压(mV)转电量百分比(0~100)
static int voltageToPercent(int16_t voltage)
{
    if (voltage <= 0)
        return 0;
    int percent = (voltage - BATTERY_VOLT_EMPTY) * 100 / (BATTERY_VOLT_FULL - BATTERY_VOLT_EMPTY);
    if (percent < 0)
        percent = 0;
    else if (percent > 100)
        percent = 100;
    return percent;
}

static void battery_card_construct(lv_obj_t *parent)
{
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(parent, 128, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(parent, 0, 0);

    // 电池外壳（仅边框，透明背景）
    lv_obj_t *battery_body = lv_obj_create(parent);
    lv_obj_clear_flag(battery_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(battery_body, BATTERY_BODY_W, BATTERY_BODY_H);
    lv_obj_set_style_border_width(battery_body, BATTERY_BORDER, 0);
    lv_obj_set_style_border_color(battery_body, lv_color_white(), 0);
    lv_obj_set_style_radius(battery_body, 3, 0);
    lv_obj_set_style_bg_opa(battery_body, 0, 0);
    lv_obj_set_style_pad_all(battery_body, 0, 0);
    lv_obj_align(battery_body, LV_ALIGN_LEFT_MID, BATTERY_BODY_X, 0);

    // 电池内部填充
    battery_fill = lv_obj_create(battery_body);
    lv_obj_clear_flag(battery_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(battery_fill, 0, BATTERY_BODY_H - BATTERY_BORDER * 2);
    lv_obj_set_style_radius(battery_fill, 1, 0);
    lv_obj_set_style_border_width(battery_fill, 0, 0);
    lv_obj_set_style_pad_all(battery_fill, 0, 0);
    lv_obj_set_style_bg_color(battery_fill, lv_color_hex(0x00C853), 0); // 默认绿色
    lv_obj_set_style_bg_opa(battery_fill, LV_OPA_COVER, 0);
    lv_obj_set_pos(battery_fill, 0, 0);

    // 图标内百分比（放在填充层之上）
    lbl_percent = lv_label_create(battery_body);
    lv_obj_set_style_text_font(lbl_percent, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_percent, lv_color_white(), 0);
    lv_label_set_text(lbl_percent, "0%");
    lv_obj_center(lbl_percent);

    // 电池正极帽
    lv_obj_t *battery_cap = lv_obj_create(parent);
    lv_obj_clear_flag(battery_cap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(battery_cap, BATTERY_CAP_W, BATTERY_CAP_H);
    lv_obj_set_style_border_width(battery_cap, 0, 0);
    lv_obj_set_style_radius(battery_cap, 1, 0);
    lv_obj_set_style_pad_all(battery_cap, 0, 0);
    lv_obj_set_style_bg_color(battery_cap, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(battery_cap, LV_OPA_COVER, 0);
    lv_obj_align(battery_cap, LV_ALIGN_LEFT_MID, BATTERY_CAP_X, 0);

    // 充电标识（位于电池图标左侧；插入充电器显示，拔出后隐藏）
    img_bolt = lv_img_create(parent);
    lv_img_set_src(img_bolt, &bolt);
    lv_obj_align(img_bolt, LV_ALIGN_LEFT_MID, BATTERY_BOLT_X, 0);
    lv_obj_set_style_img_recolor(img_bolt, lv_color_hex(0xFFD600), 0);
    lv_obj_set_style_img_recolor_opa(img_bolt, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(img_bolt, 0, 0);
}

static void battery_card_create()
{
    if (card_Battery.obj == NULL || lv_obj_is_valid(card_Battery.obj) == false)
    {
        card_Battery.create(lv_layer_sys(), BATTERY_CARD_X, BATTERY_CARD_HIDE_Y, BATTERY_CARD_WIDTH, BATTERY_CARD_HEIGHT, LV_ALIGN_TOP_RIGHT);
        card_Battery.show(CARD_ANIM_NONE);
        battery_card_construct(card_Battery.obj);
    }
}

void battery_card_check()
{
    static int cnt = 0;
    static bool last_charging = false;
    static int last_percent = -1;
    static float smooth_voltage = -1.0f;
    static int pending_percent = -1;
    static int pending_count = 0;

    if (expanded == false)
    {
        expanded = true;
        LOCKLV();
        battery_card_create();
        card_Battery.move(BATTERY_CARD_X, BATTERY_CARD_SHOW_Y);
        UNLOCKLV();
        cnt = 20;
    }
    ++cnt;
    if (cnt >= 20)
    {
        int16_t voltage = PowerManager_getBatteryVoltage();
        if (voltage > 0)
        {
            // 指数平滑，抑制 ADC 噪声
            if (smooth_voltage < 0)
                smooth_voltage = (float)voltage;
            else
                smooth_voltage += ((float)voltage - smooth_voltage) * BATTERY_SAMPLE_ALPHA;

            int percent = voltageToPercent((int16_t)(smooth_voltage + 0.5f));

            // 连续多次采样一致后再刷新，避免数值来回跳动
            if (percent == last_percent)
            {
                pending_percent = percent;
                pending_count = 0;
            }
            else if (percent == pending_percent)
            {
                ++pending_count;
            }
            else
            {
                pending_percent = percent;
                pending_count = 1;
            }

            if (last_percent < 0 || pending_count >= BATTERY_CONFIRM_COUNT)
            {
                last_percent = percent;
                pending_count = 0;

                LOCKLV();
                // 内容区宽度 = BATTERY_BODY_W - 2*BORDER
                int fill_max = BATTERY_BODY_W - BATTERY_BORDER * 2;
                int fill_w = fill_max * percent / 100;
                lv_obj_set_width(battery_fill, fill_w);
                // 颜色：低电量红色，其余绿色
                lv_color_t color = (percent < BATTERY_LOW_THRESHOLD)
                                       ? lv_color_hex(0xFF1744)
                                       : lv_color_hex(0x00C853);
                lv_obj_set_style_bg_color(battery_fill, color, 0);
                lv_label_set_text_fmt(lbl_percent, "%d%%", percent);
                UNLOCKLV();
            }
        }

        bool charging = PowerManager_isCharging();
        if (charging != last_charging)
        {
            last_charging = charging;
            LOCKLV();
            if (charging)
            {
                // 插入充电器：显示充电标识
                lv_obj_clear_flag(img_bolt, LV_OBJ_FLAG_HIDDEN);
                lv_obj_fade_in(img_bolt, 500, 0);
            }
            else
            {
                // 拔出充电器：隐藏充电标识
                lv_obj_fade_out(img_bolt, 300, 0);
            }
            UNLOCKLV();
        }
        cnt = 0;
    }
}
