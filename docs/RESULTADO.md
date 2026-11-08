# Resultado — jogo original no Windows

A recriação genérica do Billy continua parada. O módulo cifrado não foi recuperado. A sonda nova está em `tools/envelope_probe.py`.

## Sonda do envelope (código original executado em ARM)

`bootstrap.exe` em `0x7e576`–`0x7e582` grava esta máscara de 16 bytes: `fd1749545ed25ab46698c6c9f43ce891`. Em seguida `kdQueryAttribcv(0x24d)` devolve uma string C. O laço em `0x7e5e6` faz XOR dessa string, repetida até 16 bytes, sobre a máscara. Unicorn executou `0x7e500` até `0x90f98`: com atributo vazio a máscara sai intacta; com a string sintética `FIXTURE-DEVICE!` (não é um id deste aparelho) sai `bb5e11000b801f9922dd9080b779c9d7`. Os dois casos entram em `0x90f98` com o caminho `/data/keystore.dat` e 16 bytes. Isso é senha do keystore, não a chave do exe.

O módulo em memória vira `std::istrstream(char*, int)` (`0x56e54`). Antes da cifra, `seekg` recebe um `fpos` zerado em `sp+0x128`. Os 16 bytes lidos por `istream::read` entram no `SetKey` com o nome `IV` (`0x156a00`) e comprimento imediato 16. O ponteiro e o comprimento da chave do módulo são os de uma `std::string`; o comprimento não é um imediato. No keystore, `0x8f2e4` copia a senha de 16 bytes, faz `resize(0x20, 0)` e usa esses 32 bytes como chave; os 16 bytes lidos do arquivo são o IV, no mesmo wrapper. AES-GCM com a máscara crua, IV no prefixo e tag no sufixo falha em `DoubleDragon.exe` e em `libaudio.so`. A senha não abre o módulo. O `keystore.dat` que essa cifra leria não está nos arquivos disponíveis.

Esta passagem remontou o `res.pak` original e refez o caminho do loader em cima dos bytes, sem rede e sem executar o binário do jogo.

## O que foi conferido

Os três pedaços foram concatenados na ordem `part01`, `part02`, `part03`.

| Arquivo | Tamanho | SHA-256 |
|---|---:|---|
| `res.pak` remontado | 53 382 332 | `43a24c7f0c5354ecd10e09488422046fcc2081018e5b3e6fec3a430e67659291` |
| `Icon_90.png` | 25 670 | digest da assinatura `hd5db0evwdRgOvib7R27J2pod5373K4Gt5+8dN+++QA=` |

Os dois batem com o JSON e com o `signature.xml` do cache (`res.pak` = `Q6JMfwxTVOzRDglIhCIEb8wggQGOWz5v7DpDDmdlkpE=`). `DoubleDragon.exe` e `libaudio.so` também batem com a assinatura e continuam ciphertext (entropia 8,000 e 7,999, sem ELF).

`res.pak` não está cifrado. Começa com o magic `OI` dos OBM e o trailer de índice é `d0502e0300000000`. Há PNG em claro no offset 5 487 568. Os recursos originais estão nesse arquivo. O que falta para jogar é o código do módulo.

## Listing real do cache

O JSON traz o diretório completo, só nome e tamanho. Não há `keystore.dat`, `datastore.dat`, `ssdata.dat` nem `ssstore.dat`. Os únicos itens além do pacote do jogo são `data/save.dat` (400), `data/saveserv.dat` (80), `ticketdata/lsik` (44) e `ticketdata/member_code` (24). Esses quatro não vieram no upload não-pessoal. O `bootstrap.exe` não contém as strings `save.dat`, `saveserv`, `lsik` nem `member_code`. O dex cita `lsik` e `member_code` na ponte Java de ticket, não na decifração nativa. Não há segundo pedido desses arquivos: o listing já mostra que o cache original não tem o keystore nem um XML de rights object.

## Duas camadas, que a leitura anterior misturou

Endereços são VAs do ELF `assets/bootstrap.exe`. Os `.asm` antigos estão +0x10000.

`postLoadHook` de verdade é o símbolo `Security::ExeDRMHandler::postLoadHook`. O veneer em `0x1483d8` salta para o Thumb `0x820b4`. Ele faz duas coisas, em ordem:

1. `0x81f78` → `0x955dc`. Se falha, a string é `Signature check failed`. Isso é o XMLDSig do pacote (o digest que acabamos de conferir), não a chave do módulo.
2. `0x81004`, que em `0x81300` faz `movs r2, #0` e chama o pipeline `0x868f4`.

