#!/usr/bin/env bash
#
# Roda os testes unitários em native_sim e mede a cobertura.
#
#   scripts/test.sh               # testes + relatório de cobertura
#   scripts/test.sh --no-coverage # só os testes, mais rápido
#
# Relatório em build-out/coverage/index.html.

source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

PLATFORM="${VITROLINHA_TEST_PLATFORM:-native_sim}"

coverage_args="--coverage --coverage-tool gcovr --coverage-basedir ${CONTAINER_REPO}"
if [ "${1:-}" = "--no-coverage" ]; then
	coverage_args=""
fi

vitrolinha_require_image
vitrolinha_docker_run bash -euc "
	west twister \
		-p '${PLATFORM}' \
		-T '${CONTAINER_REPO}/tests' \
		--outdir /workdir/twister-out \
		--inline-logs \
		${coverage_args}
	if [ -d /workdir/twister-out/coverage ]; then
		mkdir -p '${CONTAINER_REPO}/build-out'
		rm -rf '${CONTAINER_REPO}/build-out/coverage'
		cp -r /workdir/twister-out/coverage '${CONTAINER_REPO}/build-out/coverage'
		echo
		echo '==> Cobertura em build-out/coverage/index.html'
		python3 '${CONTAINER_REPO}/scripts/coverage_gate.py' \
			/workdir/twister-out/coverage.json
	fi
"
