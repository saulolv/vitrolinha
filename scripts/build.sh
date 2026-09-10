#!/usr/bin/env bash
#
# Compila o firmware para a placa. Argumentos extras vão para o west build:
#
#   scripts/build.sh              # compilação incremental
#   scripts/build.sh --pristine   # do zero
#
# O diretório de build fica no volume, fora do bind mount: um build do Zephyr
# cria milhares de arquivos pequenos, e o bind mount do Windows cobra caro por
# cada um. Os artefatos finais são copiados para build-out/.

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# O sufixo @p2 é obrigatório. Sem ele o build recai na revisão P1, que não tem
# encoder nem display. O CMakeLists rejeita esse caso, mas o padrão daqui já
# evita o engano.
BOARD="${VITROLINHA_BOARD:-zbook@p2/rp2350b/m33}"

vitrolinha_require_image
vitrolinha_docker_run bash -euc "
	west build -b '${BOARD}' -d /workdir/build '${CONTAINER_REPO}' $*
	mkdir -p '${CONTAINER_REPO}/build-out'
	cp /workdir/build/zephyr/zephyr.uf2 \
	   /workdir/build/zephyr/zephyr.hex \
	   /workdir/build/zephyr/zephyr.elf \
	   '${CONTAINER_REPO}/build-out/'
	echo
	echo '==> Artefatos em build-out/. Grave zephyr.uf2 por USB (BOOTSEL).'
"