O pipeline chama `0x8577c` com esse modo. Para `r8<=1`, o que o código faz primeiro é outro documento, não `/ro/cek`:

- Lê o xpath `/ro_response` e exige o xmlns `http://www.acrodea.com/ns/ro_response/1.0` ou `1.1`. Outro xmlns vira `Unknown RO format`.
- Lê `/ro_response/key`. Texto vazio vira `Invalid RO format`.
- Passa esse texto por um decodificador (chamadas `0x847e4`, `0x84934`, `0x78780`, os mesmos construtores de `StringSource` / `BaseN_Decoder`).
- Lê `/ro_response/payload`. Vazio também é `Invalid RO format`.
- Com payload presente, `0x85b98` chama `0x7e500`.

`0x7e500` abre a string `/data/keystore.dat` e a entrega a `0x90f98`, a mesma rotina usada para `datastore.dat`. Antes disso há um laço que faz XOR de um buffer com a constante `oYga9NQJHQqbnZhe` e imediatos embutidos. Isso é máscara do arquivo de keystore, não a chave que decifra `DoubleDragon.exe`. Se a geração de par falha, a string é `Failed to generate key pair!`. Um par novo não recupera um módulo já cifrado para outra chave, e não existe XML `ro_response` em nenhum arquivo enviado para embrulhar essa chave.

`/ro/cek` e `/ro/pek` não são um segundo arquivo solto. No sucesso de `ro_response`, depois do `SetKey` do payload e do filtro, o código monta outro parser XML, lê `/ro/device_id` e só então escolhe o elemento: modo 0 cai em `/ro/cek` (`0x86168`), modo 1 desvia para `/ro/pek` (`0x86350`) e os dois continuam no mesmo leitor. O `postLoad` em `0x81300` passa modo 0. Outro chamador, `0x6aef4`, passa modo 1 para o mesmo pipeline `0x868f4`. Nenhum dos dois grava uma chave local.

A transformação do módulo, depois que a chave existe, é separada:

- `std::istream::read` de 16 bytes (veneer `0x580e4`).
- Vtables `GCM_Final<Rijndael, IsEncryption=false>` e, em `0xe5f9c`, `AuthenticatedDecryptionFilter`.
- `memmove` (`0x572e4`) apaga 0x20 bytes de um buffer de saída.

Isso descreve operações do código. Não prova sozinho o envelope em disco. O exe tem 4 219 674 bytes, `tamanho % 16 = 10`. Os primeiros 16 bytes também são alta entropia (4,0 em 16 bytes, o máximo dessa amostra). Não há magic em claro. Um IV de 16 e uma tag de 16 continuam compatíveis com GCM, porque GCM não exige corpo alinhado a bloco; o resto `% 16` igual a 10 não confirma nem elimina esse layout. O comprimento da chave (16, 24 ou 32) não foi lido de um campo do arquivo.

## Por que um patch de ramo não liga o jogo

O exe em disco é o ciphertext. O `postLoad` não escolhe entre “rodar cifrado” e “rodar decifrado”: ele pede um XML `ro_response` que não está aqui e abre um `keystore.dat` que o listing não tem. Mudar `movs r2, #0` para 1, ou desviar para `/ro/cek`, troca o documento procurado. Não escreve instruções ARM no lugar dos 4 219 674 bytes.

## Execução neste VM

Não houve execução do jogo, de propósito: o binário é desconhecido, ARM, e um runtime com rede poderia chamar o servidor de ticket. A sonda local deu:

- máquina `x86_64`
- `qemu-arm`, `qemu-arm-static`, `qemu-system-arm`, `adb`, `emulator`, `sdkmanager`: ausentes
- `apt-cache policy qemu-user-static` e `qemu-user`: candidato `(none)`
- `/dev/kvm` existe, modo `0660`, `open` retorna errno 13 (o usuário não está no grupo 103)
- o ELF pede o interpretador `/usr/lib/ld.so.1`; o dinâmico do host é o de x86_64

Sem usuário ARM e sem imagem Android, não há sandbox armeabi offline para inicializar o launcher. Isso é limite desta VM, não um teste do jogo.

## Auditoria dos ramos até o SetKey

A recriação jogável continua parada. Esta passagem não repetiu a busca pública e não experimentou a máscara crua de novo como chave GCM. O que foi executado de novo, em ARM, é só o prefixo de `0x85500`. O resto é o fluxo estático do mesmo `bootstrap.exe`, com os testes em `tests/test_ro_audit.py`.

