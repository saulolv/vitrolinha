#include "misc/lv_color.h"
#include "stdlib.h"
#include "stdlib/lv_string.h"

#include "vitrolinha/card.h"
#include "vitrolinha/display.h"
#include "vitrolinha/storage.h"
#include "vitrolinha/storage_fake.h"
#include "vitrolinha/track.h"

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* -------------------------------------------------------------------------- */
/* Encoder                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Nó do Devicetree associado ao dispositivo de entrada.
 *
 * A implementação selecionada depende da placa utilizada:
 * - native_sim: dispositivo de entrada LVGL configurado como keypad;
 * - ZBOOK: dispositivo de entrada LVGL configurado como encoder.
 */
#if defined(CONFIG_BOARD_NATIVE_SIM)

#define ENCODER_NODE DT_NODELABEL(lvgl_keypad)

#elif defined(CONFIG_BOARD_ZBOOK)

#define ENCODER_NODE DT_NODELABEL(lvgl_encoder)

#else
#error "Placa sem implementacao de encoder definida"
#endif

/** Dispositivo de entrada associado ao encoder. */
static const struct device *const encoder_dev = DEVICE_DT_GET(ENCODER_NODE);

/* -------------------------------------------------------------------------- */
/* Configuração                                                               */
/* -------------------------------------------------------------------------- */

LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

/** Tamanho dos buffers temporários utilizados para armazenar textos. */
#define BUFFER_SIZE 64

/**
 * @brief Habilita o teste de desempenho do display.
 *
 * Quando definido, substitui a interface principal por uma tela de
 * demonstração que invalida periodicamente a tela inteira.
 */
/* #define TEST_FPS */

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

/**
 * @brief Dados compartilhados pelos callbacks da interface.
 *
 * Armazena os objetos LVGL necessários para atualizar o nome da música
 * e a barra de progresso.
 */
typedef struct {
    /** Rótulo utilizado para apresentar o nome da música. */
    lv_obj_t *label;

    /** Barra utilizada para representar o progresso da reprodução. */
    lv_obj_t *bar;
} cb_data;

/** Indica se um evento de clique foi recebido. */
/* static bool clicked = false; */

/** Timer responsável pela animação da barra de progresso. */
static lv_timer_t *bar_timer;

/* -------------------------------------------------------------------------- */
/* Medição de desempenho                                                      */
/* -------------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------- */
/* Inicialização do display                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Inicializa a interface e habilita o display.
 *
 * Verifica a disponibilidade do dispositivo, inicializa os componentes
 * da interface e solicita a desativação do blanking do display.
 *
 * Quando `TEST_FPS` está definido, inicializa a tela de demonstração e
 * os mecanismos de medição de desempenho. Caso contrário, inicializa
 * a interface principal da aplicação.
 *
 * @retval 0        Inicialização concluída.
 * @retval -ENODEV  Dispositivo de display não está pronto.
 * @retval outro    Erro retornado por `display_blanking_off()`.
 */
int display_init(void) {
    int err = 0;

    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display nao esta pronto");
        err = -ENODEV;
        goto END;
    }

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
    title = lv_label_create(lv_screen_active());
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

    LOG_INF("Display no ar (%s)", display_dev->name);

END:
    return err;
}

/* -------------------------------------------------------------------------- */
/* Barra de progresso                                                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Reinicia a animação da barra de progresso.
 *
 * Reinicia e pausa o timer responsável pela animação e redefine o valor
 * da barra de progresso para zero.
 *
 * @param bar Objeto LVGL correspondente à barra de progresso.
 */
static void reset_bar_timer(lv_obj_t *bar) {
    lv_timer_reset(bar_timer);
    lv_timer_pause(bar_timer);
    lv_bar_set_value(bar, 0, false);
}

/**
 * @brief Atualiza o valor da barra de progresso.
 *
 * Incrementa o valor atual em cinco unidades. Quando o próximo incremento
 * ultrapassaria o limite de 100, reinicia e pausa a animação.
 *
 * @param timer Timer do LVGL responsável pela animação.
 */
static void bar_timer_cb(lv_timer_t *timer) {
    lv_obj_t *bar = lv_timer_get_user_data(timer);
    int32_t current_value = lv_bar_get_value(bar);

    if (current_value + 5 > 100) {
        reset_bar_timer(bar);
        return;
    }

    lv_bar_set_value(bar, current_value + 5, true);
}

/* -------------------------------------------------------------------------- */
/* Eventos do roller                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Processa a entrada de uma tecla no roller.
 *
 * Obtém a opção selecionada, atualiza o rótulo da música e controla o
 * timer responsável pela animação da barra de progresso.
 *
 * O processamento da seleção ocorre somente quando a variável `clicked`
 * está definida. Essa variável é atualizada pelo callback `clicked_cb`.
 *
 * @param e Evento de entrada recebido do LVGL.
 */
