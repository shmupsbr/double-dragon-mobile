# Base iOS — tabelas de animação

O protótipo Android anterior cortava pedaços soltos da folha. Essa base está abandonada. O executável desta edição iOS está legível.

Build: `com.bowmobile.doubledragon.US`, `ddiPhone` 1.00.01. Não é o pacote G-gee. As folhas OBM decodificadas coincidem com as do Android; isso só prova o desenho da folha, não o recorte nem a montagem.

`LC_ENCRYPTION_INFO` existe nos dois slices ARM e `cryptid` é 0. Conferido de novo no binário sanitizado, não só no relatório que veio junto.

## O que o código guarda

Cada peça é um `_IMG_RECT` de oito `int16`: `x, y, w, h, px, py, imagem, extra`. O endereço no slice armv7 é o valor do símbolo.

`ciJimmyRect` = `0x9B064` (864 peças). `ciJimmyPat` = `0x9E664` lista os índices e fecha cada pose com `-1`. `ciJimmyPatPoint` = `0x9F438` diz onde cada pose começa. O pivô cai na origem do personagem: a peça é desenhada em `(-px, -py)`. A pose 0 junta as peças 0, 1, 2 e 3 e fica mais alta do que qualquer peça sozinha. É o corpo inteiro, com pé.

`ciJimmyAct_000` = `0xA9FCC` é bytecode de `int32`. Palavra negativa é opcode (`Act_AI_*` em `ActionInit`). Palavra positiva abre a tripla duração, flags, padrão. O ato 0, depois do `Act_AI_HITP` (opcode 9, três argumentos), mostra os padrões 0, 1, 2, 3, 4 com durações 2, 1, 2, 1, 2 e termina em `Act_AI_LOOP` (opcode 3). `GT_BillyRunStart` grava o ato 0 em `GT_ActionSet` no endereço `0x2d53a`. A imagem desse ciclo é troca de peso em pé, com as duas pernas e os pés. Não é uma passada de corrida.

O ato 6 (`ciJimmyAct_006`) percorre os padrões 15 a 20, um quadro cada, e é um golpe em que o corpo se estica. O ato 9 segura o padrão 25: o corpo deitado, também inteiro. O dano e a queda não foram medidos em pontos de vida; a tabela `ci*AtkData` ainda não foi decodificada.

`ciZakoC00` é outra tabela, no mesmo formato. A pose 0, desenhada em `WILLIAMS.obm` com imagem local 0, fecha um inimigo de corpo inteiro. A mesma lista de retângulos não fecha em `MACHO` nem em `SUIT`. O número global em `ciZakoC00ImgNo` não é o nome do arquivo; o vínculo do arquivo é essa montagem, não uma string no binário.

## O que isto ainda não é

Não há cena jogável nova. O `port/ddslum` antigo continua no repositório como histórico e não deve ser usado como regra de luta. Falta o mapa de chips e a escolha de ato dentro de cada estado (andar para a frente, soco, chute) além dos três atos acima. O limite agora é essa máquina de estados e o mapa, não o retângulo da folha.

O binário sanitizado não entra no git. Os testes leem `/tmp/ios-work/binary/ddiPhone`.

```bash
PYTHONPATH=/workspace python3 -m unittest tests.test_ios_anim -v
```
