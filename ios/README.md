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

A fileira 0 do mapa é o asfalto. `0x1faa2` e `0x27662` gravam 480 em `applet+0x47a8` e 320 em `+0x47ac` (`mov.w r2, #0x1e0` e `subs r2, #0xa0`). `MapChipDraw` subtrai esse 320 do Y do tile. `glOrthox` põe Y=0 no topo e o fundo em `-altura`. `OPENGL_StoreSprite` nega o Y do jogo. A imagem usa `(20 - 1 - fileira) * 16`, que é essa conta com 320, não um palpite visual.

`InitEncountProcess` (`0x16c7c`), estágio 0, aponta `+0x47d8` para `csStage01EncData` (`0x64a60`) e `+0x47e0` para `csStage01EncEnemyData` (`0x64de0`, 24 bytes). `ScrollEncountCheck` (`0x181ac`) nasce com o índice 0. O byte baixo de `csStage01ScrollSet` é 0, então o caminho usado gera enquanto o X do registro é ≤ câmera e para no primeiro X maior. Com a câmera em 0, os dois registros de tipo 3 em X=0 entram e o X=100 corta o resto.

Os dois são:

- Definição 0, personagem 12 de `csEnemyDataTbl` (`0xc55d0`): tipo 6, desenho 8, vida 40. Desenho 8 usa os retângulos de `ciZakoC00Rect` e `ciZakoC01ImgNo` → `WILLIAMS.obm`.
- Definição 1, personagem 4: tipo 4, desenho 6, vida 30. Desenho 6 usa os mesmos retângulos e `ciZakoC00ImgNo` → `BASH.obm`.

A flag `0x2A` tem o bit 1. `EncountEnemyCreate` (`0x178dc`) então faz X = câmera + `+0x47a8` + 20 = 500. O Z é 56 e 8. Os dois nascem fora da borda direita, no ato 0 (`0xb2174`, padrões 0–5, dois quadros). `GT_ZakoMoveCheck` só aplica velocidade se os bits de tecla do zako já estiverem preenchidos. Quem escreve esses bits não foi lido, então eles ficam parados.

`J` é o índice 4 de `PL_PlayerKeySet` (máscara crua `0x20`). `GT_AttackCheck` (`0x35fa8`) chama `GT_BodyDmgTaskCheck` (`0x33c00`): se alguém está a até 90 px em X (`0x5a0000`) e 10 px em Z (`0xa0000`), o ato é 45; senão alterna 28 e 29 pelo bit 2 de `task+0x44`.

- Ato 28 (`0xaa608`): padrões 40, 41, 43–47. `HITA` 2. Um quadro com flag 8.
- Ato 29 (`0xaa688`): padrões 31, 32, 34–38. `HITA` 2.
- Ato 45 (`0xaaa6c`): padrões 48–57. `HITA` 5. `SET_MOVE` com X −393216, deslocado 1 à esquerda (−12 px por quadro) até o `RESET_MOVE`. Quadros 51–54 com flag 8.

A janela de golpe é `(flags & 0x28) == 8`. `HitRangeCheck` (`0xd6fc`) testa três retângulos. O registro 2 de `ciJimmyAtkData` (`0xc67c4`) é (−69, −58, 65, 11, −3, 7), dano 8 em `+0xc`. O registro 5 é (−34, −105, 34, 70, −3, 7), dano 12. A caixa de dor do ato 0 é o índice 1 de `ciZakoC00BodyData` (`0xc6ea6`): (−18, −66, 31, 66, −2, 5). C00 e C01 compartilham retângulos; o ponteiro por tipo em `+0x4dd0` não foi conferido um a um.

`EncountEnemyCreate` grava −1 em `task+0x16d`. `DamageCalc` (`0xdfcc`) passa o meio-word de `+0xc` a `DamageCalcSub` (`0xd85c`). Com `applet+0x2580` em zero e esse byte negativo, o retorno é o dano mais a metade (`add` com `asr #1`): 12 vira 18, 8 vira 12. `+0x25a4` começa em 0 (`0x990c`) e os outros multiplicadores desse campo não entram. Não achei um `str` em `+0x2580`; a conta usa o valor zero de um applet limpo. O bit `0x200000` de `+0x2538` também fica de fora nesse caso. Quando a vida chega a 0, `DamageCalc` chama `0x36a20`. O ato da queda não foi reduzido, então o corpo some em vez de ganhar uma pose inventada.

`ciDrawFlag` (`0x604d0`) troca 0↔1 quando `facing` é 1. A vista espelha o corpo nesse caso. O sombreador que consome a flag não foi aberto instrução por instrução.

## O que ainda não é o jogo

Não há perseguição, nem o ato de queda, nem `FloorWallCheck` (`0x12c28`). O limite é a largura do mapa e Z 0..95. O ângulo do analógico em `GamePad::SetKey` cai nos mesmos quatro índices, mas a ordem dos argumentos do `atan` não foi fechada; o teclado segue o sinal da velocidade. Isto é o primeiro encontro do estágio 1, não o jogo.

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

`J` dispara o índice 4. `--dump quadro.ppm 100 meet` anda até X≥430 e dá um soco: a vida impressa é `hp 22 30` (Williams 40−18, BASH intocado).

Nesta máquina o Wine 9 rodou dois modos do `scene_view.exe`:

- `--dump quadro.ppm 16 right` imprimiu `x 148 z 56 act 1 frame 4 facing 1`.
- Sem `--dump`, a janela Win32 abriu em 970×647 no display `:1`. O desenho usa máscaras `BI_BITFIELDS` (R em 0x000000FF) porque o quadro está em RGBA e o `BI_RGB` de 32 bits trocava vermelho com azul. Segurar Right andou o Jimmy e trouxe o BASH pela direita. Escape fechou a janela. O aviso do Wine sobre `wine32` apareceu; o executável é 64 bits e a janela mesmo assim abriu.

```bash
PYTHONPATH=/workspace python3 -m unittest tests.test_ios_anim tests.test_scene_dump -v
```
