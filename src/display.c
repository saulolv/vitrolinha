/**
 * @file display.c
 * @brief Interface no OLED: biblioteca no roller, faixa e progresso abaixo.
 *
 * A lista sai de @ref storage_scan e a barra, de @ref snapshot_read. Nada aqui
 * sabe quem responde ao @ref storage.h — cartão na placa, faixas embutidas ou
 * pasta do computador no simulador — nem se há um player de verdade do outro
 * lado do instantâneo.
 */

#include <vitrolinha/cmd.h>
#include <vitrolinha/display.h>
#include <vitrolinha/snapshot.h>
#include <vitrolinha/storage.h>
#include <vitrolinha/track.h>

#include <lvgl.h>
#include <lvgl_input_device.h>
#include <lvgl_zephyr.h>

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------- */
/* Encoder                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Nó do Devicetree do dispositivo de entrada que dirige o grupo.
 *
 * Escolhido pelo que o devicetree declara, não pelo nome da placa:
 * - no simulador, o overlay declara um keypad que entrega as setas e o Enter
 *   do teclado do computador;
 * - na placa, o `lvgl_encoder_input` do app.overlay, que recebe o
 *   `INPUT_REL_WHEEL` do gpio-qdec e o clique do encoder.
 */
#if DT_NODE_EXISTS(DT_NODELABEL(lvgl_keypad))
#define INPUT_NODE DT_NODELABEL(lvgl_keypad)
#else
#define INPUT_NODE DT_NODELABEL(lvgl_encoder_input)
#endif

/** Dispositivo de entrada associado ao grupo de foco. */
static const struct device *const input_dev = DEVICE_DT_GET(INPUT_NODE);

/* -------------------------------------------------------------------------- */
/* Configuração                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Habilita o teste de desempenho do display.
 *
 * Quando definido, substitui a interface principal por uma tela de
 * demonstração que invalida periodicamente a tela inteira.
 */
/* #define TEST_FPS */

/** Escala da barra: a mesma de @ref snapshot_progress_permille. */
#define BAR_MAX 1000

/**
 * @brief Tamanho das opções do roller.
 *
 * Cada faixa ocupa no máximo o nome (até TRACK_NAME_MAX - 1 bytes) e o
 * `\n`, ou seja, TRACK_NAME_MAX. Mais o terminador.
 */
#define ROLLER_OPTIONS_SIZE (LIBRARY_MAX * TRACK_NAME_MAX + 1U)

/* -------------------------------------------------------------------------- */
/* Estado global                                                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Dispositivo de display definido pelo nó chosen do Devicetree.
 *
 * O dispositivo é obtido através da propriedade `zephyr,display`.
 */
static const struct device *const display_dev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

#ifndef TEST_FPS

/** Biblioteca, na ordem de @ref storage_scan. Tamanho fixo: 32 faixas. */
static struct track_meta library[LIBRARY_MAX];

/** Quantas posições de @ref library valem. Zero se a varredura falhou. */
static size_t library_count;

/** Erro da varredura, ou 0. Decide o texto do roller quando ele fica vazio. */
static int library_error;

/** Rótulo com o nome da faixa do instantâneo. */
static lv_obj_t *music_label;

/** Barra de progresso, de 0 a ::BAR_MAX. */
static lv_obj_t *bar;

/** Faixa mostrada no rótulo, para só reescrevê-lo quando ela mudar. */
static int16_t shown_track = TRACK_NONE;

static void screen_init(void);
static void app(void);

#endif /* !TEST_FPS */

/* -------------------------------------------------------------------------- */
/* Medição de desempenho                                                      */
/* -------------------------------------------------------------------------- */

#ifdef TEST_FPS

/**
 * @brief Rótulo utilizado para apresentar o contador de quadros.
 */
static lv_obj_t *frame_label;

/** Número de atualizações solicitadas desde a inicialização. */
static uint32_t frame_counter;

/** Número de eventos de flush contabilizados na janela atual. */
static uint32_t fps_frame_count;

/** Instante de início da janela de medição, em milissegundos. */
static int64_t fps_window_start_ms;

