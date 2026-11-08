# Port em C

Há duas peças neste diretório.

O leitor `ddpak` abre o `res.pak` original: índice, OBM e as tabelas de texto que fecham. Não decifra `DoubleDragon.exe`.

`ddslum` é a reconstrução jogável da primeira rua. Usa quadros cortados desse pak. A lógica de luta é aproximada e está documentada em `RECONSTRUCAO.md`. Não é o código original.

```bash
make -C port test
make -C port shot
./port/build/ddslum port/assets/frames.bin --play
make -C port win
```

Setas movem. Z soco, X chute, C especial, espaço pulo, Enter reinicia.
