/**
 * @file card.c
 * @brief Montagem do cartão e o monitor que vê inserção e remoção.
 *
 * Duas coisas num arquivo só, porque são a mesma: montar é a única maneira de
 * descobrir se o cartão presta, e observar a presença só tem valor se alguém
 * montar quando ela aparecer.
 *
 * O que torna isto menos trivial do que parece é que, sem `cd-gpios` no nó
 * `sdhc0`, a pilha SD mente: `sdhc_spi_get_card_present` devolve 1
 * incondicionalmente, com o comentário "SPI has no card presence method,
 * assume card is in slot" (`zephyr/drivers/sdhc/sdhc_spi.c:780`). O overlay
 * da placa liga o pino, e é só por causa dele que qualquer coisa deste
 * arquivo funciona.
 *
 * @see card.h para o contrato, docs/adr/0007 para as alternativas rejeitadas
 */

#include <vitrolinha/card.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/atomic.h>

#include <ff.h>

#include <errno.h>

LOG_MODULE_REGISTER(card, LOG_LEVEL_INF);

/**
 * @brief Nome do disco, que é o ponto de montagem sem a barra.
 *
 * O subsistema de arquivos quer `"/SD"`, o `disk_access` quer `"SD"`, e o
 * `disk-name` do nó `sdhc0` é o segundo. Derivar um do outro evita a classe
 * de erro em que os dois deixam de casar e a montagem falha sem que nada no
 * código pareça errado.
 *
 * O `+ 1` pula a barra. Que ela exista é invariante do ::CARD_MOUNT_POINT, e
 * não é verificável em `BUILD_ASSERT`: indexar um literal de string não é
 * expressão constante em C.
 */
#define CARD_DISK_NAME (CARD_MOUNT_POINT + 1)

/** @brief Tempo sem novas bordas antes de avaliar o contato mecânico. */
#define CARD_DEBOUNCE_MS 20
#define CARD_HAS_DETECT DT_NODE_HAS_PROP(DT_NODELABEL(sd_cd), gpios)

/**
 * @brief Pilha do monitor.
 *
 * Dimensionada com folga por ora: quem monta é esta thread, e montar
 * atravessa o FatFs e a pilha SD inteira. O número definitivo sai da medição
 * do `CONFIG_THREAD_ANALYZER` (issue #13), que é critério de avaliação — não
 * de estimativa.
 */
#define CARD_MONITOR_STACK_SIZE 2048

/**
 * @brief Prioridade do monitor: a mais baixa do sistema.
 *
 * Abaixo da fila de trabalho do LVGL (10), que já está abaixo do `player`.
 * O monitor é a única thread que bloqueia por mais de um milissegundo, e não
 * há nada no sistema que possa esperar por ela.
 */
#define CARD_MONITOR_PRIORITY 12

/** @brief Objeto de trabalho do FatFs para o volume do cartão. */
static FATFS card_fat;

/**
 * @brief Descrição da montagem.
 *
 * `FS_MOUNT_FLAG_NO_FORMAT` é a segunda tranca do mesmo perigo que o
 * `CONFIG_FS_FATFS_MOUNT_MKFS=n` fecha: sem ele, `fs_mount` formata um cartão
 * que não montou, apagando a biblioteca de quem só queria ouvir música. O
 * Kconfig sozinho bastaria; a tranca aqui é para que ligar a opção de volta,
 * por engano ou por outro módulo, não volte a armar a bomba.
 */
static struct fs_mount_t card_mount = {
	.type = FS_FATFS,
	.fs_data = &card_fat,
	.mnt_point = CARD_MOUNT_POINT,
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
};

/**
 * @brief Estado publicado, lido sem trava por qualquer thread.
 *
 * Atômico e não `enum` simples: quem lê é a interface, a cada quadro, e quem
 * escreve é o monitor. Um `int` desalinhado não existe nesta arquitetura, mas
 * `atomic_set` também devolve o valor anterior numa operação só — que é
 * exatamente o que @ref publish precisa para decidir se houve transição.
 */
static atomic_t card_state_now = ATOMIC_INIT(CARD_ABSENT);

/** @brief O volume está montado? Escrito só sob @ref card_lock. */
static bool card_mounted;

/** @brief O monitor já existe? */
static bool card_started;

/** @brief Quem quer saber das transições. */
static card_observer_t card_observer;

/**
 * @brief Serializa as avaliações.
 *
 * A aplicação pode chamar @ref card_refresh enquanto o monitor está no meio
 * de uma montagem. Sem isto, os dois chamariam `fs_mount` sobre o mesmo
 * `fs_mount_t`.
 */
static K_MUTEX_DEFINE(card_lock);

/* Preserva uma remoção mesmo se houver reinserção antes do debounce terminar. */
static atomic_t removal_pending;

