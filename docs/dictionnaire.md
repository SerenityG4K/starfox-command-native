# Le dictionnaire des objets du jeu

Ce document est **produit** par `python tools/structures.py --document`, à partir de
l'inventaire des accès du code réécrit et des relevés d'exécution (`SFC_ETUDE_OBJETS`).
Il ne remplace pas `reference.md`, qui porte ce qu'on a compris ; il porte ce qu'on a mesuré.

Un **type** réunit les objets que les mêmes fonctions manipulent. Sa **taille** se lit à l'écart
le plus petit entre deux de ses objets. Sa **forme** dit ce que contiennent ses quarante-huit
premiers mots, vus de loin en loin pendant le jeu :

| lettre | ce que le champ porte |
| --- | --- |
| `p` | un pointeur vers la mémoire vive |
| `x` | une grandeur du monde (20.12, avec des décimales : position, vitesse, angle) |
| `n` | un petit entier |
| `t` | du texte |
| `.` | toujours nul |
| `?` | de tout : rien ne domine |

Trois pièges à garder en tête en le lisant : un objet a plusieurs vies (la mémoire est
recyclée), un tampon de travail n'est pas une structure, et ce document ne voit que les objets
passés en premier argument d'une fonction.

État : **422 types**, sur les scènes relevées (un vol et une partie complète).

## Les types les plus employés

### Type 0 — 4558 objets de 64 octets

Premier vu à `0x0228BBC0`, 250 fonctions, 9747168 appels relevés.

```
pp?p pn.? p??? ?.?. pt?? ???? ??.. ..?. p??? ???? ??.. ?.?.
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Ce que la documentation dit de ses fonctions :

- `0x021A9908` — singleton : construit son instance au premier appel, la rend ensuite (overlay des acteurs)

Fonctions : `0x01FF81BC`, `0x02043A4C`, `0x02061704`, `0x0206C790`, `0x0206DCB8`, `0x0206E878`, `0x0207065C`, `0x02070CA0`, `0x02070E88`, `0x02071254`, `0x02071DB8`, `0x02073214` …

### Type 1 — 3266 objets de 128 octets

Premier vu à `0x021D9544`, 379 fonctions, 20623367 appels relevés.

```
pp.p ??.n ..n? x?nn ..n. .?pp .... .... pp.p ??.n ..n? x?nn
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Ce que la documentation dit de ses fonctions :

- `0x021A9908` — singleton : construit son instance au premier appel, la rend ensuite (overlay des acteurs)

Fonctions : `0x01FF81BC`, `0x01FFB994`, `0x01FFBC30`, `0x01FFD740`, `0x0201CD54`, `0x0203CE4C`, `0x0203CE50`, `0x0203CE94`, `0x0203CF40`, `0x020403FC`, `0x020436B0`, `0x020439F4` …

### Type 2 — 2859 objets de 32 octets

Premier vu à `0x02280BC0`, 107 fonctions, 17240016 appels relevés.

```
ppnp ?xn. ppnp ?xn. ppnp ?xn. ppnp ?xn. ppnp ?xn. pp?p ?xn.
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FF81BC`, `0x020439C0`, `0x02060658`, `0x02068FC8`, `0x02068FEC`, `0x0206906C`, `0x020691E0`, `0x0206EE0C`, `0x0206F1FC`, `0x02071D70`, `0x02071D80`, `0x02071E6C` …

### Type 3 — 2415 objets de 64 octets

Premier vu à `0x0228BC20`, 5 fonctions, 13360 appels relevés.

```
pp?. x.?. pt?? ???? ??.. ..?. p??? ???? ???. ?.?. pt?? ????
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FF81BC`, `0x020650EC`, `0x02065104`, `0x02065118`, `0x02239134`

### Type 4 — 2181 objets de 192 octets

Premier vu à `0x022DD980`, 368 fonctions, 40686856 appels relevés.

