#!/usr/bin/env bash
#
# Prepara a bancada do zero: constrói a imagem e monta o workspace west.
# Idempotente — rodar de novo só atualiza o que mudou.
#
# Demora na primeira vez (baixa a toolchain ARM e o Zephyr, ~1 GB no total).
# Depois, segundos.

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

echo "==> Construindo a imagem ${IMAGE}"
docker build -t "${IMAGE}" "${REPO_DIR}/docker"

echo "==> Montando o workspace west no volume ${VOLUME}"
vitrolinha_docker_run bash -euc "
	# west init -l aponta para o repositório do manifesto; o topdir passa
	# a ser o diretório que o contém.
	if [ ! -d /workdir/.west ]; then
		west init -l '${CONTAINER_REPO}'
	fi
	west update --narrow --fetch-opt=--depth=1
"

echo "==> Pronto. Compile com scripts/build.sh e teste com scripts/test.sh"
