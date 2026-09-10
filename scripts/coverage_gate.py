#!/usr/bin/env python3
"""Portão de cobertura dos testes unitários.

Lê o relatório JSON do gcovr produzido pelo twister e reprova a execução se
houver linha ou ramo descoberto nos módulos do projeto — ou se alguma fonte
da aplicação tiver escapado da medição sem justificativa.

A meta é 100% em linhas e em ramos. Ramos que o ambiente de teste
comprovadamente não consegue exercitar ficam registrados em
``tests/coverage-exclusions.json``, cada um com a justificativa — o objetivo
da exclusão é que a lacuna seja uma decisão escrita, e não um número que foi
caindo sem ninguém notar.

O portão também reprova exclusão obsoleta: se um ramo excluído passar a ser
coberto, a entrada tem de sair do arquivo. Sem isso, a lista viraria um
depósito de dívidas antigas.

Uso:
    python3 scripts/coverage_gate.py twister-out/coverage.json
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

#: Raiz do repositório, deduzida da posição deste script.
REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent

#: Caminho do arquivo de exclusões.
EXCLUSIONS_PATH = REPO_ROOT / "tests" / "coverage-exclusions.json"

#: Fontes que o firmware compila mas os testes deliberadamente não medem.
#:
#: Um arquivo que não chega à medição não aparece no relatório, e cobertura de
#: 100% sobre o que sobrou não quer dizer nada — o número continua bonito
#: enquanto a cobertura real cai. Daí a lista ser explícita: cada isenção é uma
#: decisão escrita, e qualquer arquivo novo que escape entra reprovando.
UNMEASURED_SOURCES = {
    "src/main.c": "Ponto de entrada: amarra os módulos e entra em laço "
                  "infinito. Compilá-lo nos testes traria um segundo main(), "
                  "que colidiria com o do ztest.",
}


class FileCoverage:
    """Cobertura de um arquivo, já reduzida ao que o portão precisa saber."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.lines_total = 0
        self.lines_hit = 0
        self.branches_total = 0
        self.branches_hit = 0
        #: Linhas sem nenhuma execução.
        self.missing_lines: list[int] = []
        #: Linhas com ao menos um ramo não tomado.
        self.missing_branches: list[int] = []

    @property
    def line_percent(self) -> float:
        if self.lines_total == 0:
            return 100.0
        return 100.0 * self.lines_hit / self.lines_total

    @property
    def branch_percent(self) -> float:
        if self.branches_total == 0:
            return 100.0
        return 100.0 * self.branches_hit / self.branches_total


def load_coverage(path: pathlib.Path) -> list[FileCoverage]:
    """Interpreta o JSON do gcovr.

    :param path: Arquivo ``coverage.json`` gerado pelo twister.
    :return: Uma entrada por arquivo medido.
    """
    report = json.loads(path.read_text(encoding="utf-8"))
    result: list[FileCoverage] = []

    for entry in report.get("files", []):
        cov = FileCoverage(entry.get("file", "?"))

        for line in entry.get("lines", []):
            number = line.get("line_number", 0)
            cov.lines_total += 1
            if line.get("count", 0) > 0:
                cov.lines_hit += 1
            else:
                cov.missing_lines.append(number)

            branches = line.get("branches", [])
            taken = [b for b in branches if b.get("count", 0) > 0]
            cov.branches_total += len(branches)
            cov.branches_hit += len(taken)
            if branches and len(taken) < len(branches):
                cov.missing_branches.append(number)

        result.append(cov)

    return sorted(result, key=lambda c: c.name)


def load_exclusions() -> dict[str, dict[int, str]]:
    """Lê as exclusões registradas, se houver arquivo.

    :return: ``{arquivo: {linha: justificativa}}``.
    """
    if not EXCLUSIONS_PATH.exists():
        return {}

    raw = json.loads(EXCLUSIONS_PATH.read_text(encoding="utf-8"))
    return {
        name: {int(line): reason for line, reason in entries.items()}
        for name, entries in raw.items()
        if not name.startswith("//")
    }