/**
 * @brief Contabiliza a conclusão de operações de flush do display.
 *
 * O evento `LV_EVENT_FLUSH_FINISH` é emitido quando uma operação de flush
 * é concluída. Essa medição permite acompanhar a frequência de conclusão
 * das operações de atualização do display.
 *
 * A métrica corresponde à quantidade de flushes por segundo. Ela representa
 * diretamente o FPS somente quando cada atualização de tela resulta em
 * exatamente um flush.
 *
 * O callback é executado no contexto de processamento do LVGL. Na
 * configuração utilizada, a rotina de trabalho do LVGL já mantém o lock
 * durante o processamento dos eventos. Portanto, não se deve adquirir
 * novamente o mesmo lock neste callback, evitando possível deadlock.
 *
 * @param e Evento recebido do LVGL.
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

    /* Atualiza a medição aproximadamente a cada segundo. */
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
 * @brief Atualiza o contador da tela de demonstração.
 *
 * Incrementa o contador, atualiza o rótulo correspondente e invalida a
 * tela ativa para solicitar uma nova renderização.
 *
 * O callback é executado durante o processamento dos timers do LVGL.
 * Portanto, na configuração utilizada, o lock do LVGL já está adquirido.
 *
 * @param timer Timer do LVGL que disparou o callback.
 */
static void display_demo_timer_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);

    frame_counter++;
    lv_label_set_text_fmt(frame_label, "quadro %u", frame_counter);
    lv_obj_invalidate(lv_screen_active());
}

#endif /* TEST_FPS */

/* -------------------------------------------------------------------------- */
/* Biblioteca                                                                 */
/* -------------------------------------------------------------------------- */

#ifndef TEST_FPS

/**
 * @brief Varre a biblioteca pelo contrato do @ref storage.h.
 *
 * Fora do lock do LVGL: na placa a varredura lê o cartão, e o laço do LVGL
 * não tem por que esperar por ela.
 */
static void library_load(void) {
    size_t skipped = 0;
    int count = storage_scan(library, ARRAY_SIZE(library), &skipped);

    if (count < 0) {
        LOG_WRN("Biblioteca indisponivel: %s (%d)", storage_error_text(count),
                count);
        library_count = 0;
        library_error = count;
        return;
    }

    library_count = (size_t)count;
    library_error = 0;

    if (skipped > 0U) {
        LOG_WRN("Biblioteca cheia: %zu arquivo(s) ignorado(s)", skipped);
    }
}

/**
 * @brief Monta as opções do roller, uma faixa por linha.
 *
 * Biblioteca vazia vira uma única linha explicando o porquê.
 *
 * O marcador `!` da faixa rejeitada ainda não entra: pela especificação ele
 * vem da carga recusada (o `load_error` do instantâneo), junto com a tela de
 * erro, e não da varredura.
 *
 * @param out Destino de ::ROLLER_OPTIONS_SIZE bytes.
 */
static void library_options(char *out) {
    size_t used = 0;

    out[0] = '\0';

    if (library_count == 0U) {
        const char *why = (library_error != 0)
                              ? storage_error_text(library_error)
                              : "Biblioteca vazia";

        strncpy(out, why, ROLLER_OPTIONS_SIZE - 1U);
        out[ROLLER_OPTIONS_SIZE - 1U] = '\0';
        return;
    }

    for (size_t i = 0; i < library_count; i++) {
        /* track_meta_init garante o terminador dentro de TRACK_NAME_MAX. */
        size_t len = strlen(library[i].name);

        if (i > 0U) {
            out[used++] = '\n';
        }

        memcpy(&out[used], library[i].name, len);
        used += len;
    }

    out[used] = '\0';
}

/* -------------------------------------------------------------------------- */
/* Faixa e progresso                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Copia o instantâneo do player para o rótulo e a barra.
 *
 * Roda a taxa fixa, pelo timer do LVGL — com o lock do LVGL já adquirido —,
 * e nunca é acordada pelo player. O rótulo só é reescrito quando a faixa
 * muda; a barra o LVGL já ignora quando o valor é o mesmo.
 *
 * @param timer Timer do LVGL que disparou o callback.
 */
static void refresh_timer_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);

    struct player_snapshot snap;
    uint64_t now_us = k_ticks_to_us_floor64(k_uptime_ticks());

    snapshot_read(&snap);

    /* Rastro de bancada: cada publicação nova do player sai no console. Daqui,
     * e não do player, porque é a thread de baixa prioridade que já lê o
     * instantâneo. O decorrido não mora no instantâneo — sai de base_us e do
     * relógio —, então o registro só muda quando o player publica algo novo.
     */
    static struct player_snapshot logged = SNAPSHOT_IDLE_INITIALIZER;

    if (memcmp(&snap, &logged, sizeof(snap)) != 0) {
        LOG_INF("Player: estado %d, faixa %d, %u/%u ms, erro %d (linha %u)",
                snap.state, snap.track,
                snapshot_elapsed_us(&snap, now_us) / 1000U,
                snap.total_us / 1000U, snap.load_error, snap.error_line);
        logged = snap;
    }

    lv_bar_set_value(bar, snapshot_progress_permille(&snap, now_us),
                     LV_ANIM_OFF);

    if (snap.track == shown_track) {
        return;
    }

    shown_track = snap.track;

    if ((snap.track >= 0) && ((size_t)snap.track < library_count)) {
        lv_label_set_text(music_label, library[snap.track].name);
    } else {
        lv_label_set_text(music_label, "Nada tocando");
    }
}

