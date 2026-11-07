# Progress log — pós-RO até payload (continuação)

## Correção de modelo mental
- `FUN_00096df8` **não** é decrypt separado: só reordena args e chama `FUN_000968f4`.
- Ambos os ramos de `FUN_00091004` (com ou sem `param_1[1]`) passam por `968f4` → `9577c`.
- `FUN_0009577c` é grande (Ghidra corta no EH); Capstone mostra corpo até ~0x96500+.
- Em `9577c`, `param_2` (`r8`):
  - `>1` → exceção `"Invalid key argument"`
  - `==0` → caminho que referencia **`/ro/cek`** (PIC `0x96168` → string `0x1532cc`)
  - `==1` → outro ramo RO
- Chamadas desde `91004` passam `param_3=0` para `968f4`, logo `9577c` entra no caminho **`/ro/cek`**.

## Sequência em `FUN_000968f4` (Capstone, evidência direta)
1. `bl 0x9577c` — bind/parse chave (RO / cek)
2. `movs r2, #0x10` + `bl 0x680e4` — **leitura de 16 bytes** do `istream` do buffer do ficheiro (candidato forte a IV/tag GCM; tamanho = bloco AES)
3. construção de objetos C++ (`b71e8`, vtables)
4. `bl 0x94bb8` — pipeline de cifra (ctors encadeados, chama `9bcd4`, `88340`, …)
5. depois `9c8b4` / `9cfc0` / `9c770` / `9c470` / `9c1ec`

## `FUN_00091004` pós-968f4 (decompilação Ghidra prévia)
- `string::assign` do resultado
- `memcpy` para `param_3` (buffer do executável)
- `memmove` se `len+0x20 < size` — **remove 32 bytes** do meio/cabeça do buffer
- Isto é a transformação visível do conteúdo em memória após o pipeline de cifra

## `FUN_00088340`
- Se `param_2 < param_4` lança `"memcpy_s: buffer overflow"`; senão `memcpy`.
- Helper de cópia com bounds, **não** o algoritmo.

## Ainda não amarrado por xref de vtable
- Objetos Crypto++ `GCM_Final<Rijndael>`, `AuthenticatedDecryptionFilter`, `CBC_CTS_Decryption` existem (dynsym/strings).
- Construtores em `947e4`/`94934`/`94bb8` escrevem pares de ponteiros estilo vtable (+8 / +0xdc|+0xe8), tipicamente CryptoPP, mas o **nome concreto do tipo** no GOT ainda não foi lido com sucesso (endereços de slot além do ficheiro mapeado / reloc).

## DoubleDragon.exe
- Continua opaco; strip de 0x20 + leitura prévia de 0x10 no stream batem com header 32B + IV 16B **como hipótese alinhada ao código**, não como prova do modo GCM vs CBC-CTS.

## Dependência
- Material em `/ro/cek` (e/ou `/ro_response/key`) — de RO/licença. Não está no zip do jogo. `ticketdata`/`member_code` **não** inspecionados.

## Artefactos novos
- `FUN_9577c_FULL_thumb.asm`, `FUN_968f4_thumb.asm`, `FUN_94bb8_thumb.asm`, `FUN_88340` via `cek_and_payload.txt`
- `PROGRESS.md` (este ficheiro)

## Continuação — binding GOT + ticketdata + trials scratch

### Modo amarrado
- Ver `MODE_BINDING.md`: **AES-GCM decrypt** (`GCM_Final<Rijndael, Tables=0, IsEncryption=false>`) + **AuthenticatedDecryptionFilter** (tag 16).
- Callsites: `968f4` → GOT GCM_Base/GCTR/GCM_Final_Lb0/BlockCipherFinal; `f5f9c` → ADF.
- `947e4`/`94934`/`94bb8` = Source/StringSource/BaseN_Decoder/AlgorithmParameters — não o cipher mode.
- CBC_CTS só ~`0xc80xxx`, fora do path DRM do EXE.

### ticketdata (autorizado, só metadados)
- `lsik`: ASCII-b64 44B → decoded 32B (`private/lsik.decoded` mode 600)
- `member_code`: ASCII-b64 24B → decoded 16B (`private/member_code.decoded` mode 600)
- Dex: strings `lsik`/`member_code`/`ticketdata`/`local_lsik`/`setSharedKey value|error`/`getKey` — bridge/ticket API (`api/ticket/application.download.ro`), **sem** `/ro/cek` no dex.
- `bootstrap.exe`: **zero** ocorrências de `lsik`/`member_code`/`ticketdata`.
- Classificação: **IDs/tokens de conta**, não evidência de CEK nativa.

### Trials scratch (sem execução; só prova de integridade/formato)
- Originais preservados; cópias em `scratch/*.orig`.
- 24 layouts AES-GCM (key∈{lsik32, lsik16, member16}, iv∈{file16,file12,member,lsik[16:]}, tag posições standard) em DoubleDragon.exe + libaudio.so.
- **GCM_AUTH_OK_COUNT = 0**; **VALID_RECOVERED_COUNT = 0** (sem ELF ARM válido).
- Controlo CBC: corre mas `elf_magic=false` (sem prova de formato).
- Relatório: `scratch/trial_report.txt` (chmod 600; sem segredos).

### Dependência em falta (evidência)
- Material **`/ro/cek`** (e/ou payload RO via `/ro_response/key`) **ausente** do cache fornecido.
- `config.xml`/`signature.xml` sem CEK (só app meta + XMLDSig).
- Sem `keystore.dat` / `datastore.dat` / `ssdata.dat` no extract.
- lsik/member_code **não** substituem a CEK (auth GCM falhou em todos os layouts testados).

### Artefactos
- `MODE_BINDING.md`, `scratch/trial_report.txt`, `private/*` (600), actualização deste PROGRESS.
