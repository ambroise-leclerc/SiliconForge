# ADR 0006 — Qualification de la simulation du cœur CPU

- Statut : proposée
- Date : 2026-10-28
- Épique : [#5](https://github.com/ambroise-leclerc/SiliconForge/issues/5)

## Contexte

SiliconForge exécute directement les binaires produits par la chaîne de compilation. Sa valeur dépend donc d'une
question simple : **le firmware se comporte-t-il comme sur la vraie puce ?** Les garanties actuelles sont des tests
unitaires écrits à la main, par famille d'instructions (`tests/unit/AvrInstructionTests.cpp`), et quatre firmwares
d'intégration. Ils ne disent pas :

- quelles instructions, quels opérandes et quels cas de flags ne sont jamais exercés ;
- si les résultats et les cycles concordent avec une référence indépendante ;
- quel niveau de confiance accorder à une puce ou à une variante de cœur.

Les erreurs les plus coûteuses pour un utilisateur sont silencieuses : un flag faux dans un cas limite, ou un cycle
de trop qui décale une temporisation.

## Décision

La qualification est un processus outillé, exécuté par la CI, qui combine des sources de vérité indépendantes et
publie son résultat.

1. **Couverture du décodage.** Chacun des 65 536 opcodes est classé (instruction valide avec sa variante de cœur,
   alias, réservé). Un rapport indique les instructions et les cas exercés par les tests. Un opcode réservé arrête
   l'exécution (`StopReason::InvalidOpcode`).
2. **Tests différentiels contre simavr**, simulateur AVR de référence, indépendant et largement utilisé. Des séquences d'instructions aléatoires, générées avec une graine
   reproductible, s'exécutent sur SiliconForge et sur simavr. Après chaque instruction, on compare registres, SREG, SP,
   mémoire et cycles. Toute divergence est réduite automatiquement à la plus petite séquence qui la reproduit. Les
   écarts imputables à la référence sont listés et justifiés.
3. **Programmes compilés.** Une suite de programmes C (arithmétique 8/16/32 bits, flottants logiciels, `setjmp`,
   interruptions imbriquées, pile profonde) est compilée en `-O0`, `-O2` et `-Os`. Chaque programme rend son verdict par
   une écriture sur un port d'E/S réservé au test, ou par `BREAK`. Des tests de la suite d'avr-libc pourront s'y
   ajouter.
4. **Timings.** Pour chaque instruction et chaque cas, les cycles mesurés sont comparés à la table de la description
   de la puce ([ADR 0004](0004-description-mcu-json.md)), elle-même vérifiée contre l'*AVR Instruction Set Manual*. Les
   exceptions sont documentées.
5. **Matrice de qualification.** Les résultats sont consolidés par architecture et par puce : pour chaque instruction,
   sémantique, flags, cycles et différentiel, avec un état par case. La matrice est publiée en artefact de CI et dans
   `docs/qualification/` à chaque release. Le README affiche le niveau de qualification de chaque puce.
6. **Hors CI, optionnellement** : comparaison de traces avec un vrai Arduino Uno (analyseur logique sur les broches),
   pour les timings des périphériques.

## Conséquences

- La confiance dans une puce devient mesurable et visible, et la matrice dit ce qui reste à faire.
- Le même cadre servira aux futures architectures. Pour ARM et RISC-V, la référence différentielle sera un autre
  simulateur (QEMU, Spike), et les suites de conformité existantes (par exemple riscv-arch-test) s'y brancheront.
- La CI s'alourdit : un job court par PR (fuzz borné, programmes compilés) et une campagne longue planifiée.
- simavr devient une dépendance de test, compilée par CPM et absente du binaire distribué.

## Alternatives écartées

- **Se fier aux seuls tests écrits à la main** : leur couverture est inconnue, et ils partagent les erreurs de
  compréhension de l'auteur de l'émulateur.
- **Comparer uniquement au matériel réel** : c'est la référence ultime, mais impossible en CI. Elle reste un complément
  manuel.
- **Une preuve formelle de la sémantique** : hors de proportion pour le projet, à reconsidérer pour des parties ciblées
  (flags arithmétiques) si le différentiel révèle des faiblesses.
