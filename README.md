# Double Dragon (G-gee / Acrodea) — investigação de execução original

Rascunho privado. Não é um remake e não publica o jogo.

O APK `com.ggee.vividruntime.gg_1327` 2.9.0 é o runtime Android (armeabi). O `res.pak` original, remontado dos três pedaços, confere com a assinatura e está em claro. `DoubleDragon.exe` continua ciphertext. O cache listado não contém `keystore.dat` nem um XML `ro_response`, que é o que o `postLoadHook` lê antes de decifrar o módulo.

## O que está verificado

`python3 tools/audit_cache.py` relê o `bootstrap.exe` dentro do APK e confere o cache, sem gravar nos originais e sem rede.

```bash
cd /workspace
PYTHONPATH=/workspace python3 -m unittest tests.test_ro_audit tests.test_envelope tests.test_loader_trace -v
PYTHONPATH=/workspace python3 tools/audit_cache.py \
  --apk investigation/original/DoubleDragon.apk \
  --cache investigation/original/RuntimeApps/gg/1327
```

Os originais em `investigation/original/` ficam de fora do git.

## Windows

Não há build Windows do jogo. O binário do jogo é ARM e ainda está cifrado. Um emulador Android no Windows seria a rota honesta para o runtime original, e só depois que o cache estiver completo. Isso não foi executado aqui.

O leitor do pak, do OBM e das tabelas de texto está em `port/`. Não é um jogo jogável. Instruções: `port/README.md`. O bloqueio do módulo cifrado: `docs/RESULTADO.md`.
