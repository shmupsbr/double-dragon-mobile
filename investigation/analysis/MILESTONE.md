# Milestone — modo GCM amarrado; CEK ausente

## Feito
1. **Modo criptográfico concreto:** AES-GCM decrypt via Crypto++ `GCM_Final<Rijndael,...,Lb0>` + `AuthenticatedDecryptionFilter` (tag 16B); IV 16B lido em `968f4`; CEK de `/ro/cek` em `9577c` (`param_2==0`).
2. GOT/vtable: PIC base = `.got` ELF `0x1e0ed0`; mapping RW `VA-0x8000`.
3. `94bb8`/`947e4`/`94934` = pipeline Source/StringSource/params — **não** o modo.
4. CBC_CTS **fora** do path DRM do EXE (~`0xc80xxx`).
5. ticketdata lsik/member_code: tokens de conta (Java); **não** CEK; trials GCM auth 0/24.
6. Nenhum ELF/container recuperado validado.

## Bloqueio exacto
Falta o conteúdo de **`/ro/cek`** (ficheiro/RO de licença não presente no cache). Sem essa chave, decrypt scratch não autentica.

## Evidência
- `MODE_BINDING.md`, `PROGRESS.md`, `FUN_968f4_thumb.asm`, `scratch/trial_report.txt`
- Sem artefactos recovered_*.bin
