# Compilação em contêiner, com imagem enxuta na bancada e a oficial no CI

O grupo trabalha em Windows, e montar um workspace Zephyr nativo ali significa
instalar Python, CMake, Ninja, dtc, o SDK de 2 GB e acertar variáveis de
ambiente em cada máquina — com o risco de as três máquinas ficarem em
versões diferentes e a integração quebrar por diferença de ambiente, não de
código.

Decidimos padronizar a compilação em contêiner, com **duas imagens
diferentes de propósito**: `docker/Dockerfile` monta uma imagem de ~1,3 GB
para a bancada local, e o CI usa a `zephyrprojectrtos/ci` oficial. As versões
do Zephyr (v4.4.2) e do SDK (1.0.1) estão fixadas no `west.yml` e no
Dockerfile, então o que muda entre as duas é o ferramental em volta, não o
compilador nem as fontes.

## Considered Options

- **Só a imagem oficial, também na bancada.** Rejeitado: são 7,6 GB por
  desenvolvedor. Numa disciplina em que a primeira tarefa é "compilar um
  blinky", esse download é a maior parte da E0.
- **Só a imagem própria, também no CI.** Tem a seu favor eliminar a
  divergência entre bancada e CI, e chegou a ser a escolha preferida.
  Rejeitado para não desviar da issue #7 sem necessidade, e porque no CI o
  download não custa nada: os executores do GitHub puxam a imagem oficial
  rápido e o workspace west fica em cache.
- **Instalação nativa em cada máquina, documentada no README.** É o caminho
  oficial do Zephyr e continua funcionando. Rejeitado como caminho padrão
  porque é justamente o que produz três ambientes ligeiramente diferentes.

## Consequences

Existem dois ambientes a manter. A divergência é contida porque o que
determina o binário — versão do Zephyr, lista de módulos, versão do SDK —
está fixado em arquivo versionado, não na imagem. Ainda assim, subir a versão
do Zephyr exige mexer nos dois lugares, e o Dockerfile diz isso.

O workspace west (`zephyr/` e `modules/`, ~1 GB) fica num volume nomeado do
Docker, e não no bind mount: além de sobreviver entre execuções, tira o
sistema de arquivos do Windows do caminho crítico de um build que cria
milhares de arquivos pequenos.
