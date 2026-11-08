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

A flag `0x2A` tem o bit 1. `EncountEnemyCreate` (`0x178dc`) então faz X = câmera + `+0x47a8` + 20 = 500. O Z é 56 e 8. `COM_SeqCall` (`0x16be0`) zera os bits de tecla e chama o comando em `task+0x14c`. O índice 0 é `COM_Ready` (`0x15e5c`). Para os tipos 4 e 6, se `|dX| > 60` e `(rng & 0xf) == 0`, o comando passa a 3 (`COM_MoveX`, `0x1692c`) e anda a `ciZakoMoveData[0]` = −262144. Se `|dZ| > 3` e `(rng & 7) == 0`, o comando 4 (`COM_MoveZ`) usa ±262144. Dentro de 100 px em X e 7 px em Z, `GT_ZakoAttackCheck` (`0x35330`) escolhe o ato 45. O gerador é o MT19937 temperado em `0x13770` e deslocado 1 bit em `0x137a4`. A semente aqui é 1; o jogo usa o relógio.

`J` é o índice 4 de `PL_PlayerKeySet` (máscara crua `0x20`). `GT_AttackCheck` (`0x35fa8`) chama `GT_BodyDmgTaskCheck` (`0x33c00`): se alguém está a até 90 px em X (`0x5a0000`) e 10 px em Z (`0xa0000`), o ato é 45; senão alterna 28 e 29 pelo bit 2 de `task+0x44`.

- Ato 28 (`0xaa608`): padrões 40, 41, 43–47. `HITA` 2. Um quadro com flag 8.
- Ato 29 (`0xaa688`): padrões 31, 32, 34–38. `HITA` 2.
- Ato 45 (`0xaaa6c`): padrões 48–57. `HITA` 5. `SET_MOVE` com X −393216, deslocado 1 à esquerda (−12 px por quadro) até o `RESET_MOVE`. Quadros 51–54 com flag 8.

A janela de golpe é `(flags & 0x28) == 8`. `HitRangeCheck` (`0xd6fc`) testa três retângulos. O registro 2 de `ciJimmyAtkData` (`0xc67c4`) é (−69, −58, 65, 11, −3, 7), dano 8 em `+0xc`. O registro 5 é (−34, −105, 34, 70, −3, 7), dano 12. A caixa de dor do ato 0 é o índice 1 de `ciZakoC00BodyData` (`0xc6ea6`): (−18, −66, 31, 66, −2, 5). C00 e C01 compartilham retângulos; o ponteiro por tipo em `+0x4dd0` não foi conferido um a um.

`InitOptionData` grava 1 em `+0x76c` e `LoadOptionData_fromSaveArray` copia isso para `+0x24e2`. `InitSaveData` zera a palavra em `+0x24e0` antes. A palavra de dano `+0x2580` só é escrita em `ReadyCharSelectScreen` se `+0x2538` tiver o bit `0x800000` (grava 1) ou `0x200000` (copia o byte). Nenhum dos dois bits aparece no caminho do estágio 1, então `DamageCalcSub` fica no braço de dificuldade 0. Defensor com `+0x16d` negativo (o inimigo nasce com −1) soma a metade: 12 vira 18. O Jimmy nasce com o byte 0, então perde um quarto: o soco zako de 11 vira 9. A vida do Jimmy sai da tabela em `0xc54ac`, entradas 0 e 1 = 100.

