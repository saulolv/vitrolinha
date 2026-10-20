/**
 * @file playback_trace.c
 * @brief Esqueleto do registro de execução e exportação de eventos musicais.
 *
 * Manterá uma captura de capacidade fixa em RAM, sem alocação no caminho
 * de reprodução. Os instantes serão observados na saída tone_trace, não
 * copiados dos offsets pretendidos pelo parser. Pausas reais da execução
 * simulada aparecerão como intervalos de silêncio no registro.
 *
 * A capacidade será dimensionada na implementação. Estouro tornará a
 * captura inválida para exportação, sem descartar eventos silenciosamente.
 * A escrita no host ocorrerá apenas com captura encerrada, em contexto
 * de thread separado do player. Formato definido em playback_trace.h.
 *
 * Somente assinaturas; não contém buffer, E/S ou funções implementadas.
 * Arquivo exclusivo do simulador, ainda não integrado ao build.
 */

#include <vitrolinha/playback_trace.h>

/** @brief A implementar: iniciar uma captura. @see playback_trace.h */
int playback_trace_begin(uint64_t start_us);

/** @brief A implementar: registrar evento em RAM. @see playback_trace.h */
int playback_trace_record(uint64_t timestamp_us, uint32_t frequency_hz);

/** @brief A implementar: registrar silêncio e encerrar. @see playback_trace.h */
int playback_trace_end(uint64_t timestamp_us);

/** @brief A implementar: consultar evento capturado. @see playback_trace.h */
int playback_trace_read(size_t index, struct playback_trace_entry *out);

/** @brief A implementar: exportar CSV no host. @see playback_trace.h */
int playback_trace_export_csv(const char *path);