#if CARD_HAS_DETECT
static const struct gpio_dt_spec card_detect = GPIO_DT_SPEC_GET(DT_NODELABEL(sd_cd), gpios);
static struct gpio_callback card_detect_callback;
static K_SEM_DEFINE(card_change, 0, 1);
static struct k_thread card_monitor_thread;
static K_THREAD_STACK_DEFINE(card_monitor_stack, CARD_MONITOR_STACK_SIZE);

/** @brief A interrupção só registra a remoção e acorda a thread de montagem. */
static void card_detect_changed(const struct device *port, struct gpio_callback *callback,
				gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(callback);
	ARG_UNUSED(pins);
	if (gpio_pin_get_dt(&card_detect) == 0) {
		atomic_set(&removal_pending, 1);
	}
	k_sem_give(&card_change);
}
#endif

/**
 * @brief Há cartão no soquete?
 *
 * Consulta barata — na placa, uma leitura do pino de card-detect por dentro
 * da pilha SD. Não diz nada sobre o volume ser legível.
 *
 * @return Verdadeiro se o disco existe e não acusa ausência de mídia.
 */
static bool card_slot_occupied(void)
{
	int status = disk_access_status(CARD_DISK_NAME);

	/* Negativo é disco não registrado: nenhum nó `zephyr,sdmmc-disk` no
	 * devicetree. Do ponto de vista de quem usa, é o mesmo que soquete
	 * vazio — e é o que acontece no alvo de simulação.
	 */
	if (status < 0) {
		return false;
	}

	return (status & DISK_STATUS_NOMEDIA) == 0;
}

/**
 * @brief Publica o estado.
 *
 * Não chama o observador: quem faz isso é @ref card_refresh, depois de soltar
 * a trava. Avisar de dentro dela criaria ordem de travas entre este módulo e
 * quem quer que o observe — a interface, no fim — e essa é a maneira clássica
 * de produzir um impasse que só aparece na demonstração.
 *
 * @param state Estado novo.
 * @return Verdadeiro se o estado mudou de fato.
 */
static bool publish(enum card_state state)
{
	enum card_state previous =
		(enum card_state)atomic_set(&card_state_now, (atomic_val_t)state);

	if (previous == state) {
		return false;
	}

	LOG_INF("Cartao: %s", card_state_name(state));

	return true;
}

/**
 * @brief Tenta montar o volume.
 *
 * @return 0, ou o erro negativo de `fs_mount`.
 */
static int card_try_mount(void)
{
	int err = fs_mount(&card_mount);

	if (err == -EBUSY) {
		/* Sobra de uma desmontagem que não completou. O `fs_unmount`
		 * só tira o ponto de montagem da lista se o sistema de
		 * arquivos abaixo dele tiver concluído; quando não conclui, o
		 * ponto fica lá e todo `fs_mount` seguinte devolve -EBUSY —
		 * para sempre, porque nada mais tenta desmontar.
		 *
		 * Aqui a segunda tentativa tem chance de dar certo justamente
		 * por ser mais tarde: o que travou a primeira foi o cartão que
		 * saiu, e agora há outro no soquete.
		 */
		LOG_WRN("Montagem anterior nao foi desfeita. Desfazendo agora.");
		(void)fs_unmount(&card_mount);
		err = fs_mount(&card_mount);
	}

	if (err != 0) {
		LOG_ERR("Cartao no soquete mas o volume nao montou: %d", err);
		return err;
	}

	LOG_INF("Cartao montado em %s", CARD_MOUNT_POINT);

	return 0;
}

/**
 * @brief Desmonta o volume, dê no que der.
 *
 * A desmontagem é registrada como concluída mesmo quando falha, de propósito:
 * insistir com o cartão fora do soquete não ajuda — a issue zephyr#94033,
 * fechada como "not planned", é sobre exatamente isso —, e ficar marcado como
 * montado só adiaria o problema.
 *
 * O que falha aqui não some, porém: o `fs_unmount` deixa o ponto de montagem
 * na lista do subsistema, e é @ref card_try_mount quem limpa a sobra, na
 * próxima inserção.
 */
static void card_force_unmount(void)
{
	int err = fs_unmount(&card_mount);

	card_mounted = false;

	if (err != 0) {
		LOG_WRN("Desmontagem falhou: %d. Seguindo como desmontado.", err);
	}
}

/**
 * @brief Devolve o disco ao estado não inicializado.
 *
 * Existe por causa de uma assimetria do `fs_mount`: quando ele falha depois
 * de já ter inicializado o cartão — o que é o caso comum, porque a falha
 * costuma estar no volume e não no meio físico —, ninguém desfaz essa
 * inicialização. A pilha SD fica achando que o cartão está pronto
 * (`data->status == SD_OK` em `sdmmc_subsys.c`), e o `f_mount` seguinte pula
 * a inicialização por ver o disco como pronto.
 *
 * Sem isto, trocar um cartão ilegível por um bom faria o FatFs conversar com
 * um cartão que nunca foi inicializado. O `fs_unmount` faz esta mesma
 * chamada, mas só existe caminho de desmontagem se a montagem tiver dado
 * certo — daí a versão avulsa.
 */
