# Rua do Slum — reconstrução jogável

Isto é uma fatia em C do primeiro trecho de rua. Não é o `DoubleDragon.exe` decifrado e não recupera a lógica original. Os números de dano, alcance, tempo e cadeia são aproximados.

## O que dá para jogar

Jimmy (vermelho, folha `JIMMY_1.obm`) entra pela esquerda numa rua de tijolo, grade e paralelepípedo tirada de `AREA1_CHIP.obm`. Três inimigos da folha `WILLIAMS.obm` vêm da direita, alinham a profundidade e só então batem. Soco, chute e um especial com aura azul (a aura é desenhada por cima; a cor veio do vídeo de iOS, não de um quadro medido). Acerto pisca, recua e, no fim da vida, derruba. A vida fica no canto superior esquerdo, junto do retrato `face_Jimmy.obm`. Depois da primeira leva a câmera destrava, aparece `GO!` apontando para a direita, e a segunda leva, se for limpa, encerra. Enter reinicia. Sem vida, ou com o tempo zerado, é derrota.

## Controles

| Tecla | Ação |
|---|---|
| Setas | Andar e profundidade (cima = fundo da rua) |
| Z | Soco |
| X | Chute |
| C | Especial para cima |
| Espaço | Pulo curto |
| Enter | Reinicia |

Não há controle de gamepad nesta fatia.

## Referências — o que vale e o que não vale

A apresentação segue o longplay de iOS que o usuário passou (`nAj_I2b84ZI`, título “Double Dragon iOS Special Edition Jimmy Longplay”). O autor declara que modificou o jogo para tirar os botões de toque. Não há rótulo de mod no vídeo, e isso não prova que o resto seja a build oficial. Não há filmagem confirmada do Android/G-gee `gg_1327`. A fidelidade com essa build não está garantida.

Os gráficos saem do `res.pak` do Android. O iOS manda na disposição que deu para observar: Jimmy de vermelho, rua “Slum”, inimigos que alinham na profundidade, soco com brilho, recuo e queda, especial com aura azul, `GO!` para a direita quando a leva acaba, HUD com retrato e vida no alto à esquerda, tempo e crédito perto do centro, fase no alto à direita, indicador de corrente e uma área de arma vazia.

A comparação com os primeiros cinco minutos do Zeebo (`RDfiM17x8UY`) não entra como medida desta fatia. O Zeebo tem outra arte e outro HUD: retrato de vida embaixo à esquerda, sem corrente nem arma visíveis naquele trecho, sem os diálogos de retrato de antes e depois do Abobo, e um “missão cumprida” em português por volta de 04:13. O lutador de lá é o Billy azul, então o especial também pode ser diferença de personagem. Só o plano alto é comum (profundidade, golpe, queda, leva seguida de câmera, objeto, Abobo). Nada disso foi copiado como número ou layout.

A compilação “All Versions” deixa de fora, nas próprias palavras do autor, o remake de Android/iOS da Brizo de 2011. O trecho de Trilogy 2013 não serve de referência.

## Observado no vídeo de iOS, e o que foi refeito

Observado, em tempos aproximados, não medidos quadro a quadro: Jimmy selecionado; rua de tijolo e chão de pedra; entrada pela esquerda; inimigos pela direita que alinham antes de bater; soco, brilho, recuo e queda; especial com aura azul; `GO!` e câmera; mais tarde chicote, barris e Abobo, que esta fatia não inclui.

Reimplementado, com valores nossos: velocidades, dano (soco 8, chute 12, especial 16), vida (48 e 36), alcance, o portão de profundidade de 16 px, o tempo de 90 segundos, a pontuação `100 × corrente` e a corrente que expira em cerca de um segundo. O pulo é um arco curto. A palavra `GO!` e a seta são desenhadas, não são um sprite recortado do jogo. A fonte do HUD também não é a fonte original.

Os quadros foram cortados à mão nas folhas, porque o pak não tem tabela de animação. Vários desenhos estão com braço ou perna separados do corpo; o corte fica no pedaço conexo que ainda se lê como o golpe. A expansão de cor do OBM para RGBA5551 continua inferida.

## O que esta rodada não faz

Abobo, chicote, barris, diálogo de retrato, as fases seguintes, som (os wav existem no pak, mas nenhum foi identificado como o soco) e gamepad. O exe cifrado não entra no pacote.

## Build

Linux, com o pak remontado para gerar os quadros:

```bash
make -C port test
make -C port shot
make -C port win
```

Jogar:

```bash
./port/build/ddslum port/assets/frames.bin --play
```

`--dump foto.ppm --ticks 150` grava um quadro da demonstração sem janela. `--demo` abre a janela, anda e bate sozinho, e fecha.

O teste `port/build/test_sim` cobre movimento, profundidade, o soco que erra fora da linha, o inimigo que alinha antes de bater, o pulo, a vitória nas duas levas e a derrota.

Windows x64 sai em `port/build/ddslum.exe`, junto de `frames.bin`. Não precisa de DLL. Não foi executado aqui: não há Wine nesta máquina. O binário Linux da mesma cena foi executado.
