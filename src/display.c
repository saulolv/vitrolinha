#include "misc/lv_event.h"
#include "vitrolinha/storage.h"
#include "vitrolinha/track.h"
#include "widgets/roller/lv_roller.h"
#include "zephyr/device.h"
#include "zephyr/fatal_types.h"
#include <stdint.h>
#include <vitrolinha/display.h>
#include <vitrolinha/storage_fake.h>

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define ENCODER_SIM DT_NODELABEL(lvgl_keypad)

// Encoder
static const struct device *const encoder_dev = DEVICE_DT_GET(ENCODER_SIM);

LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

/* #define TEST_FPS */
/** @brief Dispositivo do display, resolvido pelo `chosen zephyr,display`. */
static const struct device *const display_dev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

/** @brief Rótulo com o contador de quadros, único elemento que muda por ciclo.
 */
static lv_obj_t *frame_label;

/** @brief Quadros desenhados desde o boot. Só a thread da fila do LVGL escreve.
 */
static uint32_t frame_counter;

/** @brief Quadros contados na janela de medição de taxa corrente. */
static uint32_t fps_frame_count;

/** @brief Início da janela de medição corrente, em `k_uptime_get()`. */
static int64_t fps_window_start_ms;

/**
 * @brief Conta um quadro real e relata a taxa medida a cada ~1 s de janela.
 *
 * `LV_EVENT_FLUSH_FINISH` dispara uma vez por flush de verdade que o driver
 * do display concluiu — a granularidade certa para medir o que o barramento
 * sustenta, e não quantas vezes `lv_timer_handler` rodou sem nada para
 * desenhar.
 *
 * Chamado pela fila de trabalho do LVGL, já dentro do lock interno que
 * `lvgl_timer_handler_work` toma — por isso não chama @ref lvgl_lock aqui,
 * o que causaria deadlock num mutex não reentrante.
 *
 * @param e Evento do LVGL. Não usado além de confirmar o disparo.
 */
static void display_flush_event_cb(lv_event_t *e) {
    ARG_UNUSED(e);

    int64_t now_ms = k_uptime_get();
    int64_t elapsed_ms;

    if (fps_window_start_ms == 0) {
        fps_window_start_ms = now_ms;
    }

    fps_frame_count++;
    elapsed_ms = now_ms - fps_window_start_ms;

    if (elapsed_ms >= 1000) {
        uint32_t fps_x10 = (uint32_t)((fps_frame_count * 10000) / elapsed_ms);

        LOG_INF("Taxa de quadros real: %u.%u fps (%u quadros em %u ms)",
                fps_x10 / 10, fps_x10 % 10, fps_frame_count,
                (uint32_t)elapsed_ms);

        fps_frame_count = 0;
        fps_window_start_ms = now_ms;
    }
}

/**
 * @brief Timer do LVGL: força um quadro cheio a cada ciclo.
 *
 * `lv_obj_invalidate` invalidando toda a screen_ative para estressar o
 * barramento (128x64 a 1 bpp = 1024 B, ~23 ms a 400 kHz). Medir a taxa real
 * neste caminho é o que confirma, ou não, que a configuração sustenta os
 * 10 fps do plano antes de a `ui` de verdade existir.
 *
 * Roda dentro de `lv_timer_handler`, portanto já sob o lock do LVGL: mesma
 * razão de @ref display_flush_event_cb não tomar o lock de novo.
 *
 * @param timer O timer do LVGL que disparou. Não usado.
 */
static void display_demo_timer_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);

    frame_counter++;
    lv_label_set_text_fmt(frame_label, "quadro %u", frame_counter);
    lv_obj_invalidate(lv_screen_active());
}

/**
 * @brief Traz o LVGL ao ar: desenha texto na tela e liga a medição de fps.
 *
 * @retval 0        Display no ar.
 * @retval -ENODEV  O dispositivo de display não inicializou.
 * @retval outro    Erro devolvido por `display_blanking_off`.
 */
int display_init(void) {
    lv_obj_t *title;
    int err;

    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display nao esta pronto");
        return -ENODEV;
    }

    lvgl_lock();
#ifdef TEST_FPS
    /*
     * `main` roda em thread própria, concorrente com a fila de trabalho do
     * LVGL que já está de pé (CONFIG_LV_Z_AUTO_INIT). Sem o lock, criar
     * objetos aqui correria com um `lv_timer_handler` no meio.
     */
    title = lv_label_create(lv_screen_active());
    lv_label_set_text(title, "Vitrola");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    frame_label = lv_label_create(lv_screen_active());
    lv_label_set_text(frame_label, "quadro 0");
    lv_obj_align(frame_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_display_add_event_cb(lv_display_get_default(), display_flush_event_cb,
                            LV_EVENT_FLUSH_FINISH, NULL);

    lv_timer_create(display_demo_timer_cb, DISPLAY_DEMO_PERIOD_MS, NULL);

    /* Renderiza o primeiro quadro antes de tirar do blanking.Sem isto, */
    /*     a tela liga mostrando o lixo do buffer até o primeiro tick da fila de
     */
    /*         trabalho — que pode vir depois de `display_blanking_off`. */

    lv_timer_handler();

#else
    lv_music_roller();
#endif

    lvgl_unlock();

    err = display_blanking_off(display_dev);
    if (err != 0 && err != -ENOSYS) {
        LOG_ERR("Nao consegui desligar o blanking do display: %d", err);
        return err;
    }

    LOG_INF("Display no ar (%s)", display_dev->name);

    return 0;
}

