/**
 * @file fake_display.h
 * @brief Display de teste, com o resultado do blanking controlado pelo teste.
 */

#ifndef VITROLINHA_TEST_FAKE_DISPLAY_H_
#define VITROLINHA_TEST_FAKE_DISPLAY_H_

/**
 * @brief Escolhe o que o próximo display_blanking_off() devolve.
 *
 * @param result 0, -ENOSYS (o driver não implementa blanking) ou um erro.
 */
void fake_display_set_blanking_off_result(int result);

#endif /* VITROLINHA_TEST_FAKE_DISPLAY_H_ */
