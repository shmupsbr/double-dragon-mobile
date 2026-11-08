# double-dragon-mobile

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

A base agora é o `ddiPhone` iOS, com `cryptid` 0. Não é código-fonte: o slice armv7 está legível e os símbolos batem com as tabelas. A cena 1 está em `ios/`: o primeiro encontro (Williams e BASH em X=500), o soco do Jimmy e o dano lido de `DamageCalc`. Como gerar o pacote e o que ainda não foi recuperado: `ios/README.md`.

O `port/ddslum` é o protótipo antigo e não vale como regra de luta. O módulo Android continua cifrado: `docs/RESULTADO.md`. Os originais não entram no git.