def check_everything_measured(measured: set[str]) -> list[str]:
    """Verifica que nenhuma fonte da aplicação escapou da medição.

    Cobertura de 100% só quer dizer alguma coisa se todo o código que deveria
    ser medido chegou à medição. Um arquivo pode escapar em silêncio de duas
    maneiras: não estar na lista de fontes que os testes compilam, ou estar
    numa parte da árvore que eles não incluem. Nos dois casos ele simplesmente
    não aparece no relatório, e a porcentagem sobre o que sobrou continua
    dizendo 100%.

    Este é o invariante que o portão defende — não uma regra de organização de
    diretórios. Onde cada arquivo mora é escolha livre do projeto; o que não
    pode é sair da medição sem alguém ter escrito por quê.

    :param measured: Nomes de arquivo, relativos à raiz do repositório,
                     presentes no relatório de cobertura.
    :return: Uma mensagem por problema encontrado; vazio se estiver tudo bem.
    """
    problems: list[str] = []
    src_dir = REPO_ROOT / "src"

    for source in sorted(src_dir.rglob("*.c")):
        name = source.relative_to(REPO_ROOT).as_posix()

        if name in measured or name in UNMEASURED_SOURCES:
            continue

        problems.append(
            f"{name} não aparece no relatório de cobertura."
            f"\n    Ou os testes não o compilam — veja"
            f" tests/vitrolinha_test.cmake e src/CMakeLists.txt —, ou ele ficou"
            f" fora da lista de fontes e não está sendo compilado nem no"
            f" firmware. Se for código que realmente não se testa, declare-o em"
            f" UNMEASURED_SOURCES com a razão.")

    for name in sorted(UNMEASURED_SOURCES):
        if name in measured:
            problems.append(
                f"{name} está declarado em UNMEASURED_SOURCES mas foi medido."
                f"\n    A isenção deixou de ser necessária: remova a entrada.")
        elif not (REPO_ROOT / name).exists():
            problems.append(
                f"{name} está declarado em UNMEASURED_SOURCES mas não existe"
                f" mais.\n    Remova a entrada.")

    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=pathlib.Path,
                        help="caminho do coverage.json do gcovr")
    args = parser.parse_args()

    if not args.report.exists():
        print(f"Relatório de cobertura não encontrado: {args.report}", file=sys.stderr)
        return 2

    files = load_coverage(args.report)
    if not files:
        print("Relatório de cobertura vazio: nenhum arquivo foi medido.", file=sys.stderr)
        return 2

    exclusions = load_exclusions()
    failures: list[str] = check_everything_measured({cov.name for cov in files})

    print()
    print(f"{'arquivo':<24} {'linhas':>16}   {'ramos':>16}")
    print("-" * 62)

    for cov in files:
        allowed = exclusions.get(cov.name, {})
        unexplained = [n for n in cov.missing_branches if n not in allowed]
        stale = [n for n in allowed if n not in cov.missing_branches]

        print(f"{cov.name:<24} "
              f"{cov.lines_hit:>4}/{cov.lines_total:<4} ({cov.line_percent:6.2f}%)   "
              f"{cov.branches_hit:>4}/{cov.branches_total:<4} ({cov.branch_percent:6.2f}%)")

        if cov.missing_lines:
            failures.append(
                f"{cov.name}: linhas sem execução: "
                + ", ".join(str(n) for n in cov.missing_lines))

        if unexplained:
            failures.append(
                f"{cov.name}: ramos não tomados e não justificados nas linhas "
                + ", ".join(str(n) for n in unexplained)
                + f"\n    Cubra-os com um teste, ou registre a razão em "
                  f"{EXCLUSIONS_PATH.name}.")

        if stale:
            failures.append(
                f"{cov.name}: exclusões obsoletas nas linhas "
                + ", ".join(str(n) for n in stale)
                + f"\n    Esses ramos passaram a ser cobertos. Remova as entradas de "
                  f"{EXCLUSIONS_PATH.name}.")

        for line, reason in sorted(allowed.items()):
            if line in cov.missing_branches:
                print(f"    ramo excluido, linha {line}: {reason}")

    print()

    if failures:
        print("Portão de cobertura REPROVADO:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print("Portão de cobertura aprovado: 100% de linhas e de ramos "
          "(fora as exclusões justificadas acima).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
