# shellcheck shell=bash
#
# Base comum dos scripts de bancada. Não executa nada sozinho: os demais
# scripts fazem `source` deste arquivo. Concentra aqui tudo que é específico
# de ambiente, para que nenhum outro script precise saber que existe Docker,
# Windows ou volume nomeado.

set -euo pipefail

IMAGE="${VITROLINHA_IMAGE:-vitrolinha-zephyr}"
VOLUME="${VITROLINHA_VOLUME:-vitrolinha-west}"

# No Git Bash o MSYS reescreve qualquer argumento parecido com caminho Unix
# antes de o docker.exe vê-lo: "/workdir" viraria "C:/Program Files/Git/workdir"
# e o mount apontaria para o lugar errado, sem erro. `pwd -W` devolve o caminho
# no formato que o Docker Desktop entende.
case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*)
	export MSYS_NO_PATHCONV=1
	export MSYS2_ARG_CONV_EXCL='*'
	REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -W)"
	;;
*)
	REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
	;;
esac

# Diretório do repositório dentro do contêiner. Tem de casar com o `self.path`
# declarado no west.yml, senão o west não reconhece o manifesto.
readonly CONTAINER_REPO="/workdir/vitrolinha"

##
# Roda um comando no contêiner de compilação, com o workspace montado.
#
# @param $@ Comando e argumentos.
##
vitrolinha_docker_run() {
	local tty_args=()

	# -t só quando há terminal de verdade: em CI e sob pipe o docker recusa.
	if [ -t 0 ] && [ -t 1 ]; then
		tty_args=(-it)
	fi

	docker run --rm "${tty_args[@]}" \
		-v "${VOLUME}:/workdir" \
		-v "${REPO_DIR}:${CONTAINER_REPO}" \
		-w /workdir \
		"${IMAGE}" \
		"$@"
}

##
# Aborta com mensagem se a imagem de compilação ainda não foi construída.
##
vitrolinha_require_image() {
	if ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
		echo "Imagem ${IMAGE} não existe. Rode: scripts/setup.sh" >&2
		exit 1
	fi
}
