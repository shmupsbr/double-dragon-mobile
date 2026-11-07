# Resultado — jogo original no Windows

A recriação do Billy foi interrompida e não foi retomada. Este rascunho só investiga o runtime original G-gee/Acrodea.

## O que deu para rodar

Nada do jogo. Não há screenshot de execução: o executável original não chega a ser um ELF.

`DoubleDragon.exe` (4 219 674 bytes) tem entropia 8,000 e não começa com `7f 45 4c 46`. O SHA-256 bate com o `signature.xml` do cache (`1U9rhWiZlbJQJyjZQ2f4E4JjoyYa1tqxeQVxXBJu4cg=`). O mesmo vale para `libaudio.so`. A assinatura confere a integridade do ciphertext; não é a chave.

## Cadeia que o bootstrap realmente executa

Endereços abaixo são VAs do ELF `assets/bootstrap.exe`. Os `.asm` antigos estão deslocados +0x10000 (base do Ghidra). Isso foi conferido de novo: o prólogo Thumb `f0 b5` está em `0x868f4`, não em `0x968f4`.

1. `postLoadHook` em `0x820b4` chama a rotina em `0x81004`.
2. Em `0x81300` há `movs r2, #0` e em seguida `bl 0x868f4`.
3. `0x868f4` repassa esse 0 para `0x8577c`. Lá, `r8 > 1` rejeita o argumento; `r8 == 0` usa o xpath `/ro/cek` (`0x1532cc`); `r8 == 1` usa `/ro/pek` (`0x1532c4`). Os dois caem no mesmo leitor de XML. Não são caminhos de arquivo.
4. Antes da cifra, `std::istream::read` (veneer `0x580e4` → `_ZNSi4readEPci`) lê 16 bytes do stream.
5. O objeto na pilha recebe as vtables `GCM_Base`, `GCM_Base::GCTR`, `GCM_Final<Rijndael, TablesOption=0, IsEncryption=false>` e `BlockCipherFinal<ENCRYPTION, Rijndael::Enc>`. `IsEncryption=false` é decifração GCM. `0xe5f9c`, chamado de dentro de `0x868f4`, instala `AuthenticatedDecryptionFilter`.
6. No retorno, `memmove` (`0x572e4`) apaga 0x20 bytes do buffer de saída se o tamanho passa de `comprimento+0x20`. Isso é outra transformação, no plaintext, não o IV de 16 bytes já consumido do stream.

CBC-CTS não está nessa cadeia. Trocar o `movs r2, #0` escolheria `/ro/pek` ou quebraria a chamada. Não recupera instruções cifradas.

Não há rights object, PEM ou `<ro_response>` dentro do APK nem do cache enviado. O dex só guarda a URL histórica `https://ap.gmo-game.com/app/gs/1.0.0/api/ticket/application.download.ro`. Este ambiente não contactou esse host.

`/data/keystore.dat`, `/data/datastore.dat`, `./ssdata.dat` e `./ssstore.dat` são os arquivos que o bootstrap nomeia para guardar material de chave/RO. Nenhum deles está neste upload. `ticketdata` / `member_code` não foram lidos aqui e não entram nessa cadeia nativa.

## O que falta, com nome

Do pacote do jogo (`RuntimeApps/gg/1327/`), citados no `signature.xml` e ausentes:

| Arquivo | Papel | Digest esperado (Base64 SHA-256) |
|---|---|---|
| `res.pak` | recursos originais; os ZIPs de PNG/WAV já extraídos não substituem estes bytes | `Q6JMfwxTVOzRDglIhCIEb8wggQGOWz5v7DpDDmdlkpE=` |
| `Icon_90.png` | ícone assinado; não decifra o exe | `hd5db0evwdRgOvib7R27J2pod5373K4Gt5+8dN+++QA=` |

Do diretório privado do app `com.ggee.vividruntime.gg_1327` (ou ao lado do cache), se existirem no ZIP completo. Mandar o arquivo, não o conteúdo colado no chat:

- `keystore.dat`
- `datastore.dat`
- `ssdata.dat`, `ssstore.dat`
- `ssdatad.dat`, `ssstored.dat` (o bootstrap também cita `/native/sdcard/`)

Um listing só com nome e tamanho do ZIP de cache já diz se esses arquivos existem. Sem um deles contendo o nó `/ro/cek`, o AES-GCM do loader não tem chave. Isso é ausência do insumo que o código lê, não uma prova de que toda derivação possível foi testada. Nenhuma tentativa de chave foi feita nesta passagem.

## Como seria no Windows, quando o cache estiver completo

O jogo é o runtime ARM do APK, não um port nativo. A rota honesta é um emulador Android armeabi (o manifesto pede o launcher `com.acrodea.vividruntime.ticketloader.TicketLoader`, pacote `com.ggee.vividruntime.gg_1327`), sem rede, com o cache montado em `RuntimeApps/gg/1327/` e os `.dat` no lugar que o bootstrap abre. Isso não foi instalado nem testado: faltam `res.pak` e o rights object, então o emulador não executaria o jogo.

Não há ZIP Windows do jogo original. O teste daqui é só o auditor.

```bash
PYTHONPATH=/workspace python3 -m unittest tests.test_loader_trace -v
```

Saída gravada em `/opt/cursor/artifacts/audit_cache.txt`.
