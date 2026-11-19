/**
 * @file fake_display.h
 * @brief Display de teste: guarda o quadro, e o blanking responde o que o
 *        teste mandar.
 */

#ifndef VITROLINHA_TEST_FAKE_DISPLAY_H_
#define VITROLINHA_TEST_FAKE_DISPLAY_H_

#include <stdbool.h>

/**
 * @brief Escolhe o que o próximo display_blanking_off() devolve.
 *
 * @param result 0, -ENOSYS (o driver não implementa blanking) ou um erro.
 */
void fake_display_set_blanking_off_result(int result);

/**
 * @brief Diz se o pixel aparece aceso no OLED da ZBook.
 *
 * Aceso é o que o LVGL pinta de preto — o texto, o destaque, o preenchimento
 * da barra. O branco do LVGL, fundo do tema mono, sai apagado no SSD1306.
 * Vale o último quadro escrito; chamar lv_refr_now() antes.
 */
bool fake_display_lit(int x, int y);

/**
 * @brief Imprime o último quadro no console, `#` aceso e `.` apagado.
 *
 * Para olhar a tela sem a placa: rode o teste com VITROLINHA_TELA=1 no
 * ambiente. Sem a variável, não imprime nada.
 *
 * @param title Linha de título antes do quadro.
 */
void fake_display_dump(const char *title);

#endif /* VITROLINHA_TEST_FAKE_DISPLAY_H_ */
