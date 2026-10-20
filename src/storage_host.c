/**
 * @file storage_host.c
 * @brief Esqueleto do armazenamento de CSVs em uma pasta do computador.
 *
 * Implementará storage.h exclusivamente no simulador, como alternativa
 * a storage_fake.c. Apenas uma dessas implementações poderá ser ligada.
 * O acesso ao host ficará isolado aqui; não dependerá do módulo card.
 *
 * Origem proposta: variável de ambiente VITROLINHA_MUSIC_DIR; se ausente,
 * fixtures/music relativa ao diretório de execução. Resolver a origem
 * na primeira varredura e mantê-la até a próxima. O índice de cada faixa
 * permanecerá estável entre varreduras. Não seguir subdiretórios.
 *
 * O nome exibido virá do nome do arquivo sem extensão. O campo valid
 * indicará inicialmente um candidato; a validação musical caberá ao loader.
 * A integração futura deverá ajustar a documentação hoje voltada a RTTTL.
 *
 * Somente assinaturas existentes em storage.h. Não abre arquivos e não
 * foi acrescentado ao build; não necessita de um novo storage_host.h.
 */

#include <vitrolinha/storage.h>

/**
 * @brief A implementar: listar arquivos regulares .csv, sem distinguir caixa.
 * @param out Metadados de destino, com nomes limitados a TRACK_NAME_MAX.
 * @param max Capacidade de out, limitada também por LIBRARY_MAX.
 * @return Quantidade listada; -EINVAL para argumentos inválidos; -ENODEV
 *         para pasta ausente; -EIO para falha de acesso.
 * @note Ordenar por nome antes de atribuir índices. Não executar no player.
 */
int storage_scan(struct track_meta *out, size_t max);

/**
 * @brief A implementar: carregar integralmente um CSV em buffer do chamador.
 * @param track Índice da última varredura.
 * @param buf Buffer de destino, sem garantia de terminador NUL.
 * @param len Capacidade em bytes; verificar o tamanho antes de escrever.
 * @return Bytes lidos ou erro de storage.h: -EINVAL, -ENOENT, -ENODEV,
 *         -EFBIG ou -EIO. Em erro, o chamador não deve consumir o buffer.
 * @note Tratar leituras parciais e fechar o arquivo em todos os caminhos.
 *       Respeitar STORAGE_FILE_MAX; não executar no player nem no timer.
 */
int storage_load(int track, uint8_t *buf, size_t len);

/**
 * @brief A implementar: verificar se a pasta da biblioteca está acessível.
 * @return true para pasta disponível, mesmo vazia; false se indisponível.
 * @note Neste alvo, presença refere-se à pasta, não ao cartão físico.
 *       Consulta exclusiva do contexto de armazenamento, fora do player.
 */
bool storage_present(void);
