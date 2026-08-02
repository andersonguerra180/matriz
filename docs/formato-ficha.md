# Formato de definição de ficha (YAML)

Referência normativa do formato descrito em §6.2 da especificação. Todo
arquivo em `fichas/*.yaml` segue este formato. O parser (`Source/Ficha`)
valida um arquivo de definição contra estas regras antes de aceitá-lo — uma
definição inválida é rejeitada na carga, nunca falha em silêncio depois.

Uma única tela (Etapa 3) renderiza qualquer esquema que passe nesta validação.
Adicionar tipo de mídia novo é escrever um arquivo aqui, não recompilar.

## Estrutura de nível superior

Toda definição tem:

```yaml
tipo: <identificador>       # obrigatório. Casa com item.tipo_midia no banco.
rotulo: <texto>             # obrigatório. Nome exibido na interface.
niveis: [raiz]               # opcional. Ausente = só nível raiz (a maioria dos tipos).
                              # Presente = lista de níveis, ex.: [release, faixa].
grupos: [...]                 # usado quando niveis está ausente (um só nível)
<nome_do_nivel>:              # usado quando niveis está presente, um bloco por nível
  campos: [...]
arquivos_esperados: [...]     # opcional
```

Um tipo tem **ou** `grupos` (nível único, ex.: `fita_rolo`) **ou** um bloco
por nome de nível listado em `niveis` (nível aninhado, ex.: `release` com
`release:` e `faixa:`). Nunca os dois. `sample`, `foto` e a maioria dos tipos
físicos usam `grupos`. Só `release` usa níveis aninhados nesta versão.

### `grupos`

Lista de agrupamentos visuais de campos. Cada grupo:

```yaml
- rotulo: <texto exibido como cabeçalho do grupo>
  campos: [<campo>, ...]
```

Grupos existem só para organizar a tela — não têm efeito em validação ou
armazenamento. `item_campo.nivel` para um tipo sem `niveis` é sempre `'raiz'`.

### Níveis aninhados

Quando `niveis: [release, faixa]`, cada bloco de nível tem sua própria lista
de `campos`. O nível raiz da lista (`release`) grava com `item_campo.nivel =
'raiz'` e `nivel_indice = 0`. Um nível repetido (`faixa`) grava uma linha de
`item_campo` por ocorrência, com `nivel = 'faixa'` e `nivel_indice` = posição
(1, 2, 3...). Esta versão do formato suporta exatamente dois níveis de
profundidade (raiz + um nível repetido) — o suficiente para release/faixa,
que é o único caso descrito na especificação.

## Campo

```yaml
- id: <identificador>              # obrigatório. Único dentro do nível. Vira item_campo.campo_id.
  rotulo: <texto>                  # obrigatório. Rótulo exibido.
  tipo: <tipo>                     # obrigatório. Ver "Tipos de campo" abaixo.
  obrigatorio: true|false          # opcional, default false.
  opcoes: [<valor>, ...]           # obrigatório se tipo for opcao ou opcao_livre.
  colunas: [<nome>, ...]           # obrigatório se tipo for tabela.
  validacao: <nome>                # opcional. Ver "Validações nomeadas".
  visivel_se: <expressão>          # opcional. Ver "Visibilidade condicional".
  herda_do_projeto: true|false     # opcional, default false.
  preenchido_por: <fonte_tecnica>  # opcional. Nome do processo de leitura técnica (estágio 1, §7.2).
  sugerido_por: <modelo>           # opcional. Nome do modelo de IA que sugere valor (P3, §9).
  afeta: [<efeito>, ...]           # opcional. Ver "Efeitos colaterais".
  alerta_se_true: <texto>          # opcional. Só para tipo booleano — mensagem de alerta quando valor = true.
  gerar_em_sequencia: true|false   # opcional. Só ISRC de faixa — gera próximo código a partir do registrante do projeto.
```

Um campo tem no máximo uma origem automática entre `herda_do_projeto`,
`preenchido_por` e `sugerido_por` — são mutuamente exclusivas. Um campo sem
nenhuma das três é preenchido manualmente do zero pelo operador.

