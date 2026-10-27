# ADR 0001 — Architecture multi-cible : `core` / `arch` / `mcu`

- Statut : acceptée
- Date : 2026-10-27

## Contexte

La première cible est l'ATmega328P, mais SiliconForge doit pouvoir émuler d'autres microcontrôleurs : d'autres AVR
(ATmega2560, ATtiny), puis d'autres architectures (ARM Cortex-M, RISC-V). L'application, l'interface web et les
tests ne doivent pas être réécrits à chaque nouvelle cible.

## Décision

Le code est découpé en trois couches de bibliothèques, chacune ne dépendant que des précédentes :

1. `core/` (`SiliconForge::core`) : tout ce qui est indépendant du jeu d'instructions — l'interface `Mcu`,
   l'ordonnanceur, les broches, les ports série, le débogueur, les chargeurs HEX/ELF.
2. `arch/<famille>/` (`SiliconForge::avr`, …) : le cœur CPU et les périphériques d'une famille, paramétrés par les
   adresses de leurs registres et leurs vecteurs pour servir toutes les puces de la famille.
3. `mcu/<puce>/` (`SiliconForge::mcu`) : la composition d'une puce (tailles mémoire, carte des E/S, périphériques
   présents, noms des broches) et le registre `mcu::create(nom)`.

L'application, le banc de test et l'interface ne connaissent que `Mcu`. Cette interface est virtuelle : elle n'est
traversée qu'à grain grossier (tranches d'exécution, lecture d'état), jamais à chaque instruction. À l'intérieur d'un
cœur, la performance prime : table de décodage, gestionnaires d'instructions non virtuels.

## Conséquences

- Ajouter une puce AVR = un nouveau dossier `mcu/<puce>` ; ajouter une architecture = un nouveau `arch/<famille>`.
- Les notions propres à une architecture (espaces mémoire de Harvard, SREG, vecteurs) restent dans `arch/` ; `core/`
  n'en expose que des vues génériques (`RegisterValue`, `MemorySpace`).
- Les broches portent à la fois leur nom de datasheet et, le cas échéant, leur nom sur la carte (Arduino Uno), ce qui
  permet aux tests et à l'interface de parler le langage du développeur.