static void card_release_disk(void)
{
	/* A variante forçada, que zera a contagem de referências do disco em
	 * vez de decrementá-la. É a certa aqui: o cartão saiu do soquete, e
	 * não há como saber se a montagem que falhou chegou ou não a
	 * inicializá-lo. A variante contada responderia -EINVAL e um aviso no
	 * log em metade dos casos, sem que nenhum dos dois fosse notícia.
	 */
	bool force = true;

	(void)disk_access_ioctl(CARD_DISK_NAME, DISK_IOCTL_CTRL_DEINIT, &force);
}

enum card_state card_refresh(void)
{
	card_observer_t observer;
	enum card_state next;
	bool changed;

	(void)k_mutex_lock(&card_lock, K_FOREVER);

	bool removed = atomic_set(&removal_pending, 0) != 0;

	if (removed) {
		if (card_mounted) {
			card_force_unmount();
		} else if (card_get_state() != CARD_ABSENT) {
			card_release_disk();
		}
	}

	if (!card_slot_occupied()) {
		if (card_mounted) {
			card_force_unmount();
		} else if (!removed && card_get_state() != CARD_ABSENT) {
			/* Estava presente e ilegível: a montagem que falhou pode
			 * ter deixado o cartão inicializado para trás.
			 */
			card_release_disk();
		}

		next = CARD_ABSENT;
	} else if (card_mounted) {
		next = CARD_READY;
	} else if (!removed && card_get_state() == CARD_UNREADABLE) {
		/* Já tentamos montar este cartão e não deu. Um volume não se
		 * conserta sozinho dentro do soquete. Só tenta de novo após remoção.
		 */
		next = CARD_UNREADABLE;
	} else if (card_try_mount() == 0) {
		card_mounted = true;
		next = CARD_READY;
	} else {
		next = CARD_UNREADABLE;
	}

	changed = publish(next);
	/* Uma troca rápida pode terminar no mesmo estado, mas é outra mídia. */
	changed |= removed && next != CARD_ABSENT;

	k_mutex_unlock(&card_lock);

	observer = card_observer;

	if (changed && (observer != NULL)) {
		observer(next);
	}

	return next;
}

#if CARD_HAS_DETECT
/** @brief Aguarda uma interrupção e o fim das oscilações antes de montar. */
static void card_monitor(void *unused1, void *unused2, void *unused3)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	ARG_UNUSED(unused3);

	for (;;) {
		k_sem_take(&card_change, K_FOREVER);
		while (k_sem_take(&card_change, K_MSEC(CARD_DEBOUNCE_MS)) == 0) {
			/* Cada nova borda reinicia o intervalo de estabilização. */
		}
		(void)card_refresh();
	}
}
#endif

void card_observe(card_observer_t observer)
{
	card_observer = observer;
}

int card_init(void)
{
	if (card_started) {
		return -EALREADY;
	}

#if CARD_HAS_DETECT
	if (!gpio_is_ready_dt(&card_detect)) {
		return -ENODEV;
	}
	int err = gpio_pin_configure_dt(&card_detect, GPIO_INPUT);

	if (err != 0) {
		return err;
	}
	gpio_init_callback(&card_detect_callback, card_detect_changed, BIT(card_detect.pin));
	err = gpio_add_callback(card_detect.port, &card_detect_callback);
	if (err != 0) {
		return err;
	}
	err = gpio_pin_interrupt_configure_dt(&card_detect, GPIO_INT_EDGE_BOTH);
	if (err != 0) {
		(void)gpio_remove_callback(card_detect.port, &card_detect_callback);
		return err;
	}
#endif

	card_started = true;
	/* Trata também quem liga a placa com o cartão já inserido. */
	(void)card_refresh();

#if CARD_HAS_DETECT
	(void)k_thread_create(&card_monitor_thread, card_monitor_stack,
			      K_THREAD_STACK_SIZEOF(card_monitor_stack), card_monitor, NULL,
			      NULL, NULL, CARD_MONITOR_PRIORITY, 0, K_NO_WAIT);
	(void)k_thread_name_set(&card_monitor_thread, "card");
#endif

	return 0;
}

enum card_state card_get_state(void)
{
	return (enum card_state)atomic_get(&card_state_now);
}

bool card_ready(void)
{
	return card_get_state() == CARD_READY;
}

const char *card_state_name(enum card_state state)
{
	switch (state) {
	case CARD_ABSENT:
		return "ausente";
	case CARD_READY:
		return "presente e legivel";
	case CARD_UNREADABLE:
		return "presente e ilegivel";
	default:
		return "desconhecido";
	}
}