```
ppn. .p.? ???. .n?. .?n. .n?? ?..? nn?n .... ?.?? ..?. .?..
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Ce que la documentation dit de ses fonctions :

- `0x021A9908` — singleton : construit son instance au premier appel, la rend ensuite (overlay des acteurs)
- `0x021B3E9C` — appelle le test dans l'ordre que dicte un drapeau du propriétaire
- `0x021B3FD0` — premier élément d'une liste à nœuds inclus
- `0x021B594C` — deux cent quarante-sept mille, `0x0216DADC` deux cent onze mille

Fonctions : `0x01FF81BC`, `0x0201C70C`, `0x0203EFFC`, `0x020403FC`, `0x02043AC4`, `0x02048F24`, `0x020493DC`, `0x0204955C`, `0x0204AD3C`, `0x02061674`, `0x020617A0`, `0x02069BD0` …

### Type 5 — 1486 objets de 16 octets

Premier vu à `0x0227D73C`, 15 fonctions, 960374 appels relevés.

```
p.nx .??n nnx? ?p?? ???? ???? ??.? .pp? ???? ???? ??.? .p??
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FFCD5C`, `0x02086160`, `0x02086C14`, `0x02090CF8`, `0x02090EE4`, `0x02090F20`, `0x02090F64`, `0x0209476C`, `0x02094790`, `0x020947B4`, `0x02094D98`, `0x020951C8` …

### Type 6 — 793 objets de 384 octets

Premier vu à `0x02302500`, 340 fonctions, 15684550 appels relevés.

```
ppn. .p.. .... .n?. ..n. .n.. n..n nn?n pppp p... .... ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Ce que la documentation dit de ses fonctions :

- `0x021A9908` — singleton : construit son instance au premier appel, la rend ensuite (overlay des acteurs)

Fonctions : `0x01FF81BC`, `0x0201C70C`, `0x020403FC`, `0x02048F24`, `0x020493DC`, `0x0204955C`, `0x0204AD3C`, `0x02069BD0`, `0x0206D950`, `0x02075F24`, `0x02075F30`, `0x02076068` …

### Type 7 — 444 objets de 16 octets

Premier vu à `0x0227D600`, 95 fonctions, 132242 appels relevés.

```
ppnp pppp pppp pppp pppp pppp pppp pppp pppp pppp pppp pppp
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FF81BC`, `0x020671F0`, `0x0206722C`, `0x02067AB4`, `0x02068258`, `0x02068F64`, `0x02071218`, `0x02071264`, `0x0207FD94`, `0x0216C440`, `0x0216C444`, `0x0216C448` …

### Type 8 — 390 objets de 128 octets

Premier vu à `0x022CFFC0`, 2 fonctions, 780 appels relevés.

```
pppp pppp tn.. pppp .pp. p... .... .... pppp pppp tn.. pppp
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0217CFA4`, `0x0217EF30`

### Type 9 — 378 objets de 32 octets

Premier vu à `0x0233F220`, 11 fonctions, 7092 appels relevés.

```
???? ???? ???? ???? ???? ???? ???? ???? ???? ???? ???? ????
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FF81BC`, `0x02027394`, `0x02027464`, `0x020274CC`, `0x02027534`, `0x0202759C`, `0x02027604`, `0x02065004`, `0x02065018`, `0x0206502C`, `0x02068F2C`

### Type 10 — 366 objets de 32 octets

Premier vu à `0x02281530`, 15 fonctions, 21302 appels relevés.

```
pp.p ppp? p?.p ppp? ??.p ppp? ??.p ppp? ??.? ppp? ??.p ppp?
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x02087740`, `0x021BC8D8`, `0x021F78EC`, `0x022113C4`, `0x022113FC`, `0x02211434`, `0x02215040`, `0x02215078`, `0x02219B5C`, `0x0221FD04`, `0x0223F5DC`, `0x0226049C` …

### Type 11 — 359 objets de 192 octets

Premier vu à `0x022DDB00`, 56 fonctions, 2731229 appels relevés.

