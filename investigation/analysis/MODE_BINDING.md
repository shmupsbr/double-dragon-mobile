# Binding concreto do modo criptográfico (vtable/GOT)

## Mapeamento ELF corrigido
- PIC base nos ctors DRM = `.got` @ ELF `0x1e0ed0` (= Ghidra `0x1f0ed0`)
- 2º LOAD: `file_off = ELF_VA - 0x8000`
- Leitura GOT: `*(GOT_base + addend)` → ZTV Crypto++

## Pipeline em FUN_968f4 (evidência Capstone)
1. `bl 0x9577c` — resolve chave (`param_2==0` → `/ro/cek`)
2. `movs r2,#0x10` + `bl 0x680e4` — lê **16 bytes** do istream (IV/nonce)
3. Constrói objeto GCM no stack:
   - GOT+`0x142c` → `_ZTVN8CryptoPP8GCM_BaseE`
   - GOT+`0x1808` → `_ZTVN8CryptoPP8GCM_Base4GCTRE`
   - GOT+`0x2028` → `_ZTVN8CryptoPP9GCM_FinalINS_8RijndaelELNS_16GCM_TablesOptionE0ELb0EEE`
     - **Lb0 = `IsEncryption=false` → GCM *decryption***
   - GOT+`0x1884` → `BlockCipherFinal<ENCRYPTION, Rijndael::Enc>` (bloco subjacente)
4. `bl 0xf5f9c` — ctor **AuthenticatedDecryptionFilter**
   - GOT+`0xd74` → `_ZTVN8CryptoPP29AuthenticatedDecryptionFilterE`
   - arg `r3=0x10` → tag/MAC truncado **16 bytes**
5. `bl 0x94bb8` — StringSource / AlgorithmParameters (wrappers, não o modo)

## 947e4 / 94934 / 94bb8 — o que realmente instalavam
Não são GCM/CBC. Typeinfo via GOT:
- `947e4` GOT+0x1968 → `CryptoPP::BaseN_Decoder`
- `94934`/`94bb8` GOT+0x1730 → `CryptoPP::Source`
- GOT+0xe14 → `SourceTemplate<StringStore>`
- GOT+0xa24 → `StringStore`
- GOT+0x1780 → `StringSource`
- GOT+0x17c4 → `AlgorithmParametersTemplate<ConstByteArrayParameter>`

## CBC_CTS excluído do loader DRM
Literais GOT de `CBC_CTS_*` só em ~`0xc80xx`–`0xc82xx`, fora da cadeia `91004→968f4→9577c→94bb8→f5f9c`.

## ProcessData / DecryptAndVerify
Strings presentes em `.rodata` (mensagens de erro Crypto++ `AuthenticatedSymmetricCipherBase`).
Chamada efectiva: ADF + `GCM_Final<Rijndael,...,false>` (decrypt+verify de tag 16B).

## Conclusão de modo
**AES-GCM (Crypto++ `GCM_Final<Rijndael>` decrypt) via `AuthenticatedDecryptionFilter`, IV 16B do stream, tag 16B.**
Tamanho da CEK: Rijndael VariableKeyLength 16..32 (16/24/32). Valor vem de `/ro/cek`, não determinado nesta passagem.