static void event_cb(lv_event_t *e) {
    cb_data *local_data = lv_event_get_user_data(e);

    lv_obj_t *music_label = local_data->label;
    lv_obj_t *bar =
        local_data->bar; /* TODO: refletir o estado real da música. */

    lv_obj_t *roller = lv_event_get_target_obj(e);
    lv_indev_t *indev = lv_indev_get_act();

    char buf[BUFFER_SIZE];
    char playing_music[BUFFER_SIZE];

    const char *text_ref = lv_label_get_text(music_label);

    /* Preserva o nome da música antes de atualizar o rótulo. */
    lv_strlcpy(playing_music, text_ref, BUFFER_SIZE);

    LOG_INF("Key entered: %d", lv_indev_get_key(indev));

    /* Obtém o texto da opção atualmente selecionada. */
    lv_roller_get_selected_str(roller, buf, sizeof(buf));

    LOG_INF("Buffer: %s\n", buf);

    /* Atualiza o rótulo com o nome da nova seleção. */
    lv_label_set_text_fmt(music_label, "%s", buf);

    LOG_INF("Buffer after: %s\n", playing_music);

    /*
     * Cria o timer na primeira seleção.
     * O retorno evita executar as operações seguintes nessa chamada.
     */
    if (!bar_timer) {
        bar_timer = lv_timer_create(bar_timer_cb, 500, bar);
        return;
    }

    /* Reinicia o progresso quando a seleção é alterada. */
    if (lv_strcmp(playing_music, buf)) {
        reset_bar_timer(bar);
    }

    /* Alterna o estado de execução do timer. */
    if (lv_timer_get_paused(bar_timer)) {
        lv_timer_resume(bar_timer);
    } else {
        lv_timer_pause(bar_timer);
    }
}

/* -------------------------------------------------------------------------- */
/* Armazenamento de faixas                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Obtém os nomes e conteúdos das faixas disponíveis.
 *
 * Percorre as faixas do armazenamento de teste, carrega seus conteúdos
 * e concatena os respectivos nomes em uma string, separados por quebras
 * de linha.
 *
 * A implementação utiliza o armazenamento falso e deverá ser adaptada
 * quando o suporte ao armazenamento real for implementado.
 *
 * @param name    Buffer que receberá os nomes das faixas.
 * @param content Vetor de ponteiros para os buffers de conteúdo.
 *
 * @retval 0                      Operação concluída.
 * @retval K_ERR_STACK_CHK_FAIL   Arquivo maior que o limite permitido.
 *
 * @warning Os elementos de `content` devem apontar para buffers válidos
 *          com capacidade de pelo menos `STORAGE_FILE_MAX` bytes.
 */
static int get_name_content(char *name, uint8_t **content) {
    struct track_meta *trackMeta =
        (struct track_meta *)malloc(sizeof(struct track_meta) * 10);

    for (size_t i = 0; i < 5; i++) {
        /* int file_size = storage_load(i, content[i], STORAGE_FILE_MAX); */
        int file_size = 3;

        if (file_size > (int)STORAGE_FILE_MAX) {
            return K_ERR_STACK_CHK_FAIL;
        }

        /* int ret_scan = storage_scan(trackMeta, 10, 0); */
        /* LOG_INF("ret_scan: %d\n", ret_scan); */

        /* Concatena o nome da faixa e uma quebra de linha. */
        lv_strcat(name, "Test");
        lv_strcat(name, "\n");
    }

    /* Remove a última quebra de linha da lista de nomes. */
    name[lv_strnlen(name, 35) - 1] =
        '\0'; /* TODO: adaptar ao carregamento das faixas reais. */

    return 0;
}

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
/* Eventos de interação                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Processa o evento de clique no roller.
 *
 * Habilita o modo de edição do grupo associado ao objeto, quando existir,
 * e sinaliza que um clique foi recebido.
 *
 * A variável `clicked` é utilizada posteriormente por `event_cb` para
 * determinar se a entrada de tecla deve atualizar a música selecionada.
 *
 * @param e Evento de clique recebido do LVGL.
 */
#if defined(CONFIG_BOARD_ZBOOK)

static void card_changed(enum card_state state) {
    LOG_INF("Cartao mudou para: %s", card_state_name(state));
}

static void card_report(void) {
    int err;

    /* Observar antes de inicializar, para não perder a transição de quem
     * liga a placa com o cartão já no soquete.
     */
    card_observe(card_changed);

    err = card_init();
    if (err != 0) {
        LOG_ERR("Monitor do cartao nao subiu: %d", err);
        return;
    }

    LOG_INF("Cartao: %s (ponto de montagem %s)",
            card_state_name(card_get_state()), CARD_MOUNT_POINT);
}

static void clicked_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target_obj(e);
    lv_group_t *g = lv_obj_get_group(obj);

    card_report();

    if (g != NULL) {
        lv_group_set_editing(g, true);
    }
}
#endif

