#include "stdlib/lv_string.h"
#include "vitrolinha/storage.h"
#include "vitrolinha/track.h"
#include "zephyr/device.h"
#include <vitrolinha/display.h>
#include <vitrolinha/storage_fake.h>

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define ENCODER_SIM DT_NODELABEL(lvgl_keypad)

// For testing fps and board throughput
/* #define TEST_FPS */

typedef struct {
    lv_obj_t *label;
    lv_obj_t *bar;
} cb_data;

// Timer for music progress animation
static lv_timer_t *bar_timer;

// Encoder
static const struct device *const encoder_dev = DEVICE_DT_GET(ENCODER_SIM);

LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

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
    screen_init();
    app();
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

static void reset_bar_timer(lv_obj_t *bar) {

    lv_timer_reset(bar_timer);
    lv_timer_pause(bar_timer);
    lv_bar_set_value(bar, 0, false);
}

static void bar_timer_cb(lv_timer_t *timer) {

    lv_obj_t *bar = lv_timer_get_user_data(timer);
    int32_t current_value = lv_bar_get_value(bar);
    if (current_value + 5 > 100) {
        reset_bar_timer(bar);
        return;
    }
    lv_bar_set_value(bar, current_value + 5, true);
}
#define BUFFER_SIZE 64
static void event_cb(lv_event_t *e) {

    cb_data *data = lv_event_get_user_data(e);
    lv_obj_t *music_label = data->label;
    lv_obj_t *bar = data->bar; // TODO reflect real music state

    lv_obj_t *roller = lv_event_get_target_obj(e);
    lv_indev_t *indev = lv_indev_get_act();

    char buf[BUFFER_SIZE];

    const char playing_music[BUFFER_SIZE];
    const char *text_ref = lv_label_get_text(music_label);
    const int32_t size_title = lv_strnlen(text_ref, BUFFER_SIZE);
    lv_strlcpy(playing_music, text_ref, BUFFER_SIZE);

    LOG_INF("Key entered: %d", lv_indev_get_key(indev));
    /* LOG_INF("Current Music: %s", lv_label_get_text(music_label)); */

    if (lv_indev_get_key(indev) == LV_KEY_ENTER) {

        lv_roller_get_selected_str(roller, buf, sizeof(buf));
        LOG_INF("Buffer: %s\n", buf);
        /* LV_LOG_USER("roller: selected %u (\"%s\")", */
        /*             (unsigned)lv_roller_get_selected(roller), buf); */

        lv_label_set_text_fmt(music_label, "%s", buf);

        LOG_INF("Buffer after: %s\n", playing_music);

        if (!bar_timer) {

            bar_timer = lv_timer_create(bar_timer_cb, 500, bar);
            return;
        }

        if (lv_strcmp(playing_music, buf)) {

            reset_bar_timer(bar);
        }

        if (lv_timer_get_paused(bar_timer)) {
            lv_timer_resume(bar_timer);
        } else {

            lv_timer_pause(bar_timer);
        }
    }
}

static int get_name_content(char *name,
                            uint8_t **content) { // TODO use real data

    struct track_meta trackMeta;
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

    name[lv_strnlen(name, 1000) - 1] =
        '\0'; // TODO remove this part to use real music data from sd card

    return 0;
}

static lv_obj_t *create_flex_container(lv_obj_t *parent) {

    lv_obj_t *container = lv_obj_create(parent);

    lv_obj_set_width(container, LV_HOR_RES - 5);
    lv_obj_set_height(container, 25);

    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(container, 3, 0);

    return container;
}

static void screen_init() {

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(screen, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(screen, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(screen, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(screen, 3, 0);
}

// TODO after memory card support this function will change. Some parts will
// need to run inside main loop
// TODO bug where scroll animation stops when music is selected too fast.
// Solution: event queue or delay (depends if we'll implement mp3 to PCM
// decoder).
static void app(void) {

    static lv_style_t style_roller_main;
    static lv_style_t style_roller_selected;
    static lv_style_t style_bar;
    static lv_style_t style_container;

    static char names[1000]; // TODO change allocation (used only for tests)
    LOG_INF("Name: %s", names);

    static bool inited = false;

    if (!inited) {

        uint8_t *content[STORAGE_FILE_MAX];
        if (get_name_content(names, content)) {
            LOG_ERR("File too large");
        }

        lv_style_init(&style_roller_main);
        lv_style_set_bg_color(&style_roller_main, lv_color_hex(0xffffff));
        lv_style_set_radius(&style_roller_main, 12);
        lv_style_set_border_opa(&style_roller_main, 0);
        lv_style_set_text_color(&style_roller_main, lv_color_hex(0x000000));
        lv_style_set_text_line_space(&style_roller_main, 2);

        lv_style_init(&style_roller_selected);
        lv_style_set_bg_color(&style_roller_selected, lv_color_hex(0x000000));
        lv_style_set_text_color(&style_roller_selected, lv_color_hex(0xffffff));
        /* lv_style_set_text_font(&style_roller_selected, &font_example_large);
         */

        lv_style_init(&style_bar);
        lv_style_set_radius(&style_bar, 12);

        lv_style_init(&style_container);
        lv_style_set_border_opa(&style_container, 0);

        inited = true;
    }

    lv_obj_t *screen = lv_screen_active();
    /* 💡 Bump `text_line_space` on `style_roller_main` to grow the selected
     * band's height — the indicator always fills the gap between rows. */
    lv_obj_t *roller = lv_roller_create(screen);
    lv_obj_set_width(roller, LV_HOR_RES - 10);
    lv_obj_set_height(roller, LV_VER_RES * 0.55);
    lv_roller_set_visible_row_count(roller, 2);
    lv_roller_set_options(roller, names, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(roller, 2, false);
    lv_obj_add_style(roller, &style_roller_main, LV_PART_MAIN);
    lv_obj_add_style(roller, &style_roller_selected, LV_PART_SELECTED);

    lv_obj_t *container = create_flex_container(screen);
    lv_obj_add_style(container, &style_container, LV_PART_MAIN);
    // Music label in the bottom
    lv_obj_t *music_label = lv_label_create(container);
    lv_obj_set_style_text_font(music_label, &lv_font_unscii_8, 0);
    lv_label_set_text(music_label, "Nothing Playing");

    /* Add roller into indev_group to get input from key by making it focused */
    lv_group_t *group = lv_group_create();

    lv_indev_set_group(lvgl_input_get_indev(encoder_dev), group);

    lv_group_add_obj(group, roller);
    lv_group_focus_obj(roller);

    lv_obj_t *bar = lv_bar_create(container);
    lv_obj_set_size(bar, lv_pct(90), 8);
    lv_bar_set_min_value(bar, 0);
    lv_bar_set_max_value(bar, 100);
    lv_obj_add_style(bar, &style_bar, LV_PART_MAIN);

    // Create struct for passing data to event callback linked to roller
    cb_data data = {music_label, bar};

    lv_obj_add_event_cb(roller, event_cb, LV_EVENT_KEY,
                        &data); // Has to be after label to get user data
}