Hipóteses olhadas:

1. O id de aparelho ausente vira uma chave embutida. Não. `0x85500` começa com o literal `12345678901234`, consulta `kdQueryAttribcv(0x24d)` e, se o retorno é nulo, fica com esse literal. Unicorn parou em `0x8553c`: nulo mantém o literal; a fixture `FIXTURE-DEVICE` substitui. O passo seguinte instala a vtable `CryptoPP::SHA256` (`GOT+0x1af4`). Isso alimenta a comparação com `/ro/device_id`. O `SetKey` do payload já aconteceu antes, em `0x85e14`. O literal não é a CEK.
2. O elemento `/ro_response/key` é RSA-OAEP e a chave privada está no binário. A vtable em `GOT+0x17f8` é `PK_FinalTemplate<TF_DecryptorImpl<TF_ES<OAEP<SHA1, P1363_MGF1>, RSA>>>`, com `InvertibleRSAFunction`. Não há PEM, nem inteiro de versão de chave privada RSA, em `bootstrap.exe`, `DoubleDragon.exe` ou `libaudio.so`. Os módulos públicos dos dois certificados do `signature.xml` também não aparecem nesses três arquivos. Chave pública embutida não decifra.
3. Um segundo imediato de 16 bytes em `0x7e848` seria chave de reserva. Ele é gravado e, nos dois lados do `blt` em `0x7e866`, a máscara da senha do keystore escreve por cima do mesmo buffer antes de qualquer `bl`.
4. Atributo nulo, keystore ausente ou entrada ausente cairia numa CEK constante. Atributo nulo chama `kdSetError` e mesmo assim entra em `0x90f98` (`/data/keystore.dat`). Entrada ausente no mapa lança `Failed to generate key pair!`. A escrita do keystore, se falha, lança `Could not write the keystore!`. `0x7c714`, no caminho da ausência, instala `InvertibleRSAFunction`: um par novo não abre ciphertext embrulhado para outro par.
5. Pular a checagem de modo, de xmlns ou de device id recuperaria o exe. Modo maior que 1 lança `Invalid key argument`. Xmlns fora de 1.0/1.1 lança `Unknown RO format`. Key ou payload vazio lança `Invalid RO format`. Device id diferente lança `Device ID does not match with the RO`. O corpo de `DoubleDragon.exe` continua ciphertext. Bypass de autenticação sem a CEK não é recuperação.
6. Os dois chamadores do `postLoad` usariam cifras diferentes. `0x81106` só reordena registradores e cai em `0x868f4`. `0x81304` chama `0x868f4` direto. Os dois passam pelo parser de RO.

O que o ARM e o fluxo mostram sobre a cifra, sem adivinhar layout:

- No payload do RO, `0x85e14` chama o mesmo `SetKey`. O comprimento do IV é o imediato 16. O ponteiro da chave é uma `std::string` e o comprimento é o tamanho dessa string, não um imediato. Em `0x85ea0` o código soma 16 ao ponteiro e subtrai 16 do comprimento antes do filtro `0xe5f9c`. Na chamada, `r3` é `0x10`, `[sp]` é `-1` e `[sp+4]` é `4`.
- No módulo, `0x86a0a` carrega a chave de `[sp+0x280]`. Esse endereço é o `r0` entregue a `0x8577c`. O IV é o buffer dos 16 bytes lidos do stream, comprimento imediato 16 em `0x86a2e`. `[sp+0x284]` é outro slot, zerado e passado como parâmetro separado; não é o ponteiro da chave.
- Uma entrada da vtable GCM, comum à base e ao final de decrypt, devolve a constante 16; outra devolve 12. O chamador passa 16. Não executei `DigestSize()` e não tratei o 12 como IV deste caminho.

Eliminado nos caminhos testados: CEK local, chave privada RSA embutida, fallback do literal de device id como chave, segundo imediato, atributo nulo como chave, par RSA gerado na hora, troca de modo 0/1. Não recuperou CEK nem instruções originais em claro.

Ainda não testado, e não é prova de que seja impossível: o tamanho da string que chega ao `SetKey` do módulo (16, 24 ou 32); o valor que `DigestSize()` devolve quando o filtro recebe `-1`; o ponteiro exato da saída do filtro até o segundo parser XML; um RO original, que não está nos arquivos, embrulhado para uma chave privada que também não está. Não houve brute force.

no flaw found in tested paths.

```bash
PYTHONPATH=/workspace python3 -m unittest tests.test_ro_audit tests.test_envelope tests.test_loader_trace -v
```