```
pp.p p..p .?.. ..pp ..?n .?n? ?... ..?n .?nn ..n. .... ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Ce que la documentation dit de ses fonctions :

- `0x021B3E9C` — appelle le test dans l'ordre que dicte un drapeau du propriétaire
- `0x021B3FD0` — premier élément d'une liste à nœuds inclus
- `0x021B594C` — deux cent quarante-sept mille, `0x0216DADC` deux cent onze mille

Fonctions : `0x02171364`, `0x02171774`, `0x02173E74`, `0x021741E4`, `0x021742B0`, `0x021742D0`, `0x0217F5F8`, `0x0217F608`, `0x0217F810`, `0x0217F9F4`, `0x0217FA00`, `0x0217FA0C` …

### Type 12 — 348 objets de 64 octets

Premier vu à `0x0228BCC0`, 41 fonctions, 484693 appels relevés.

```
pp?p pppn pnn. ..?. pp?? ???? ??.. ..?. pp?? ???? ??.. ..?.
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x02073214`, `0x02073264`, `0x02073784`, `0x020737D4`, `0x0216E608`, `0x0216E684`, `0x0217355C`, `0x0218646C`, `0x02186498`, `0x0218649C`, `0x021864A0`, `0x0218A4D4` …

### Type 13 — 324 objets de 32 octets

Premier vu à `0x02281FA0`, 17 fonctions, 1132371 appels relevés.

```
ppnx xxp. ppn? ??p. ppn? ???. ppn? ??p. ppn? ??p. ppn? ???.
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x02182148`, `0x02182220`, `0x0218224C`, `0x0218ECA8`, `0x0218ECE8`, `0x0218EEF0`, `0x02199380`, `0x02199BF4`, `0x02199E58`, `0x0219A16C`, `0x0219A308`, `0x0219A350` …

### Type 14 — 309 objets de 40 octets

Premier vu à `0x020D97DC`, 3 fonctions, 23194 appels relevés.

```
.... .... .... .... .... .... .... .... .... .... .... ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FFEDE4`, `0x01FFEE38`, `0x02087474`

### Type 15 — 240 objets de 12 octets

Premier vu à `0x022F93EC`, 2 fonctions, 680 appels relevés.

```
.... .... .... .... .... .... .... .... .... .... .... ...?
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x02061588`, `0x021A9438`

### Type 16 — 235 objets de 64 octets

Premier vu à `0x0228C988`, 26 fonctions, 3268 appels relevés.

```
n??? pttx .?nx ??pt ???? ???? ???. ?.pt ???? ???? ?.?. ?.pt
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FF81BC`, `0x0216D994`, `0x02188DE8`, `0x02196758`, `0x02197648`, `0x021E1204`, `0x021E7568`, `0x021E762C`, `0x021EFD08`, `0x021F173C`, `0x022284F0`, `0x0222FCCC` …

### Type 17 — 224 objets de 12 octets

Premier vu à `0x022F9428`, 17 fonctions, 10203907 appels relevés.

```
.... .... .... .... .... .... .... .... .?.? ???? ???? ????
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FF81BC`, `0x0207ED94`, `0x0207EDB0`, `0x0207EDFC`, `0x02086160`, `0x02086C14`, `0x02180A58`, `0x02180B34`, `0x02180CD8`, `0x0218A340`, `0x021A9420`, `0x021E1000` …

### Type 18 — 214 objets de 40 octets

Premier vu à `0x022733C0`, 135 fonctions, 4708355 appels relevés.

```
pppp pppp .pn. ...x xx.. .nnn ???? ?n.. .... .?.n p... ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Ce que la documentation dit de ses fonctions :