O byte `+0x11` desses socos é 5. `DamagedCheck` instala `GT_FukiUpDamage` (`0x32c60`). Com a velocidade Y ainda ≤ 0 o corpo sobe. No primeiro quadro em que ela fica positiva, `0x32ca8` instala `GT_FukiDownDamage` (`0x32b90`) e o ato 12. No chão, com `+0x44` sem `0x400000`, `FukiDown` chama `GT_SetBoundMove` e troca o proc para `GT_BoundDamage` (`0x2e5d8`), ato 15. Quando esse hop volta ao chão, `BoundDamage` instala `GT_Down` (`0x32a3c`) no ato 16. O `STOP` desse ato é a transição: vida restante vai para o proc `0x32924` no ato 17 e, no `STOP` seguinte, `GT_ReadySet` devolve o ato 0; vida 0 vai para `GT_KO` (`0x31f40`) no ato 23. `GT_KO` devolve 2 se `+0x16d` é negativo, e o laço em `0xc7f2` desliga a task no bit 2 de `+0x1c`. O bit `0x400000` só é somado por `FloorAttrCause` (`0x128dc`, constante `0x400200`) em chão tipo 1, que também zera a vida. A rua é tipo 0, então esse ramo (atos 23/42 por essa flag) não corre. `0x36a20` segue sendo `GT_BossKOCheck` e não escolhe pose.

`ciDrawFlag` (`0x604d0`) troca 0↔1 quando `facing` é 1. A vista espelha o corpo nesse caso. O sombreador que consome a flag não foi aberto instrução por instrução.

## O que ainda não é o jogo

`DrawZSort` (`0xd50c`) desenha o maior Z primeiro. Z igual usa o id da task; o id menor fica na frente. `COM_MoveZ` sobe o BASH até a linha do Jimmy, e aí os dois ocupam a mesma profundidade.

`FloorWallCheck` (`0x12c28`) empurra o eixo que entrou numa célula cujo height passa da elevação dos pés. No estágio 1 o registro 0 vale 99999 em todo o subtile (formato 0 é zero), e a fileira z 64..79 é esse registro na calçada inicial. Z 63 passa; Z 64 não. O registro 2 é formato 5: bit 1 escolhe 99999 e bit 0 escolhe 0. Na descida, a varredura olha os subtiles abaixo antes de gravar `(Z & ~15) - 1`.

A câmera é `MapScrollCheck` (`0x124b0`). Com um jogador ela segue a margem de `cStage01ScrollPush`: esquerda 80, direita 240, no máximo 12 px por quadro. Depois o índice em `+0x47d4` escolhe `csStage01ScrollLock`. O registro 0 trava a câmera em 0. O registro 1 deixa ir até 191. `ScrollEncountCheck` (`0x181ac`), no modo byte 0, consome a fila enquanto o X do registro é ≤ câmera. A fileira de X 100 só entra depois que a câmera anda. Ela anda quando os dois inimigos com o bit `0x20` terminam `GT_KO` e o gatilho 1 de `csStage01EncData2` soma o índice. Depois do lock 2 a câmera chega a 303 e nascem a Linda (char 1, draw 4, X 720, vida 25) e um Williams (X 752). O word +8 desses registros é 256 ou 288, então `GT_CheckFloor` os solta com velocidade Y −262144. Em 687 nascem um Bash (X 880) e um Bolo (X 912) e o kind 5 trava a câmera no registro 4. Os objetos kind 9 de scroll 100 não são destruídos no soco. `GT_Stage01DoorReady` (`0x2ccb0`) e `GT_Stage01WallReady` (`0x2d8e4`) olham `applet+0x47f4`. O kind 8 de `SetEncount` grava o param nessa palavra. A chave 2 (quatro abates da cota de 303) grava 1 e a porta, no ato 1, dispara a extra 10 quando o quadro com `0x10000000` começa. A chave 10 nasce o inimigo 9 e um timer kind 10 de 40 quadros para a chave 11. Os dois abates disparam a chave 3, que grava a fase 2. A parede, no STOP do ato 3, dispara a extra 20 e as chaves 21 e 22 nascem os inimigos 12 e 13, este o char 31 (draw 19, `MACHO_AFRO.obm`, vida 50). A chave 4 é kind 5 e soma o lock. O objeto de 687 tem extra 30 e o estágio 1 nunca grava a fase 3 ou 4, então ele não abre. Kind 4 (item) e o kind 10 de scroll 1055 continuam de fora: a fase 2 não foi alcançada por entrada, então a condição do 1055 não chegou. O reaction 2 da Linda é `GT_NokeDamage` (`0x32cf4`): no STOP ele chama `GT_ReadySet` sem ler `+0x134`. Isso não é uma varredura de todo o jogo; o reaction 5 continua em `GT_KO`. RNG segue com semente 1. Não há HUD, áudio, continue depois do `GT_KO` do jogador, nem a agenda original de chamadas do MT.

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