static void btn_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target_obj(e);
    if (code == LV_EVENT_CLICKED) {
        static uint8_t cnt = 0;
        cnt++;

        /*Get the first child of the button which is the label and change its
         * text*/
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        lv_label_set_text_fmt(label, "Button: %d", cnt);
    }
}

/**
 * @title Button with click counter
 * @brief Increment a label on a button each time it is clicked.
 *
 * A button sized 120x50 is placed at position (10, 10) on the active screen
 * with a centered label reading `Button`. The button subscribes to
 * `LV_EVENT_ALL` and on `LV_EVENT_CLICKED` the callback updates its child
 * label with `lv_label_set_text_fmt` to show an incrementing counter.
 */
void lv_example_get_started_button(void) {
    lv_obj_t *btn = lv_button_create(
        lv_screen_active());       /*Add a button the current screen*/
    lv_obj_set_pos(btn, 10, 10);   /*Set its position*/
    lv_obj_set_size(btn, 120, 50); /*Set its size*/
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_ALL,
                        NULL); /*Assign a callback to the button*/

    lv_obj_t *label = lv_label_create(btn); /*Add a label to the button*/
    lv_label_set_text(label, "Button");     /*Set the labels text*/
    lv_obj_center(label);
}

static void event_cb(lv_event_t *e) {

    lv_obj_t *music_label = lv_event_get_user_data(e);
    lv_obj_t *roller = lv_event_get_target_obj(e);
    lv_indev_t *indev = lv_indev_get_act();

    LOG_INF("Key entered: %d", lv_indev_get_key(indev));

    if (lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) {

        if (lv_indev_get_key(indev) == LV_KEY_ENTER) {

            char buf[32];
            lv_roller_get_selected_str(roller, buf, sizeof(buf));
            LV_LOG_USER("roller: selected %u (\"%s\")",
                        (unsigned)lv_roller_get_selected(roller), buf);

            lv_label_set_text_fmt(music_label, "%s", buf);
        }
    }
}

struct track_meta trackMeta;
static int get_name_content(char *name,
                            uint8_t **content) { // TODO use real data
    for (size_t i = 0; i < STORAGE_FAKE_TRACK_COUNT; i++) {
        int file_size = storage_load(i, content[i], STORAGE_FILE_MAX);
        if (file_size > (int)STORAGE_FILE_MAX) {
            return K_ERR_STACK_CHK_FAIL;
        }
        fake_storage_get_name(&trackMeta, (uint8_t)i);
        /* LOG_INF("Name: %s", trackMeta.name); */
        lv_strcat(name, trackMeta.name);
        lv_strcat(name, "\n");
    }
    return 0;
}

static void lv_music_roller(void) {

    lv_style_t style_roller_main;
    lv_style_t style_roller_selected;

    char names[1000]; // TODO change allocation (used only for tests)
    uint8_t *content[STORAGE_FILE_MAX];
    if (get_name_content(names, content)) {
        LOG_ERR("File too large");
    }
    LOG_INF("Name: %s", names);

    static bool inited = false;

    if (!inited) {
        lv_style_init(&style_roller_main);
        lv_style_set_bg_color(&style_roller_main, lv_color_hex(0xf3f4f6));
        lv_style_set_bg_opa(&style_roller_main, (255 * 100 / 100));
        lv_style_set_radius(&style_roller_main, 12);
        lv_style_set_border_color(&style_roller_main, lv_color_hex(0xd1d5db));
        lv_style_set_border_width(&style_roller_main, 1);
        lv_style_set_text_color(&style_roller_main, lv_color_hex(0x6b7280));
        lv_style_set_text_line_space(&style_roller_main, 3);

        lv_style_init(&style_roller_selected);
        lv_style_set_bg_color(&style_roller_selected, lv_color_hex(0x6366f1));
        lv_style_set_bg_opa(&style_roller_selected, (255 * 100 / 100));
        lv_style_set_text_color(&style_roller_selected, lv_color_hex(0xffffff));
        /* lv_style_set_text_font(&style_roller_selected, &font_example_large);
         */

        inited = true;
    }

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(screen, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(screen, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(screen, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(screen, 5, 0);

    /* 💡 Bump `text_line_space` on `style_roller_main` to grow the selected
     * band's height — the indicator always fills the gap between rows. */
    lv_obj_t *roller = lv_roller_create(screen);
    lv_obj_set_width(roller, LV_HOR_RES - 10);
    lv_roller_set_visible_row_count(roller, 3);
    lv_roller_set_options(roller, names, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(roller, 2, false);
    lv_obj_add_style(roller, &style_roller_main, LV_PART_MAIN);
    lv_obj_add_style(roller, &style_roller_selected, LV_PART_SELECTED);

    // Music label in the bottom
    lv_obj_t *music_label = lv_label_create(screen);
    lv_obj_align(music_label, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_font(music_label, &lv_font_unscii_8, 0);
    lv_label_set_text(music_label, "Nothing Playing");

    /* lv_obj_add_event_cb(roller, event_cb, LV_EVENT_VALUE_CHANGED, NULL); */
    lv_obj_add_event_cb(roller, event_cb, LV_EVENT_KEY,
                        music_label); // Has to be after label to get user data

    /* Add roller into indev_group to get input from key by making it focused */
    lv_group_t *group = lv_group_create();

    lv_indev_set_group(lvgl_input_get_indev(encoder_dev), group);

    lv_group_add_obj(group, roller);
    lv_group_focus_obj(roller);
}
