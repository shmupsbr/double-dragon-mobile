# Cena 1 a partir do ddiPhone

O executável iOS está legível. `LC_ENCRYPTION_INFO` existe e `cryptid` é 0 nos dois slices. Isso é código de máquina com símbolos, não código-fonte.

A cena abaixo usa só o que o slice armv7 guarda. O `port/ddslum` antigo não entra nessa conta.

## O que a cena faz

Janela 480×320, desenhada em dobro. Jimmy nasce em X=60, Z=56 (`csStage01ScrollSet` +0x0c e +0x10, lidos por `GT_CreatePlayerTask` em `0xabe0`). O chão desse tile é 239 (`FloorGroundSet` em `0x12a44`, registro 1 da lista, altura 0). A origem na tela é `chão − Z`.

Setas ou WASD repetem os eixos de `GT_MoveCheck` (`0x36820`):

- Direita é o índice 3: velocidade X = −`ciBillyMoveData[0]` = +360448 em 16.16 (+5,5 px por quadro), `facing` 1, ato 1.
- Esquerda é o índice 1: velocidade X = `ciBillyMoveData[0]` = −360448, `facing` 0, ato 1.
- Cima é o índice 0: velocidade Z = +262144 (+4 px), ato não muda.
- Baixo é o índice 2: velocidade Z = −262144.
- Sem direção, `GT_ReadySet` (`0xf8e8`) zera a velocidade e volta ao ato 0.

O ponteiro de `ciBillyMoveData` (`0xc6398`) é gravado em `applet+0x4fa8` (tipo 0, Billy) e em `applet+0x4fac` (tipo 1). O registro 1 de `csPlayerCharLoadData` (`0xa84ec`) é o Jimmy: tipo 1, folhas 16 e 17, `JIMMY_1.obm` e `JIMMY_2.obm`.

O ato 0 (`ciJimmyAct_000`, `0xa9fcc`) é a troca de peso: durações 2, 1, 2, 1, 2, padrões 0–4, opcode 3 (`Act_AI_LOOP`). O ato 1 (`0xaa01c`) é a passada: 2, 1, 1, 2, 2, padrões 5–9, também `LOOP`. `ActionSub` (`0xd130`) conta a duração em quadros.

O mapa é `ciStage01MapChip` (`0x65498`) e `ciStage01MapChipNear` (`0x663c0`). `csStage01MapRange` diz 97×20 tiles, 1552×320 px. `MapChipDraw` (`0x12f24`) indexa `altura * coluna + linha`. Tile negativo some. Os outros usam `tx = id & 31`, `ty = id >> 5`, 16 px, folha `AREA1_CHIP.obm`. A camada near entra primeiro e rola com a câmera deslocada 2 bits.

A fileira 0 do mapa é o asfalto. `glOrthox` põe Y=0 no topo da tela e o fundo em `-altura`. `OPENGL_StoreSprite` e `FillRect` negam o Y do jogo, então o +Y do jogo desce a partir do topo. `FloorGroundSet` devolve 239 (`0xEF`) nesse eixo. Esse valor só cai na rua se a fileira 0 estiver embaixo: a imagem usa `(20 - 1 - fileira) * 16`. A montagem anterior punha a fileira 0 no topo, e a rua ficava no telhado. O campo `+0x47ac`, que o desenho subtrai do Y, não teve uma gravação achada; com ele em zero a fórmula do chip sozinha deixaria a fileira 0 no topo. A orientação usada é a que põe o asfalto no mesmo lugar que o 239.

O inimigo é o zako C00: retângulos `ciZakoC00Rect`, imagem local 0 → nome 21 → `BASH.obm`. O ato 0 (`0xb2174`) repete os padrões 0–5, dois quadros cada. A posição X=160 é desta vista. `csStage01EncEnemyData` não foi decodificado, então esse X não é um spawn original.

`ciDrawFlag` (`0x604d0`) troca 0↔1 quando `facing` é 1. A vista espelha o corpo nesse caso. O sombreador que consome a flag não foi aberto instrução por instrução; o espelho é a leitura dessa troca.

## O que ainda não é o jogo

Não há soco, dano, vida, IA, nem a tabela de encontro. O muro de `FloorWallCheck` não está ligado; o limite é a largura do mapa e Z 0..95, que é o teste de altura do chão (`header+0x0e` = 7). O ângulo do analógico em `GamePad::SetKey` cai nos mesmos quatro índices, mas a ordem dos argumentos do `atan` não foi fechada; o teclado segue o sinal da velocidade, não um desenho do stick.

## Como rodar

O Mach-O sanitizado e o zip das OBM ficam fora do git.

```bash
PYTHONPATH=/workspace python3 -m ios.scene_pack \
  --macho /tmp/ios-work/binary/ddiPhone \
  --obm /caminho/02_resources_obm_d64a.zip \
  --out scene.bin
make -C ios linux
./ios/scene_view scene.bin
./ios/scene_view scene.bin --dump quadro.ppm 16 right
make -C ios windows
```

O `.exe` foi compilado e o Wine rodou o `--dump` (a lógica, não a janela Win32). A saída coincidiu com a do binário Linux: `x 148 z 56 act 1 frame 4 facing 1`. A janela Win32 não foi aberta.

```bash
PYTHONPATH=/workspace python3 -m unittest tests.test_ios_anim tests.test_scene_dump -v
```
