# Qualification du cœur : ATmega328P

<!-- Généré par `cmake --build build --target qualification` (ADR 0006). Ne pas modifier à la main. -->

**Niveau : qualifié, avec exceptions documentées.** 85 instructions sur 85 qualifiées, dont 11 avec une exception documentée ; 18 programmes compilés sur 18 réussis.

## Sources de vérité

- **Décodage** : les 65 536 opcodes sont classés, 63770 instructions et 1766 réservés (212 sont des instructions d'autres cœurs : EIJMP, ELPM, DES…). Un opcode réservé arrête l'exécution (`InvalidOpcode`). La classification est recoupée avec le désassembleur de binutils (`avr-objdump`).
- **Différentiel** : 2000 programmes aléatoires (graine 1, 60 éléments chacun), 149168 instructions comparées après chacune avec simavr 30eef25 : registres, SREG, SP, PC, SRAM et cycles. 0 divergence(s) inexpliquée(s).
- **Cycles** : 120 scénarios, un par cas de la table `timing` (branchement pris, saut d'un ou deux mots, accès SRAM ou registres), comparés à la description et à l'*AVR Instruction Set Manual (DS40002198B), colonne AVRe+* ; chacun des 63770 opcodes exécuté une fois depuis l'état de reset.
- **Programmes compilés** : 18 variantes (`firmware/qualification/`, `-O0`, `-O2`, `-Os`) rendant leur verdict sur les octets réservés 0xFC à 0xFF.
- **Couverture des tests** : 72 instructions sur 85 exécutées par la suite de tests unitaires et d'intégration ; la colonne *Opcodes* compte les encodages distincts exercés par les tests, puis par l'ensemble de la qualification.

## Matrice

✅ vérifié · ⚠️ exception documentée · ❌ en défaut · — non mesuré

| Instruction | Décodage | Sémantique | Flags | Cycles | Différentiel | Opcodes (tests / qualification) |
| --- | --- | --- | --- | --- | --- | --- |
| `adc` | ✅ 1024 opcodes, AVR (alias : rol) | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1513 exécutions | 11 / 811 sur 1024 |
| `add` | ✅ 1024 opcodes, AVR (alias : lsl) | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1437 exécutions | 7 / 794 sur 1024 |
| `adiw` | ✅ 256 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 1492 exécutions | 2 / 256 sur 256 |
| `and` | ✅ 1024 opcodes, AVR (alias : tst) | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1533 exécutions | 1 / 801 sur 1024 |
| `andi` | ✅ 4096 opcodes, AVR (alias : cbr) | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1503 exécutions | 8 / 1288 sur 4096 |
| `asr` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1559 exécutions | 2 / 32 sur 32 |
| `bclr` | ✅ 8 opcodes, AVR (alias : clc, clh, cli, cln, cls, clt, clv, clz) | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1533 exécutions | 1 / 8 sur 8 |
| `bld` | ✅ 256 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1454 exécutions | 3 / 254 sur 256 |
| `brbc` | ✅ 1024 opcodes, AVR (alias : brcc, brge, brhc, brid, brne, brpl, brtc, brvc) | ✅ différentiel | ✅ différentiel | ✅ 1 / 2 | ✅ 2987 exécutions | 11 / 175 sur 1024 |
| `brbs` | ✅ 1024 opcodes, AVR (alias : brcs, breq, brhs, brie, brlt, brmi, brts, brvs) | ✅ différentiel | ✅ différentiel | ✅ 1 / 2 | ✅ 2997 exécutions | 9 / 189 sur 1024 |
| `break` | ✅ 1 opcode, AVRe | ✅ tests unitaires | ✅ tests unitaires | ✅ 1 | ⚠️ exclue : arrête l'émulation pour le débogueur ; simavr l'exécute comme un NOP en l'absence de débogueur | 1 / 1 sur 1 |
| `bset` | ✅ 8 opcodes, AVR (alias : sec, seh, sei, sen, ses, set, sev, sez) | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1480 exécutions | 2 / 8 sur 8 |
| `bst` | ✅ 256 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1489 exécutions | 4 / 255 sur 256 |
| `call` | ✅ 64 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 4 | ✅ 787 exécutions | 1 / 1 sur 64 |
| `cbi` | ✅ 256 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 960 exécutions | 3 / 11 sur 256 |
| `com` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1453 exécutions | 2 / 32 sur 32 |
| `cp` | ✅ 1024 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1543 exécutions | 8 / 812 sur 1024 |
| `cpc` | ✅ 1024 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1447 exécutions | 11 / 804 sur 1024 |
| `cpi` | ✅ 4096 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1453 exécutions | 10 / 1310 sur 4096 |
| `cpse` | ✅ 1024 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 / 2 / 3 | ✅ 1444 exécutions | 5 / 770 sur 1024 |
| `dec` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1480 exécutions | 3 / 32 sur 32 |
| `eor` | ✅ 1024 opcodes, AVR (alias : clr) | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1469 exécutions | 7 / 787 sur 1024 |
| `fmul` | ✅ 64 opcodes, AVRe+ | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 1439 exécutions | 1 / 64 sur 64 |
| `fmuls` | ✅ 64 opcodes, AVRe+ | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 1372 exécutions | 1 / 64 sur 64 |
| `fmulsu` | ✅ 64 opcodes, AVRe+ | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 1515 exécutions | 0 / 64 sur 64 |
| `icall` | ✅ 1 opcode, AVR | ✅ différentiel | ✅ différentiel | ✅ 3 | ✅ 704 exécutions | 1 / 1 sur 1 |
| `ijmp` | ✅ 1 opcode, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 812 exécutions | 1 / 1 sur 1 |
| `in` | ✅ 2048 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1028 exécutions | 16 / 203 sur 2048 |
| `inc` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1430 exécutions | 5 / 32 sur 32 |
| `jmp` | ✅ 64 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 3 | ✅ 771 exécutions | 1 / 1 sur 64 |
| `ld -x` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ⚠️ 695 exécutions, 2 écarts de simavr expliqués | 2 / 30 sur 32 |
| `ld -y` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ⚠️ 695 exécutions, 4 écarts de simavr expliqués | 0 / 30 sur 32 |
| `ld -z` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ⚠️ 684 exécutions, 3 écarts de simavr expliqués | 1 / 30 sur 32 |
| `ld x` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 676 exécutions | 1 / 32 sur 32 |
| `ld x+` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 684 exécutions | 0 / 30 sur 32 |
| `ld y+` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 747 exécutions | 0 / 30 sur 32 |
| `ld z+` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 692 exécutions | 2 / 30 sur 32 |
| `ldd y` | ✅ 2048 opcodes, AVR (alias : ld y) | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 683 exécutions | 1 / 902 sur 2048 |
| `ldd z` | ✅ 2048 opcodes, AVR (alias : ld z) | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 722 exécutions | 0 / 695 sur 2048 |
| `ldi` | ✅ 4096 opcodes, AVR (alias : ser) | ✅ différentiel | ✅ différentiel | ✅ 1 / 1 | ✅ 38356 exécutions | 112 / 2575 sur 4096 |
| `lds` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 3258 exécutions | 10 / 32 sur 32 |
| `lpm` | ✅ 33 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 3 | ✅ 2008 exécutions | 2 / 33 sur 33 |
| `lpm z+` | ✅ 32 opcodes, AVRe | ✅ différentiel | ✅ différentiel | ✅ 3 | ✅ 1009 exécutions | 3 / 30 sur 32 |
| `lsr` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1474 exécutions | 2 / 32 sur 32 |
| `mov` | ✅ 1024 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1449 exécutions | 10 / 836 sur 1024 |
| `movw` | ✅ 256 opcodes, AVRe | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1512 exécutions | 14 / 255 sur 256 |
| `mul` | ✅ 1024 opcodes, AVRe+ | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 1469 exécutions | 1 / 802 sur 1024 |
| `muls` | ✅ 256 opcodes, AVRe+ | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 1477 exécutions | 1 / 255 sur 256 |
| `mulsu` | ✅ 64 opcodes, AVRe+ | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 1530 exécutions | 1 / 64 sur 64 |
| `neg` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1426 exécutions | 1 / 32 sur 32 |
| `nop` | ✅ 1 opcode, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1498 exécutions | 1 / 1 sur 1 |
| `or` | ✅ 1024 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1473 exécutions | 3 / 787 sur 1024 |
| `ori` | ✅ 4096 opcodes, AVR (alias : sbr) | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1475 exécutions | 5 / 1251 sur 4096 |
| `out` | ✅ 2048 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1051 exécutions | 25 / 153 sur 2048 |
| `pop` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 2587 exécutions | 6 / 32 sur 32 |
| `push` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 8081 exécutions | 6 / 32 sur 32 |
| `rcall` | ✅ 4096 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 3 | ✅ 766 exécutions | 1 / 20 sur 4096 |
| `ret` | ✅ 1 opcode, AVR | ✅ différentiel | ✅ différentiel | ✅ 4 | ✅ 778 exécutions | 1 / 1 sur 1 |
| `reti` | ✅ 1 opcode, AVR | ✅ différentiel | ✅ différentiel | ✅ 4 | ✅ 719 exécutions | 1 / 1 sur 1 |
| `rjmp` | ✅ 4096 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 755 exécutions | 15 / 153 sur 4096 |
| `ror` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1524 exécutions | 2 / 32 sur 32 |
| `sbc` | ✅ 1024 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1457 exécutions | 3 / 796 sur 1024 |
| `sbci` | ✅ 4096 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1499 exécutions | 7 / 1287 sur 4096 |
| `sbi` | ✅ 256 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 1016 exécutions | 8 / 16 sur 256 |
| `sbic` | ✅ 256 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 / 2 / 3 | ✅ 1539 exécutions | 0 / 8 sur 256 |
| `sbis` | ✅ 256 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 / 2 / 3 | ✅ 1428 exécutions | 0 / 8 sur 256 |
| `sbiw` | ✅ 256 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 | ✅ 1466 exécutions | 5 / 255 sur 256 |
| `sbrc` | ✅ 256 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 / 2 / 3 | ✅ 1418 exécutions | 1 / 255 sur 256 |
| `sbrs` | ✅ 256 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 / 2 / 3 | ✅ 1473 exécutions | 3 / 255 sur 256 |
| `sleep` | ✅ 1 opcode, AVR | ✅ tests unitaires | ✅ tests unitaires | ✅ 1 | ⚠️ exclue : arrête le cœur jusqu'à une interruption : le réveil relève des périphériques, couvert par les tests d'interruptions | 1 / 1 sur 1 |
| `spm` | ✅ 1 opcode, AVRe | ✅ tests unitaires | ✅ tests unitaires | ⚠️ 1 | ⚠️ exclue : écrit la flash par SPMCSR, une séquence de périphérique couverte par les tests unitaires | 1 / 1 sur 1 |
| `st -x` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ⚠️ 702 exécutions, 3 écarts de simavr expliqués | 0 / 30 sur 32 |
| `st -y` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ⚠️ 720 exécutions, 5 écarts de simavr expliqués | 0 / 30 sur 32 |
| `st -z` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ⚠️ 654 exécutions, 5 écarts de simavr expliqués | 0 / 30 sur 32 |
| `st x` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ⚠️ 676 exécutions, 4 écarts de simavr expliqués | 0 / 32 sur 32 |
| `st x+` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 669 exécutions | 5 / 30 sur 32 |
| `st y+` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 656 exécutions | 0 / 30 sur 32 |
| `st z+` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 654 exécutions | 1 / 30 sur 32 |
| `std y` | ✅ 2048 opcodes, AVR (alias : st y) | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 662 exécutions | 0 / 728 sur 2048 |
| `std z` | ✅ 2048 opcodes, AVR (alias : st z) | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 706 exécutions | 4 / 613 sur 2048 |
| `sts` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 2 / 2 | ✅ 3375 exécutions | 6 / 32 sur 32 |
| `sub` | ✅ 1024 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1457 exécutions | 4 / 778 sur 1024 |
| `subi` | ✅ 4096 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1481 exécutions | 15 / 1379 sur 4096 |
| `swap` | ✅ 32 opcodes, AVR | ✅ différentiel | ✅ différentiel | ✅ 1 | ✅ 1423 exécutions | 2 / 32 sur 32 |
| `wdr` | ✅ 1 opcode, AVR | ✅ tests unitaires | ✅ tests unitaires | ✅ 1 | ⚠️ exclue : n'agit que sur le chien de garde, qui n'est pas émulé | 1 / 1 sur 1 |

## Programmes compilés

| Programme | -O0 | -O2 | -Os |
| --- | --- | --- | --- |
| `arith` | ✅ 2034 vérifications, 498549 cycles | ✅ 2034 vérifications, 309751 cycles | ✅ 2034 vérifications, 309258 cycles |
| `float` | ✅ 22 vérifications, 29130 cycles | ✅ 22 vérifications, 24372 cycles | ✅ 22 vérifications, 24372 cycles |
| `interrupts` | ✅ 7 vérifications, 6985 cycles | ✅ 7 vérifications, 6679 cycles | ✅ 7 vérifications, 6751 cycles |
| `libc` | ✅ 15 vérifications, 24737 cycles | ✅ 15 vérifications, 22664 cycles | ✅ 15 vérifications, 22716 cycles |
| `setjmp` | ✅ 6 vérifications, 1717 cycles | ✅ 6 vérifications, 1035 cycles | ✅ 6 vérifications, 1080 cycles |
| `stack` | ✅ 11 vérifications, 380486 cycles | ✅ 11 vérifications, 158417 cycles | ✅ 11 vérifications, 221649 cycles |

## Exceptions

- **Écart de simavr** (ld -x, ld -y, ld -z, st -x, st -y, st -z, st x ; 26 occurrences dans cette campagne) : simavr met X, Y ou Z à jour après l'accès, et réécrit X même pour ST X, qui ne le modifie pas. Quand l'adresse est le registre pointeur lui-même (fichier de registres en 0x00-0x1F), LD -X lit l'ancien pointeur et un stockage dans le pointeur est perdu. Le manuel décrémente avant l'accès et laisse X intact pour ST X, comme SiliconForge.
- **Cycles de `spm`** : le manuel ne donne pas de nombre de cycles : le CPU est arrêté pendant l'effacement ou l'écriture de la page, quelques ms que l'émulateur ne modélise pas. La description compte l'instruction elle-même, 1 cycle.
- **`sbi 0x1f, 0`** : 4 cycles de plus. SBI EECR, EERE lance une lecture de l'EEPROM : la datasheet arrête le CPU pendant 4 cycles (`readCycles` du périphérique `avr.eeprom` de la description).
- **`break` hors différentiel** : arrête l'émulation pour le débogueur ; simavr l'exécute comme un NOP en l'absence de débogueur
- **`sleep` hors différentiel** : arrête le cœur jusqu'à une interruption : le réveil relève des périphériques, couvert par les tests d'interruptions
- **`spm` hors différentiel** : écrit la flash par SPMCSR, une séquence de périphérique couverte par les tests unitaires
- **`wdr` hors différentiel** : n'agit que sur le chien de garde, qui n'est pas émulé

Les encodages que le manuel déclare indéfinis (`LD r26, X+`, `ST X+, r26`…) sont exécutés de façon déterministe mais ne sont pas comparés.
