# Resultado — jogo original no Windows

A recriação continua parada. Esta passagem remontou o `res.pak` original e refez o caminho do loader em cima dos bytes, sem rede e sem executar o binário do jogo.

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

`/ro/cek` (`0x1532cc`) e `/ro/pek` (`0x1532c4`) aparecem mais adiante, em `0x86168` / `0x86350`, comparando `r8` de novo. É outro formato de documento. Não está no APK, no pak, no exe, no `config.xml` nem no `signature.xml`.

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

## O que continua desconhecido

- O layout exato dos 16 bytes lidos e dos 0x20 apagados no arquivo do exe (operação vista no código, não um header parseado).
- O tamanho da chave AES.
- O conteúdo de `save.dat` / `saveserv.dat`, que não foi enviado e que o bootstrap não nomeia.
- Se um keystore antigo, fora deste cache, ainda existiria em `/data/data/com.ggee.vividruntime.gg_1327/` num aparelho de 2012. O listing entregue é o cache `RuntimeApps`, e esse keystore não está nele.

Não há ZIP Windows jogável. Os recursos originais estão íntegros; o módulo nativo não.

```bash
PYTHONPATH=/workspace python3 -m unittest tests.test_loader_trace -v
```