/* -------------------------------------------------------------------------- */
/* Eventos de interação                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Clique no roller: toca a faixa selecionada.
 *
 * Não muda nada na tela. Publica ::CMD_SELECT, e o que aparece no rótulo e
 * na barra é o que o player publicar no instantâneo em resposta — a tela
 * mostra o que está tocando, não o que foi pedido.
 *
 * Reabre o modo de edição do grupo: com o encoder, o clique o encerra, e sem
 * ele o giro passaria a mover o foco em vez da seleção do roller.
 *
 * @param e Evento de clique recebido do LVGL.
 */
static void roller_clicked_cb(lv_event_t *e) {
    lv_obj_t *roller = lv_event_get_target_obj(e);
    lv_group_t *g = lv_obj_get_group(roller);
    uint32_t selected = lv_roller_get_selected(roller);
    int err;

    if (g != NULL) {
        lv_group_set_editing(g, true);
    }

    if (selected >= library_count) {
        return;
    }

    err = cmd_post(CMD_SELECT, (int16_t)library[selected].index);
    if (err != 0) {
        LOG_WRN("Selecao da faixa %u descartada: %d", library[selected].index,
                err);
    }
}

#endif /* !TEST_FPS */

/* -------------------------------------------------------------------------- */
/* Inicialização do display                                                  */
/* -------------------------------------------------------------------------- */

int display_init(void) {
    int err = 0;

    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display nao esta pronto");
        err = -ENODEV;
        goto END;
    }

#ifndef TEST_FPS
    library_load();
#endif

    /*
     * A função pode ser executada em uma thread concorrente com a thread
     * responsável pelo processamento periódico do LVGL.
     *
     * O lock protege a criação e configuração dos objetos durante essa
     * concorrência.
     */
    lvgl_lock();

#ifdef TEST_FPS

    /* Cria o título da tela de demonstração. */
    lv_obj_t *title = lv_label_create(lv_screen_active());
    lv_label_set_text(title, "Vitrola");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    /* Cria o rótulo utilizado para apresentar o contador de quadros. */
    frame_label = lv_label_create(lv_screen_active());
    lv_label_set_text(frame_label, "quadro 0");
    lv_obj_align(frame_label, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* Registra o callback para eventos de conclusão de flush. */
    lv_display_add_event_cb(lv_display_get_default(), display_flush_event_cb,
                            LV_EVENT_FLUSH_FINISH, NULL);

    /* Agenda a invalidação periódica da tela. */
    lv_timer_create(display_demo_timer_cb, DISPLAY_DEMO_PERIOD_MS, NULL);

    /*
     * Processa as tarefas pendentes do LVGL para solicitar a renderização
     * do primeiro quadro antes de desativar o blanking do display.
     */
    lv_timer_handler();

#else

    /* Inicializa a tela e os componentes da aplicação. */
    screen_init();
    app();

#endif

    lvgl_unlock();

    /* Solicita a desativação do blanking do display. */
    err = display_blanking_off(display_dev);

    if (err != 0 && err != -ENOSYS) {
        LOG_ERR("Nao consegui desligar o blanking do display: %d", err);
        goto END;
    }

    err = 0;
    LOG_INF("Display no ar (%s)", display_dev->name);

END:
    return err;
}

#ifndef TEST_FPS

/* -------------------------------------------------------------------------- */
/* Construção dos componentes da interface                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Cria um container com layout flexível em coluna.
 *
 * Configura as dimensões, o alinhamento dos elementos filhos e o
 * espaçamento entre linhas. A rolagem e a barra de rolagem são desabilitadas.
 *
 * @param parent Objeto pai do container.
 *
 * @return Ponteiro para o objeto container criado.
 */
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

/**
 * @brief Configura o layout da tela principal.
 *
 * Desabilita a rolagem e configura o layout flexível em coluna, centralizando
 * os elementos filhos nos eixos principal, transversal e das linhas.
 */