O char 12 da primeira fileira é draw 8, `BOLO.obm`. O char 4 é draw 6, `BASH.obm`.

- `J` é o botão lógico 4 (soco).
- `K` é o 5. Longe, ato 30. Perto, ato 46. No ar, ato 31.
- Espaço é o 6. `GT_MoveCheck` lê a borda `0x40` e chama `SetMove2`. Esquerda guarda `ciBillyMoveData` +0x18/−0x1c: velocidade X −393216 e aceleração X +16384. Direita nega as duas. O Y não espelha: velocidade −1048576, aceleração +212992. O ato 3 sobe e o ato 5 aterrissa. `MoveX` soma a aceleração na velocidade e não zera quando o sinal troca.
- Dois toques na mesma direção dentro de 16 quadros são o dash. O registro +0x28 é −1048576; a direita nega. O dash continua enquanto a direção fica presa e para quando solta. Ato 6.
- `U` é o botão 7. Com cima, ato 50 (registro 14, dano 12, reaction 5). Com baixo, ato 48 (registro 13, dano 15, reaction 8). Sem direção o agarre não foi recuperado. O registro 13 grava `+0x174` = byte `+0x0f` = 3 e `+0x175` = facing XOR byte `+0x10` (1). `GT_SetFukiMove` lê `ciFukiData` registro 3: X +131072, aceleração X 0, Y −458752, aceleração Y +212992. `+0x175` diferente de zero nega só o X. O proc é `GT_BackFukiDamage` no ato 18. No chão entra `GT_Down` no ato 41. Vida restante segue para `GT_Okiagari` no ato 20 e depois `GT_ReadySet`. Vida 0 segue para `GT_KO` no ato 24 e a task sai.
- `C` é o botão 9. O ato 25 faz `NEXT` para o 26 no mesmo `ActionSub`. O 26 segura o padrão 11 por 45 quadros. No `STOP`, `GT_Crouch` põe o ato 27, e o `NEXT` dele volta ao ato 0. Direção ou pulo saem por `GT_MoveCheck`. Os golpes de agachamento (atos 0x33 e 0x34) não estão ligados.
- O botão 7 sem direção chama `GT_DownDmgTaskCheck`. A lista que ele lê (`+0x3258`, contagem `+0x365a`) não é a lista que `GT_DownDmgRequest` enche (`+0x2e58`, contagem `+0x3658`). Sem esse elo o ato 67 não entra. `GT_NageRangeCheck` existe e não aparece no `GT_AttackCheck`.
- Os atos aéreos 57, 59 e 70 continuam de fora: o 57 não tem a flag de golpe 8, e o 59 e o 70 não fecham um golpe legível.

`--dump 80 right` anda até x 278 com `cam 0 lock 0`. `--dump 4 up` para em z 63. `--dump 2 jumpr` vai a x 66 com `facing` 1. `--dump 2 jumpl` vai a x 54 com `facing` 0. `--dump 12 dash` anda 16 px por quadro. `--dump 8 b7` deixa o alvo com 22 de vida e no tombo do reaction 5. `--dump 2 b7d` ainda não acertou. `--dump 4 b7d` está em x 72 (facing direita, +2 px). `--dump 4 b7l` está em x 48. `--dump 45 b7d` voltou ao ato 0 com 18 de vida. `--dump 60 b7k` desligou o alvo que entrou com 22. Esses quatro são fixtures de uma task; o `route` não aperta o botão 7. `--dump 307 stand` ainda está em câmera 96. `--dump 308 stand` passa de 100 e nasce `(612,56)`, `(656,32)`, `(612,8)`. `--dump 780 stand` traz a Linda e o Williams em 303, ainda na queda. O reaction 2 dela (ato 7, hitstun 0, timer 5 no jogador) está em `--dump 24 react` (`pm 10`, vida 94) e o quadro 30 volta a `pm 0`. O soco do Jimmy em seguida a derruba.

