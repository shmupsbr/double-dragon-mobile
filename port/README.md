# Camada de recursos — port em C

Isto **não é o jogo**. É a parte do port de preservação que dá para recuperar dos bytes do `res.pak` original. O módulo `DoubleDragon.exe` continua ciphertext; a lógica de luta, fases e IA não está neste diretório e não foi inventada.

## O que está recuperado

O índice do pak fecha exatamente no rodapé (`u32` do offset do índice, depois `u32` zero). São 292 arquivos e 7 pastas, sem sobreposição. Seis tabelas `*.dat` de texto (conquistas, perfil, tutorial, EN e JP) são listas contíguas de offset e tamanho. Os dois `HELP` usam outro layout e o leitor recusa. OBM (`OI`) tem cabeçalho, paleta e índices com tamanho exato. A expansão da paleta para RGBA5551 é **inferida**, não um campo do arquivo.

Os `u16` antes das strings são guardados. O que cada um significa no jogo não está na tabela.

## O que não está aqui

Quadros de animação, hitboxes, física e o comportamento do Billy. Isso, se existe, está no executável cifrado. Pular a checagem de licença não decifra esse arquivo.

## Build

Linux, testado contra o pak remontado:

```bash
make -C port test PAK=investigation/cache1327/res.pak
```

Windows x64, cross-compilado com MinGW (não executado nesta máquina Linux):

```bash
make -C port win
```

Gera `port/build/ddpak.exe`. Uso:

```text
ddpak list res.pak
ddpak text res.pak res/Language_EN/ddiPhoneAchieve_En.dat
ddpak obm res.pak res/obm2/face_billy.obm face.ppm
```

O PPM descarta o alfa. A paleta impressa pelo comando é a inferida.