## Tipos de campo

| `tipo` | Armazenamento em `item_campo.valor` | Observação |
|---|---|---|
| `texto` | texto livre | — |
| `numero` | número decimal, como texto (`"120.5"`) | — |
| `inteiro` | número inteiro, como texto (`"3"`) | — |
| `data` | `YYYY-MM-DD`, como texto | — |
| `booleano` | `"true"` / `"false"` | dispara `alerta_se_true` quando aplicável |
| `opcao` | um dos valores de `opcoes`, exato | fechado — a interface não deixa digitar valor fora da lista |
| `opcao_livre` | qualquer texto | autocompleta com `opcoes` e com valores já usados no projeto; aceita criar novo |
| `lista_pessoas` | JSON: `[{"nome": "...", "papel": "..."}, ...]` | usado em compositores, participação |
| `tabela` | JSON: `[{"<coluna>": "...", ...}, ...]` | requer `colunas`; usado em splits |

## Validações nomeadas

`validacao` referencia uma rotina fixa no parser, não uma expressão livre:

| Nome | Regra |
|---|---|
| `isrc` | Estrutura `CC-XXX-YY-NNNNN` (país, registrante, ano, designação). O software valida formato e detecta duplicata dentro do projeto; **não** emite código de registrante — isso vem da agência nacional (texto de ajuda obrigatório na tela, §6.4). |
| `ean13` | Dígito verificador EAN-13 válido. |
| `soma_100` | Só em campo `tipo: tabela`: a coluna `percentual` de todas as linhas deve somar exatamente 100. |

Novas validações nomeadas exigem código novo no parser — não são
extensíveis por YAML. Se a especificação de um tipo de ficha precisar de uma
validação que não está nesta tabela, é preciso parar e definir a regra aqui
antes de usá-la num arquivo de definição.

## Visibilidade condicional (`visivel_se`)

Expressão booleana simples, avaliada contra os valores já preenchidos no
mesmo nível:

```
<campo_id> in [<valor>, <valor>, ...]
<campo_id> == <valor>
<campo_id> != <valor>
```

Exemplo (§6.3): `visivel_se: "natureza in [fala, misto]"` — o campo `idioma`
só aparece na tela quando `natureza` foi respondido como `fala` ou `misto`.
Um campo oculto por `visivel_se` não é obrigatório mesmo que `obrigatorio:
true`, e não bloqueia salvamento.

## Efeitos colaterais (`afeta`)

Lista de identificadores de efeito que a interface reconhece e aciona quando
o campo recebe valor. Nesta versão, o único efeito definido é:

- `habilitar_transcricao` — libera a ação "transcrever" para o item (Etapa 7). Usado por exemplo quando `natureza` é respondido como `fala` ou `misto` (§6.3).

Novos efeitos seguem a mesma regra das validações nomeadas: exigem código
correspondente na interface, não são livres.

## `arquivos_esperados`

Lista, no nível superior da definição, dos papéis de arquivo que o tipo
espera receber:

```yaml
arquivos_esperados:
  - papel: <identificador de papel, casa com arquivo.papel>
    obrigatorio: true|false        # opcional, default false
    por: <nome_do_nivel>            # opcional. Presente quando o arquivo é esperado por ocorrência de um nível repetido (ex.: master por faixa).
    minimo: <LARGURAxALTURA>        # opcional. Só para papéis de imagem — resolução mínima.
```

O painel de inconsistências (§7.4) usa esta lista para apontar o que falta:
faixa sem master, release sem capa, capa abaixo do mínimo.

## O que este formato deliberadamente não cobre

- Profundidade de aninhamento além de dois níveis.
- Validação cruzada entre campos além de `soma_100` (ex.: um campo cujo
  limite depende do valor de outro) — se aparecer essa necessidade, é uma
  validação nomeada nova, definida aqui antes de implementada.
- Cálculo/fórmula em campo — todo campo é entrada direta, técnica ou sugestão de IA, nunca derivado de outros campos na hora do render.

Qualquer necessidade fora desta lista é motivo para parar e perguntar antes
de estender o parser (regra 2 da §0).