/* -------------------------------------------------------------------------- */
/* Aplicação principal                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Inicializa os componentes da interface principal.
 *
 * Cria e configura os estilos, o roller de seleção de músicas, o grupo
 * de entrada, o rótulo da música e a barra de progresso.
 *
 * Os estilos e os dados associados aos callbacks possuem duração estática,
 * permitindo que o LVGL mantenha referências a esses objetos durante a
 * execução da interface.
 *
 * @note A implementação atual utiliza o armazenamento falso para carregar
 *       os nomes das faixas.
 *
 * @todo Adaptar o carregamento das faixas para utilizar o cartão de memória.
 *       Algumas operações poderão precisar ser executadas pelo loop principal.
 *
 * @todo Corrigir o problema em que a animação de rolagem pode parar quando
 *       músicas são selecionadas em uma frequência elevada.
 *
 *       Uma possível solução é utilizar uma fila de eventos ou introduzir
 *       um intervalo entre seleções, dependendo da implementação do
 *       decodificador de MP3 para PCM.
 */
static void app(void) {
    /* Estilos utilizados pelos componentes da interface. */
    static lv_style_t style_roller_main;
    static lv_style_t style_roller_selected;
    static lv_style_t style_bar;
    static lv_style_t style_container;

    /* Buffer estático utilizado para armazenar os nomes das faixas. */
    static char names[BUFFER_SIZE];

    static bool inited = false;

    uint8_t *content[STORAGE_FILE_MAX];

    if (get_name_content(names, content)) {
        LOG_ERR("File too large");
    }

    if (!inited) {
        /* Estilo principal do roller. */
        lv_style_init(&style_roller_main);
        lv_style_set_bg_color(&style_roller_main, lv_color_hex(0xffffff));
        /* lv_style_set_radius(&style_roller_main, 2); */
        lv_style_set_border_opa(&style_roller_main, LV_OPA_0);
        lv_style_set_text_color(&style_roller_main, lv_color_hex(0x000000));
        lv_style_set_text_line_space(&style_roller_main, 2);
        lv_style_set_outline_opa(&style_roller_main, LV_OPA_100);

        /* Estilo da opção selecionada. */
        lv_style_init(&style_roller_selected);
        lv_style_set_bg_color(&style_roller_selected, lv_color_hex(0x000000));
        lv_style_set_text_color(&style_roller_selected, lv_color_hex(0xffffff));

        /* Estilo da barra de progresso. */
        lv_style_init(&style_bar);
        lv_style_set_radius(&style_bar, 12);

        /* Estilo do container. */
        lv_style_init(&style_container);
        lv_style_set_border_opa(&style_container, LV_OPA_0);

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
    lv_roller_set_options(roller, names, LV_ROLLER_MODE_NORMAL);

    /* Define a terceira opção como seleção inicial. */
    lv_roller_set_selected(roller, 2, false);

    lv_obj_add_style(roller, &style_roller_main,
                     LV_PART_MAIN | LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY |
                         LV_STATE_EDITED);
    lv_obj_add_style(roller, &style_roller_selected, LV_PART_SELECTED);

    /* ---------------------------------------------------------------------- */
    /* Container inferior: informações da reprodução                         */
    /* ---------------------------------------------------------------------- */

    lv_obj_t *container = create_flex_container(screen);

    lv_obj_add_style(container, &style_container, LV_PART_MAIN);

    /* Rótulo que apresenta o nome da música atualmente selecionada. */
    lv_obj_t *music_label = lv_label_create(container);

    lv_obj_set_style_text_font(music_label, &lv_font_unscii_8, 0);
    lv_label_set_text(music_label, "Nothing Playing");

    /* ---------------------------------------------------------------------- */
    /* Grupo de entrada                                                       */
    /* ---------------------------------------------------------------------- */

    /*
     * Associa o dispositivo de entrada a um grupo LVGL.
     * O roller recebe o foco inicial e é configurado em modo de edição.
     */
    lv_group_t *group = lv_group_create();

    lv_indev_set_group(lvgl_input_get_indev(encoder_dev), group);

    lv_group_add_obj(group, roller);
    lv_group_focus_obj(roller);

#if defined(CONFIG_BOARD_ZBOOK)
    lv_group_set_editing(group, true);
#endif

    /* ---------------------------------------------------------------------- */
    /* Barra de progresso                                                     */
    /* ---------------------------------------------------------------------- */

    lv_obj_t *bar = lv_bar_create(container);

    lv_obj_set_size(bar, lv_pct(90), 8);

    lv_bar_set_min_value(bar, 0);
    lv_bar_set_max_value(bar, 100);

    lv_obj_add_style(bar, &style_bar, LV_PART_MAIN);

    /* ---------------------------------------------------------------------- */
    /* Registro dos callbacks                                                 */
    /* ---------------------------------------------------------------------- */

    /*
     * Estrutura estática para armazenar os objetos utilizados por event_cb.
     * Sua duração deve ser igual ou superior à dos callbacks registrados.
     */
    static cb_data global_data;

    global_data.label = music_label;
    global_data.bar = bar;

    /* lv_obj_add_event_cb(roller, event_cb, LV_EVENT_KEY, &global_data); */
    lv_obj_add_event_cb(roller, event_cb, LV_EVENT_VALUE_CHANGED, &global_data);

#if defined(CONFIG_BOARD_ZBOOK)
    /* Essencial para manter o foco no roller após clicks */
    lv_obj_add_event_cb(roller, clicked_cb, LV_EVENT_CLICKED, NULL);
#endif
}
