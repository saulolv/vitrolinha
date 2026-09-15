#!/usr/bin/env bash
#
# Compila e roda a bancada do RNF03 no simulador.
#
#   scripts/bench.sh                # compila e roda
#   scripts/bench.sh --pristine     # recompila do zero
#   scripts/bench.sh --board        # compila para a placa (não roda)
#
# A bancada mede o custo de abrir e ler uma faixa, para o orçamento de 100 ms
# entre o botão e o som (RNF03, issue #8). O relatório sai no terminal; o que
# ele significa está em docs/medicoes/rnf03-abertura-de-faixa.md.
#
# No simulador o disco é de mentira: um cartão esparso de 8 GiB formatado em
# FAT32 com aglomerados de 32 KiB, que conta os setores que o FatFs pede e
# cobra no relógio simulado o que aquele acesso custaria no SPI da placa. O que
# ele mede com exatidão é a CONTAGEM de setores; o tempo é modelo declarado.

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BOARD="native_sim/native/64"
BUILD_DIR="/workdir/build-bench"
run=1

build_args=""
for arg in "$@"; do
	case "${arg}" in
	--board)
		BOARD="zbook@p2/rp2350b/m33"
		BUILD_DIR="/workdir/build-bench-zbook"
		run=0
		;;
	*) build_args="${build_args} ${arg}" ;;
	esac
done

vitrolinha_require_image

cmd="west build -b '${BOARD}' -d '${BUILD_DIR}' '${CONTAINER_REPO}/bench/rnf03'${build_args}"

if [ "${run}" -eq 1 ]; then
	cmd="${cmd}
'${BUILD_DIR}/zephyr/zephyr.exe'"
else
	cmd="${cmd}
echo
echo '==> Binário da bancada em ${BUILD_DIR}/zephyr/. Grave e leia o console.'"
fi

vitrolinha_docker_run bash -euc "${cmd}"
