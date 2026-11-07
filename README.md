# Double Dragon (G-gee / Acrodea) — investigação de execução original

Rascunho privado. Não é um remake e não publica o jogo.

O APK `com.ggee.vividruntime.gg_1327` 2.9.0 é o runtime Android (armeabi). O jogo em si está no cache `RuntimeApps/gg/1327/`. Com os arquivos deste rascunho o executável original **não abre**: `DoubleDragon.exe` e `libaudio.so` são ciphertext, e faltam `res.pak` e o rights object de onde o loader lê a chave.

## O que está verificado

`python3 tools/audit_cache.py` relê o `bootstrap.exe` dentro do APK e confere o cache, sem gravar nos originais e sem rede.

```bash
cd /workspace
PYTHONPATH=/workspace python3 -m unittest tests.test_loader_trace -v
PYTHONPATH=/workspace python3 tools/audit_cache.py \
  --apk investigation/original/DoubleDragon.apk \
  --cache investigation/original/RuntimeApps/gg/1327
```

Os originais em `investigation/original/` ficam de fora do git.

## Windows

Não há build Windows do jogo. O binário do jogo é ARM e ainda está cifrado. Um emulador Android no Windows seria a rota honesta para o runtime original, e só depois que o cache estiver completo. Isso não foi executado aqui.

Detalhe e a lista do que falta: `docs/RESULTADO.md`.
