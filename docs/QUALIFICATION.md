# Qualification du cœur CPU

La qualification répond à une question : **le firmware se comporte-t-il comme sur la vraie puce ?** Elle met en
œuvre l'[ADR 0006](adr/0006-qualification-coeur.md) avec quatre sources de vérité indépendantes de l'émulateur,
exécutées par CTest et consolidées dans une matrice publiée dans [`docs/qualification/`](qualification/README.md).

| Source | Outil | Ce qui est comparé | Référence |
| --- | --- | --- | --- |
| Décodage | `tests/unit/OpcodeClassTests.cpp` | Classe de chacun des 65 536 opcodes | Désassembleur de binutils (`avr-objdump`) |
| Différentiel | `siliconforge_differential` | Registres, SREG, SP, PC, SRAM et cycles après chaque instruction | simavr, épinglé par CPM |
| Cycles | `siliconforge_qualification cycles` | Chaque cas de la table `timing` | Description de la puce et *AVR Instruction Set Manual* |
| Programmes | `siliconforge_qualification program` | Verdict de programmes C compilés en `-O0`, `-O2`, `-Os` | Valeurs calculées sur l'hôte, libgcc, avr-libc |

## Lancer la qualification

```sh
cmake --build build --target qualification   # tests depuis un état propre, puis docs/qualification/
ctest --test-dir build -L qualification       # les seuls tests de qualification (une seconde environ)
```

La cible `qualification` efface la couverture précédente, lance `ctest -L fast`, puis réécrit la matrice dans
`docs/qualification/`. La CI exécute les mêmes tests à chaque PR et publie la matrice en artefact (`qualification`) ;
le workflow *Release assets* la joint à chaque release avec l'animation du README. La copie de `docs/qualification/`
est mise à jour en même temps que la release.

Le différentiel et les programmes compilés demandent la chaîne AVR (`gcc-avr`, `avr-libc`) : sans elle, CTest les
saute et la matrice indique les colonnes non mesurées.

## Décodage et couverture

`avr::classify(opcode)` (`include/siliconforge/arch/avr/Opcodes.hpp`) range chaque opcode parmi les **instructions**,
avec leur mnémonique de la table `timing`, la plus petite variante de cœur qui les possède (AVR, AVRe, AVRe+) et leurs
alias d'assembleur (`lsl`, `clr`, `breq`…), ou parmi les **opcodes réservés**. Parmi ces derniers, 212 sont des instructions
de cœurs non émulés (ELPM, EIJMP, DES, XCH…) et 1 554 n'existent sur aucun AVR. Les tests vérifient que :

- chaque instruction a son entrée dans la table `timing`, et chaque entrée au moins un opcode ;
- chaque opcode réservé arrête l'exécution sur `StopReason::InvalidOpcode`, PC sur l'instruction fautive ;
- `avr-objdump` donne la même lecture des 65 536 opcodes, alias et instructions d'autres cœurs compris.

La **couverture** compte les opcodes exécutés. `avr::OpcodeCoverage::start()` bascule la boucle d'émulation sur une
seconde instance qui enregistre chaque opcode : la boucle habituelle ne paie rien. CTest pose
`SILICONFORGE_OPCODE_COVERAGE` pour la suite de tests (`coverage/tests`) et pour les outils de qualification
(`coverage/qualification`), et chaque processus y écrit son relevé. La matrice distingue ainsi ce que les tests
écrits à la main exercent de ce que la qualification ajoute.

## Tests différentiels contre simavr

`siliconforge_differential` génère des programmes aléatoires reproductibles par leur graine : instructions de calcul,
accès indirects par X, Y, Z (dont des adresses du fichier de registres), LDS/STS, PUSH/POP appariés, branchements,
sauts conditionnels d'un ou deux mots, RJMP, CALL, IJMP, RET, RETI, IN/OUT sur SREG, SP et les GPIOR, LPM. Les sauts vont
toujours vers l'avant : chaque programme se termine. Registres, SREG et SRAM partent du même état aléatoire sur les deux
simulateurs.

Après chaque instruction, registres, SREG, SP, PC, SRAM, GPIOR et cycles sont comparés. Une divergence est **réduite**
automatiquement (delta debugging) au plus petit programme qui la montre encore, puis affichée désassemblée :

```text
Divergence on st -y, r11 (st -y/semantics), program 56:
  r28: 0x4e here, 0x1c on simavr
    0000: e1cd  ldi r28, 0x1d
    0002: e0d0  ldi r29, 0x00
    0004: 92ba  st -y, r11
```

Les écarts imputables à simavr sont listés dans `knownReferenceDivergences` (`tests/qualification/Differential.cpp`)
avec leur justification et la condition exacte qui les délimite. Ils sont comptés, simavr est réaligné sur
SiliconForge et la comparaison continue. Aujourd'hui, un seul écart est connu : simavr met à jour le pointeur après
l'accès, ce qui fausse `LD -X` et `ST X`/`ST -X` (et Y, Z) quand l'adresse est le registre pointeur lui-même.

```sh
build/tests/qualification/siliconforge_differential --seed 1 --programs 2000          # CTest, ~0,2 s
build/tests/qualification/siliconforge_differential --seed 7 --programs 1000000       # campagne longue, ~1 min
```