- `0x0208940C` — rend une référence à un objet, et le confie à l'allocateur quand plus personne ne la tient
- `0x0216DADC` — l'octet `+0x4C` de l'objet désigné par le premier mot du champ `+0x24
- `0x021A9908` — singleton : construit son instance au premier appel, la rend ensuite (overlay des acteurs)

Fonctions : `0x02066B78`, `0x0206C790`, `0x0208940C`, `0x0216C454`, `0x0216C468`, `0x0216C498`, `0x0216C4A4`, `0x0216C5B8`, `0x0216C5C8`, `0x0216C678`, `0x0216C6A4`, `0x0216C70C` …

### Type 19 — 203 objets de 20 octets

Premier vu à `0x02304260`, 12 fonctions, 12231 appels relevés.

```
p... .p.. ..p. ...p .... p... .p.. ..p. ...? .... .... ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FF81BC`, `0x0204C1D8`, `0x0204DE54`, `0x02086340`, `0x0208636C`, `0x02087740`, `0x021FC094`, `0x021FC0C0`, `0x022072F4`, `0x02207320`, `0x022442C4`, `0x022442F0`

### Type 20 — 194 objets de 32 octets

Premier vu à `0x02281610`, 9 fonctions, 12406 appels relevés.

```
pp.p ppp? ??.p pppp ??.p ppp? ??.p ppp? ??.p ppp? ???p ppp?
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x021BC8D8`, `0x021F78EC`, `0x02211434`, `0x02215040`, `0x02215078`, `0x02219B5C`, `0x0221FD04`, `0x0223F5DC`, `0x0226049C`

### Type 21 — 189 objets de 64 octets

Premier vu à `0x02290C00`, 18 fonctions, 7998 appels relevés.

```
ppnn n..p pppp .... ?ttt n... .... ..?. pppp pn.? p... n.?.
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x02073214`, `0x02073264`, `0x02073784`, `0x020737D4`, `0x021718B8`, `0x0217228C`, `0x021725D0`, `0x02172664`, `0x021727CC`, `0x0218A4D4`, `0x0218A7A4`, `0x0218A7D8` …

### Type 22 — 180 objets de 128 octets

Premier vu à `0x022D3340`, 5 fonctions, 612 appels relevés.

```
pppp pppp tt?. pppp .pp. p... ..p. .... pppp pppp tt?. pppp
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0217CC98`, `0x0217EF30`, `0x0218FF98`, `0x0218FFA0`, `0x0219CD6C`

### Type 23 — 155 objets de 128 octets

Premier vu à `0x022D4510`, 2 fonctions, 697 appels relevés.

```
p... n.n. .... pppp pppp t?.. pppp .pp. pp.p n.n. .... pppp
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0203963C`, `0x0218D184`

### Type 24 — 149 objets de 128 octets

Premier vu à `0x022BBA40`, 18 fonctions, 104128 appels relevés.

```
pp.. ...? .... ?.?? .... .... .... .... ppn? ??.p ?... ?.??
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FFB994`, `0x01FFBC30`, `0x01FFD740`, `0x0201CD54`, `0x020601AC`, `0x02060334`, `0x02060594`, `0x020606CC`, `0x0206193C`, `0x0208148C`, `0x020814B0`, `0x0208155C` …

### Type 25 — 146 objets de 64 octets

Premier vu à `0x0228C982`, 2 fonctions, 6062 appels relevés.

```
??t? tt?? ??.. ..?. ?ttt ???? ??.. ..?. ?ttt t??? ??.. ..?.
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FF81BC`, `0x02006C54`

### Type 26 — 134 objets de 28 octets

Premier vu à `0x022BA4D0`, 9 fonctions, 155005 appels relevés.

```
pppp pppp pppp .??? ?.?? .... ??.. pp?p ???? ??.? ???? ..??
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x02185C80`, `0x02185FAC`, `0x0218600C`, `0x021860EC`, `0x021861E8`, `0x02186230`, `0x02186350`, `0x021863B0`, `0x0218A340`

### Type 27 — 125 objets de 192 octets

Premier vu à `0x022E0C8C`, 7 fonctions, 241049 appels relevés.

```
.... ..t? ..?? .??? ..?? ??.. ?n?? n... ...? n... ..?. .pp?
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FF81BC`, `0x02021E70`, `0x0203F868`, `0x0203F8B0`, `0x0217BA34`, `0x021BEA90`, `0x021BEAAC`

### Type 28 — 112 objets de 448 octets

Premier vu à `0x02307C00`, 61 fonctions, 917020 appels relevés.

```
pppp pppp .pn. ...x xx.. .nnn ??.. .n.. .... .?.n p... ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Ce que la documentation dit de ses fonctions :