static void screen_init(void) {
    lv_obj_t *screen = lv_screen_active();

    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);

    lv_obj_set_style_flex_main_place(screen, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(screen, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(screen, LV_FLEX_ALIGN_CENTER, 0);

    lv_obj_set_style_pad_row(screen, 3, 0);
}

/* -------------------------------------------------------------------------- */
/* Aplicação principal                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Inicializa os componentes da interface principal.
 *
 * Cria e configura os estilos, o roller de seleção de músicas, o grupo
 * de entrada, o rótulo da música e a barra de progresso.
 *
 * Os estilos e as opções do roller possuem duração estática, permitindo que
 * o LVGL mantenha referências a esses objetos durante a execução da
 * interface.
 *
 * @todo Revarrer a biblioteca quando o cartão entrar ou sair (issues #29 e
 *       #30). Hoje a lista é a do boot.
 */
static void app(void) {
    /* Estilos utilizados pelos componentes da interface. */
    static lv_style_t style_roller_main;
    static lv_style_t style_roller_selected;
    static lv_style_t style_bar;
    static lv_style_t style_container;

    /* Nomes das faixas, um por linha. Tamanho fixo, como a biblioteca. */
    static char options[ROLLER_OPTIONS_SIZE];

    static bool inited = false;

    library_options(options);

    if (!inited) {
        /* Estilo principal do roller. */
        lv_style_init(&style_roller_main);
        lv_style_set_bg_color(&style_roller_main, lv_color_hex(0xffffff));
        lv_style_set_radius(&style_roller_main, 12);
        lv_style_set_border_opa(&style_roller_main, 0);
        lv_style_set_text_color(&style_roller_main, lv_color_hex(0x000000));
        lv_style_set_text_line_space(&style_roller_main, 2);

        /* Estilo da opção selecionada. */
        lv_style_init(&style_roller_selected);
        lv_style_set_bg_color(&style_roller_selected, lv_color_hex(0x000000));
        lv_style_set_text_color(&style_roller_selected, lv_color_hex(0xffffff));

        /* Estilo da barra de progresso. */
        lv_style_init(&style_bar);
        lv_style_set_radius(&style_bar, 12);

        /* Estilo do container. */
        lv_style_init(&style_container);
        lv_style_set_border_opa(&style_container, 0);

        inited = true;
    }

    /* ---------------------------------------------------------------------- */
    /* Roller: seleção das faixas                                             */
    /* ---------------------------------------------------------------------- */

    lv_obj_t *screen = lv_screen_active();

    lv_obj_t *roller = lv_roller_create(screen);

    lv_obj_set_width(roller, LV_HOR_RES - 10);
    lv_obj_set_height(roller, LV_VER_RES * 0.55);

    lv_roller_set_visible_row_count(roller, 2);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(roller, 0, LV_ANIM_OFF);

    lv_obj_add_style(roller, &style_roller_main, LV_PART_MAIN);
    lv_obj_add_style(roller, &style_roller_selected, LV_PART_SELECTED);

    /* ---------------------------------------------------------------------- */
    /* Container inferior: informações da reprodução                         */
    /* ---------------------------------------------------------------------- */

    lv_obj_t *container = create_flex_container(screen);

    lv_obj_add_style(container, &style_container, LV_PART_MAIN);

    /* Rótulo que apresenta o nome da faixa do instantâneo. */
    music_label = lv_label_create(container);

    lv_obj_set_style_text_font(music_label, &lv_font_unscii_8, 0);
    lv_label_set_text(music_label, "Nada tocando");

    /* ---------------------------------------------------------------------- */
    /* Grupo de entrada                                                       */
    /* ---------------------------------------------------------------------- */

    /*
     * Associa o dispositivo de entrada a um grupo LVGL.
     * O roller recebe o foco inicial e é configurado em modo de edição.
     */
    lv_group_t *group = lv_group_create();

    lv_indev_set_group(lvgl_input_get_indev(input_dev), group);

    lv_group_add_obj(group, roller);
    lv_group_focus_obj(roller);

    lv_group_set_editing(group, true);

    /* ---------------------------------------------------------------------- */
    /* Barra de progresso                                                     */
    /* ---------------------------------------------------------------------- */

    bar = lv_bar_create(container);

    lv_obj_set_size(bar, lv_pct(90), 8);

    lv_bar_set_range(bar, 0, BAR_MAX);

    lv_obj_add_style(bar, &style_bar, LV_PART_MAIN);

    /* ---------------------------------------------------------------------- */
    /* Registro dos callbacks                                                 */
    /* ---------------------------------------------------------------------- */

    lv_obj_add_event_cb(roller, roller_clicked_cb, LV_EVENT_CLICKED, NULL);

    lv_timer_create(refresh_timer_cb, DISPLAY_REFRESH_PERIOD_MS, NULL);
}

#endif /* !TEST_FPS */