Nesta máquina o Wine 9 rodou o `scene_view.exe` de 64 bits (avisou `wine32` e seguiu):

- `--dump 40 up` parou em z 63.
- `--dump 387 stand` imprimiu `612 656 612` e `cam 108 lock 2 nf 5`.
- `--dump 24 react` imprimiu `pm 10` com vida 94. `--dump 30 react` voltou a `pm 0`.
- `--dump 700 crowd` imprimiu `hp 0 40 30 ... pm 4`.
- Sem `--dump`, a janela abriu no display `:1` com os dois inimigos já na calçada. Escape fechou.
`CTask::Caller` (`0xc834`) anda as prioridades de 0 para cima. Dentro de uma prioridade a task mais antiga vem primeiro. O jogador nasce na prioridade 2 (`GT_CreatePlayerTask`) e o inimigo na 3 (`EncountEnemyCreate`), então o proc do Jimmy corre antes do dos inimigos. O golpe e o primeiro `MoveX` do defensor cabem no mesmo quadro.

O par da porta não está imune. A flag `0x10` só corta o empurrão. O passeio que fica dez pixels fora do Z deles acerta, e as duas mortes abrem a fase 2 com inimigos em x 992. O Jimmy morre nessa leva. Isso é o roteiro, não uma mudança de regra.

Depois do lock 5 a câmera chega a 1055. A fileira traz um kind 5, uma cota de 2 sem chave seguinte, dois inimigos (14 e 15) e o kind 10: 100 quadros e a chave 5. A chave 5 pede o inimigo 19: registro 19 de `csStage01EncEnemyData`, char 40, tipo 24, draw 26, vida 200, X 1320, Z 88, entrada 6, extra 99. A entrada 6 de `EncountEnemyCreate` (`0x17afa`, meia-palavra 6) instala `GT_EnemyOutEntry` (`0x2d92c`), não `GT_AboboAppearEvent`. Ele anda com o move do tipo até o X gravado em `+0xc8` e então chama `GT_JumpSet` (`0x33ae8`). `EncountEnemyCreate` copia o extra para `+0x144`. No `GT_KO` a chave 99 espera 30 quadros e a chave 100, kind 7, soma `0x1000` em `+0x2538`. Isso é o bit, não a tela de clear.

`csPlayerCharLoadData` (`0xa84ec`, 12 bytes) tem o tipo no campo 0, o draw no campo 1 e o recurso no campo 2. O registro `(26, 28, 41)` é `ABOBO.obm`. Esse par é o char 42 (tipo 26, draw 28, vida 230). Nenhum registro dessa tabela tem tipo 24 ou draw 26, então o corpo do inimigo 19 não é desenhado e `ABOBO.obm` não entra no lugar dele. `--dump 1 s1055` é o despacho isolado, com o lock já em 5. `--dump 101 s1055` marca `us 19 ch 40`. `--dump 30 bosskey` marca `sc 1` sem ser uma luta. Nenhum dos dois é o passeio contínuo. Kind 4 (itens) e os kind 9 de scroll 0 continuam de fora.
- `GT_EnemyWallEntry` anda em Z com −131072. O primeiro passo cai em 74, dentro da fileira sólida, e `FloorWallCheck` grava `(Z & ~15) - 1` = 63. Os dez passos seguintes descem a calçada e param em Z 43 (`--dump 2 entry`, `--dump 12 entry`). O formato 5, que escolhe um subtile livre dentro da célula, não está varrido.

```bash
PYTHONPATH=/workspace python3 -m unittest tests.test_ios_anim tests.test_scene_dump -v
```