SLEEP, BREAK, WDR et SPM sont exclus des programmes aléatoires (raisons dans la matrice) et couverts par les tests
unitaires. simavr est sous GPL v3 : il n'est lié qu'à `siliconforge_differential`, exécutable de test jamais installé
ni distribué, et l'option `-DSILICONFORGE_DIFFERENTIAL_TESTS=OFF` le retire.

## Cycles

Pour chaque entrée de la table `timing` de la description et pour chacun de ses cas (`taken`, `skipOneWord`,
`skipTwoWords`, `sram`), un scénario prépare l'état puis mesure l'instruction : branchement pris ou non, saut d'une NOP
ou d'un LDS, accès en SRAM ou à un registre d’E/S d’usage général, pile déplacée pour PUSH/POP. Les adresses
viennent du profil de la cible. Un point de surveillance vérifie l’adresse effectivement utilisée. La mesure
doit égaler la description, qui doit égaler le manuel (table embarquée dans `tests/qualification/Cycles.cpp`). Une
entrée sans scénario est une erreur. `pc3Bytes` est signalé non applicable sur un PC de 16 bits.

Sur l’ATtiny424, le test `CycleConsistency-attiny424` compare les mesures au profil provisoire.
Le rapport indique `mode: consistency`. La matrice laisse les cycles non qualifiés en attendant
la vérification des timings contre la colonne AVRxt du manuel.

Chacun des 63 770 opcodes d'instruction est en outre exécuté une fois depuis l'état de reset, et ses cycles doivent
être l'une des valeurs de son entrée. Les exceptions sont documentées dans la matrice : SPM, dont le manuel ne donne pas
de durée, et `SBI EECR, EERE`, qui arrête le CPU 4 cycles pendant la lecture de l'EEPROM, comme la datasheet.

## Programmes compilés

`firmware/qualification/*.c` est compilé par CMake à trois niveaux d'optimisation (`-O0`, `-O2`, `-Os`) ; chaque
variante est un test CTest. Les binaires ne sont pas versionnés.

| Programme | Ce qu'il vérifie |
| --- | --- |
| `arith` | Addition, soustraction, multiplication, division, modulo, décalages et comparaisons sur 8, 16, 32 et 64 bits, signés ou non (2 034 vérifications, valeurs calculées sur l'hôte) |
| `float` | Flottants logiciels : opérations correctement arrondies au bit près, débordements, NaN, conversions, `sqrtf`, `sinf`, `expf`, `powf`, `strtod` |
| `setjmp` | `setjmp`/`longjmp` depuis 20 niveaux d'appel : valeur de retour, pile, variables `volatile`, SREG |
| `interrupts` | Interruptions imbriquées (comparaison et débordement du Timer0) : ordre, profondeur, registres préservés |
| `stack` | Récursion profonde (Ackermann, récursion mutuelle), grands cadres, arguments variadiques, pointeurs de fonction |
| `libc` | `snprintf`, `sscanf`, `strtol`, `qsort`, `memmove`, `strstr`, accès à la mémoire programme, `ltoa` |

### Convention de verdict

Un programme inclut [`qualification.h`](../firmware/qualification/qualification.h), appelle `check(condition)` pour
chaque vérification puis `finish()`, qui écrit le verdict dans quatre octets d'E/S étendues que l'ATmega328P laisse
réservés et exécute `BREAK` :

| Adresse | Contenu |
| --- | --- |
| `0xFF` | Verdict : `0x01` réussi, `0x02` échoué, `0x00` tant que le programme tourne |
| `0xFE` | Numéro de la première vérification en échec, `0` si aucune |
| `0xFC`–`0xFD` | Nombre de vérifications (octet bas, puis haut) |

`siliconforge_qualification program fichier.hex` lance le programme (200 millions de cycles au plus) et réussit si
l'émulation s'arrête sur `BREAK` avec le verdict `0x01`. `OPAQUE(x)` cache une valeur à l'optimiseur pour que le calcul
ait lieu sur le cœur émulé et non à la compilation.

### Ajouter un programme, ou un test d'avr-libc

Déposez `firmware/qualification/nom.c`, reconfigurez CMake : trois tests `Qualification-Program-nom-O0/O2/Os`
apparaissent. Un test de la suite d'avr-libc (`tests/simulate/` de ses sources) s'adapte en remplaçant son `exit()` de
fin par `finish()` et ses `abort()` par `check(0)` ; il peut alors rejoindre le répertoire tel quel.

## Lire la matrice

[`docs/qualification/atmega328p.md`](qualification/atmega328p.md) donne une ligne par entrée de la table `timing` et une
colonne par source, avec un état par case (vérifié, exception documentée, en défaut, non mesuré). La puce est
**qualifiée** quand chaque ligne est vérifiée ou couverte par une exception documentée, et que tous les programmes
réussissent. La même matrice existe au format JSON (`atmega328p.matrix.json`) pour les outils.

Pour produire une matrice ciblée, lancez `siliconforge_qualification matrix --mcu atmega2560 --results build/qualification`.
Le générateur vérifie le champ `mcu` des rapports et lit la couverture dans les sous-répertoires
`builtin/<cible>`. La couverture des profils chargés depuis JSON et des cœurs autonomes est enregistrée
dans le répertoire parent, hors des matrices des profils intégrés. Relancez les tests pour actualiser
les anciennes mesures dépourvues d’identifiant.

Le même cadre servira aux futures architectures : pour ARM et RISC-V, la référence différentielle sera un autre
simulateur (QEMU, Spike) et les suites de conformité s'y brancheront.