- `0x0216DADC` — l'octet `+0x4C` de l'objet désigné par le premier mot du champ `+0x24

Fonctions : `0x02066B78`, `0x0216C454`, `0x0216C468`, `0x0216C498`, `0x0216C4A4`, `0x0216C5B8`, `0x0216C5C8`, `0x0216C678`, `0x0216C6A4`, `0x0216C70C`, `0x0216C730`, `0x0216C760` …

### Type 29 — 108 objets de 64 octets

Premier vu à `0x0228C20C`, 2 fonctions, 1157 appels relevés.

```
p.nx ..nn nnx. npt? ???? ???? ??.? .p?? ???? ???? ???? .pt?
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x02090EE4`, `0x02094790`

### Type 30 — 107 objets de 20 octets

Premier vu à `0x02302460`, 2 fonctions, 11213 appels relevés.

```
p... .p.. ..?. ...? .... .... .... .... .... .... ?..? ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0204C1D8`, `0x0204DE54`

### Type 31 — 107 objets de 128 octets

Premier vu à `0x022C66C0`, 13 fonctions, 1976594 appels relevés.

```
pppp pppp t??. pppp .pp. p... ?.n. .... pppp pppp t??. pppp
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FFD740`, `0x02060594`, `0x0206193C`, `0x0208148C`, `0x020814B0`, `0x0208155C`, `0x02081868`, `0x020840FC`, `0x0217DC50`, `0x0217DCD0`, `0x0217DCF8`, `0x0217DE30` …

### Type 32 — 89 objets de 20 octets

Premier vu à `0x02302724`, 3 fonctions, 1089 appels relevés.

```
p... .p.. ..p. ...p .... p... .p.. ..p. ...n .... .... ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0204C1D8`, `0x021FC094`, `0x021FC0C0`

### Type 33 — 88 objets de 256 octets

Premier vu à `0x022F8FC0`, 72 fonctions, 145661 appels relevés.

```
ppn. .??n nn.? ?..? .?.. ?n.. .... .nnn .... .... ..n? ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Ce que la documentation dit de ses fonctions :

- `0x021A9908` — singleton : construit son instance au premier appel, la rend ensuite (overlay des acteurs)

Fonctions : `0x01FF81BC`, `0x020403FC`, `0x02043B3C`, `0x0204896C`, `0x0207EB20`, `0x020A1C14`, `0x02178874`, `0x0217C198`, `0x0217C1B4`, `0x0217C200`, `0x0217C7EC`, `0x0217C9C4` …

### Type 34 — 66 objets de 28 octets

Premier vu à `0x020E2E2C`, 2 fonctions, 82769 appels relevés.

```
pp?n nn.p .?nn n.p. ?nnn ?p.? nnn? p.?n nnn? .?n? nn?. ?n?n
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x02092CDC`, `0x02092F80`

### Type 35 — 60 objets de 96 octets

Premier vu à `0x020BD6B4`, 2 fonctions, 65225 appels relevés.

```
x.p. .nnn .nnn .nn. .nnn .nnn .nnn nnn. nnnn ??nn ???. n??.
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FFBCE4`, `0x0218AE1C`

### Type 36 — 55 objets de 64 octets

Premier vu à `0x02297740`, 7 fonctions, 1112411 appels relevés.

```
ppxn nn.p .n.. .... ?ttt n... .... ..?. pp?n nn.p .n.. ..?.
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0207C188`, `0x0207C190`, `0x0207C1F8`, `0x0207C39C`, `0x0207C6C0`, `0x0207C754`, `0x0207C760`

