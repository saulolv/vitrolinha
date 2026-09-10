#!/usr/bin/env bash
#
# Compila e roda o firmware no simulador.
#
#   scripts/sim.sh                # compila e roda
#   scripts/sim.sh --pristine     # recompila do zero
#   scripts/sim.sh --build-only   # só compila, não executa
#
# O executável fica em build-out/zephyr-sim.elf, e é um binário Linux x86-64.
#
# Teclas, quando há janela:
#   1 2 3 4     botões de transporte
#   Enter       clique do encoder
#   setas       girar o encoder
#
# ----------------------------------------------------------------------------
# Sobre a janela da tela
#
# O contêiner não tem servidor gráfico. Rodando por aqui, o firmware executa e
# o relatório sai no terminal, mas a tela do OLED não aparece — o driver SDL
# reclama de XDG_RUNTIME_DIR e segue sem janela.
#
# Para ver a tela no Windows, rode o mesmo binário pelo WSL, que tem WSLg:
#
#   wsl -d Ubuntu -e sudo apt-get install -y libsdl2-2.0-0    # uma vez só
#   wsl -d Ubuntu -e ./build-out/zephyr-sim.elf
#
# No Linux, basta executar build-out/zephyr-sim.elf direto.
#
# ----------------------------------------------------------------------------
# O que o simulador NÃO faz
#
# Som. Não existe PWM emulado no Zephyr, e o buzzer continua exigindo a placa.
# Também não substitui a bancada em nada da E1: limiar do piezo, estalo na
# troca de nota, ordem física dos botões e jitter real.

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# A variante /native/64 é obrigatória: o nome dos arquivos em boards/ tem de
# casar com a string de build, e o native_sim de 32 bits exigiria a SDL em
# i386. O CMakeLists recusa qualquer outro alvo.
BOARD="native_sim/native/64"
BUILD_DIR="/workdir/build-sim"

build_args=""
run=1
for arg in "$@"; do
	case "${arg}" in
	--build-only) run=0 ;;
	*) build_args="${build_args} ${arg}" ;;
	esac
done

vitrolinha_require_image

cmd="west build -b '${BOARD}' -d '${BUILD_DIR}' '${CONTAINER_REPO}'${build_args}
mkdir -p '${CONTAINER_REPO}/build-out'
cp '${BUILD_DIR}/zephyr/zephyr.exe' '${CONTAINER_REPO}/build-out/zephyr-sim.elf'
echo
echo '==> build-out/zephyr-sim.elf pronto.'"

if [ "${run}" -eq 1 ]; then
	cmd="${cmd}
echo '==> Rodando. Ctrl+C encerra.'
echo
'${BUILD_DIR}/zephyr/zephyr.exe'"
fi

vitrolinha_docker_run bash -euc "${cmd}"