### Type 37 — 53 objets de 128 octets

Premier vu à `0x022648A0`, 2 fonctions, 439 appels relevés.

```
p... ...p pppp pppt tt.p ppp. pp.p .... pp?? ...p pppp pppt
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0201EEEC`, `0x0201EF6C`

### Type 38 — 50 objets de 128 octets

Premier vu à `0x022D82C0`, 2 fonctions, 708 appels relevés.

```
pppp pppp n... pppp .pp. p... .... .... pppp pppp n... pppp
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0217DE30`, `0x0217EF30`

### Type 39 — 48 objets de 512 octets

Premier vu à `0x0232D800`, 32 fonctions, 165955 appels relevés.

```
ppnn ..n. .... ..nn nn.p .... .... .... .... p... .... ...p
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x020770C0`, `0x02083B4C`, `0x02083CB0`, `0x020856D4`, `0x02184034`, `0x02184248`, `0x02184270`, `0x021842F0`, `0x021842F8`, `0x021A088C`, `0x021A09B8`, `0x021A09E0` …

### Type 40 — 46 objets de 128 octets

Premier vu à `0x022C38C0`, 4 fonctions, 37678 appels relevés.

```
ppn? ?..p pp.n pnpn pnpn .xpx .??. .... ppn? ?..p pp.n pnpn
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FFB994`, `0x01FFBC30`, `0x020601AC`, `0x02060334`

### Type 41 — 46 objets de 64 octets

Premier vu à `0x0228CFC0`, 5 fonctions, 57745 appels relevés.

```
ppnp .nx. .xnn nx.n pp?p ..?? ??.. ..?. ppnp .??? ??n. ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x021A9834`, `0x021AB3CC`, `0x021AB3EC`, `0x021AB41C`, `0x021AB478`

### Type 42 — 45 objets de 512 octets

Premier vu à `0x0232DA4C`, 6 fonctions, 1024 appels relevés.

```
p... .... .... .... .p.. .... .... p... .... .... .... ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x021BFFB4`, `0x021BFFF4`, `0x021C0034`, `0x021C5850`, `0x021C61E0`, `0x021C61FC`

### Type 43 — 44 objets de 448 octets

Premier vu à `0x0231EE04`, 5 fonctions, 1518 appels relevés.

```
.... ...p p??n n.nn .... .... .... .nnp ..pn ...n ...n ..p.
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0216FF88`, `0x021700A8`, `0x021700F0`, `0x02170138`, `0x0217013C`

### Type 44 — 41 objets de 128 octets

Premier vu à `0x022BEEC0`, 18 fonctions, 23341 appels relevés.

```
pp.p ???? ..?. ???? ?.?. ???p .... .... pp?p ???? ???? ????
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FFD740`, `0x02060594`, `0x0206193C`, `0x0208148C`, `0x020814B0`, `0x0208155C`, `0x02081868`, `0x020840FC`, `0x0217B87C`, `0x0217B8A0`, `0x0217B8AC`, `0x0217B9B8` …

### Type 45 — 40 objets de 128 octets

Premier vu à `0x022D9590`, 2 fonctions, 344 appels relevés.

```
p... ..n. .... pppp pppp n... pppp .pp. pp?p ..n. .... pppp
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0203963C`, `0x02039F88`

### Type 46 — 40 objets de 56 octets

Premier vu à `0x02168500`, 2 fonctions, 2335 appels relevés.

```
.... .... .... .... .... .... .... .... .... .... .... ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x020916E4`, `0x02092268`

### Type 47 — 39 objets de 20 octets

Premier vu à `0x02303924`, 3 fonctions, 634 appels relevés.

```
p... .p.. ..p. ...p .... p... .p.. ..n. ...n .... .... ?...
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0204C1D8`, `0x022072F4`, `0x02207320`

### Type 48 — 39 objets de 68 octets

Premier vu à `0x021688FC`, 3 fonctions, 9072 appels relevés.

```
pp.. ??p. n?nn nnx? n... ..pp .nnn nnnx ?n.. ...p p.nn n?nn
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FF81BC`, `0x020982B4`, `0x0209853C`

### Type 49 — 37 objets de 192 octets

Premier vu à `0x022E074C`, 2 fonctions, 202 appels relevés.

```
.... ..t? ?.n? .?nn ..?? ?n.. nnn? n... .p.? n..? .... .pp.
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0217BA34`, `0x021BEAAC`

### Type 50 — 36 objets de 16 octets

Premier vu à `0x02273758`, 2 fonctions, 59633 appels relevés.

```
pnpn pnpn pnpn pnpn pnpn pnpn pnpn pnpn pnpn pnpn pnpn pnpn
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Ce que la documentation dit de ses fonctions :

- `0x0208940C` — rend une référence à un objet, et le confie à l'allocateur quand plus personne ne la tient

Fonctions : `0x0206C790`, `0x0208940C`

### Type 51 — 36 objets de 192 octets

Premier vu à `0x022E188C`, 2 fonctions, 148 appels relevés.

```
.... ..tt ?.n? .??n ..n? ?n.. nnn? n... .p.? n..? ..x. .pp?
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x021BEA90`, `0x021BEAAC`

### Type 52 — 34 objets de 384 octets

Premier vu à `0x02290340`, 3 fonctions, 19498 appels relevés.

```
pp?n ..n. .... ..?. pp?p ..?. ?.n? ..?. pp?p pppn pnn? ..?.
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x021864A0`, `0x02191658`, `0x02191660`

### Type 53 — 32 objets de 32 octets

Premier vu à `0x02281710`, 2 fonctions, 884 appels relevés.

```
pp.p ppp? ??.? ppp? p?.? pp?? ??.? pp?? ??.? pp?. ??.? pp??
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x02087740`, `0x0226049C`

### Type 54 — 32 objets de 128 octets

Premier vu à `0x022D4010`, 3 fonctions, 74 appels relevés.

```
p... .np. .... pppp pppp tt?. pppp .pp. p... .... .... pppp
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0203963C`, `0x02039FEC`, `0x02039FF4`

### Type 55 — 32 objets de 84 octets

Premier vu à `0x0233A040`, 15 fonctions, 142161 appels relevés.

```
.... .... .... .... .... .... .... .... .... .... .... ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FF81BC`, `0x021AC2D4`, `0x021AC418`, `0x021AC47C`, `0x021AC49C`, `0x021AC4EC`, `0x021AC7C8`, `0x021ACAF4`, `0x021ACB40`, `0x021AD9F8`, `0x021ADA44`, `0x021AE450` …

### Type 56 — 32 objets de 384 octets

Premier vu à `0x02306A00`, 3 fonctions, 91126 appels relevés.

```
ppn. .p.n n... .n?. ..n. .n?? n..n nn.n .... .x.. .pp. p.pp
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x02078148`, `0x020827D4`, `0x02082850`

### Type 57 — 32 objets de 128 octets

Premier vu à `0x022C1BC0`, 5 fonctions, 24366 appels relevés.

```
ppn. ppn? pnnp nnpn xn.. ...p .... .... ppn. ppn? pnnp nnpn
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x01FFD740`, `0x0206193C`, `0x0217AD58`, `0x0217AF94`, `0x0217B0CC`

### Type 58 — 30 objets de 752 octets

Premier vu à `0x02224798`, 2 fonctions, 182 appels relevés.

```
.... .... .... .... .... .... .... .... .... .... .... ....
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x020128A0`, `0x020395FC`

### Type 59 — 30 objets de 128 octets

Premier vu à `0x022DA588`, 2 fonctions, 344 appels relevés.

```
ppnp n... .... ..pp nppn pxnp pn.. xxx. p... .... .... ..pp
```

*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*

Fonctions : `0x0201EFB8`, `0x0201EFC8`

