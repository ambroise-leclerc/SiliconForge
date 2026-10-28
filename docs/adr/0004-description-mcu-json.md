# ADR 0004 — Description normalisée des MCU en JSON

- Statut : proposée
- Date : 2026-10-28
- Épique : [#3](https://github.com/ambroise-leclerc/SiliconForge/issues/3)

## Contexte

Les caractéristiques de l'ATmega328P sont aujourd'hui dispersées dans le code :

- tailles mémoire, adresses des registres, vecteurs, prescalers et timings dans `src/mcu/Atmega328p.cpp` (la
  conversion ADC y dure 13 cycles d'horloge ADC, codés en dur ; la première conversion, à 25 cycles, n'est pas
  modélisée) ;
- nombre de cycles de chaque instruction dans les 65 handlers de `src/arch/avr/Instructions.cpp` ;
- brochage et fonctions des broches, une seconde fois, dans `ui/atmega328p.js` ;
- la liste des puces dans `src/mcu/Registry.cpp`, figée à la compilation.

Ajouter un ATmega168 (même cœur, autres tailles) ou un cœur AVRxt (cycles différents) oblige à écrire du C++ et à
recompiler. Il est aussi impossible de relire ou de comparer ces caractéristiques à la documentation du fabricant.

## Décision

1. **Un fichier JSON par puce**, `mcus/<puce>.json`, validé par un **JSON Schema versionné**
   (`schemas/mcu.schema.json`, champ `formatVersion`). Il décrit :
   - l'identité : nom, architecture, variante du cœur (`avr:AVRe+`), horloge par défaut ;
   - les mémoires : flash, SRAM, EEPROM, registres, avec leurs tailles et adresses ;
   - la **table de cycles** de chaque instruction, par mnémonique et par cas : branchement pris ou non, saut d'une
     instruction d'un ou deux mots, accès SRAM, `CALL`/`RET` selon la taille du PC ;
   - les vecteurs d'interruption ;
   - la carte des registres d'E/S : adresse, valeur de reset, bits nommés ;
   - les **instances de périphériques**, typées (`avr.gpio`, `avr.timer8`, `avr.usart`, `avr.adc`, `avr.eeprom`) avec
     leurs paramètres : registres, vecteurs, prescalers et **timings**, dont la durée de conversion ADC (13 cycles
     ADC, 25 pour la première) et la durée d'écriture EEPROM ;
   - le **brochage** du boîtier : numéro physique, port, fonctions, étiquettes de carte (Arduino).
2. **La sémantique reste en C++, les paramètres passent en données.** Les instructions et les périphériques sont du
   code (`arch/<famille>`) : c'est là que se jouent la vitesse et la justesse. Le JSON choisit, paramètre et cadence ce
   code, sans le remplacer. Une puce dont un périphérique n'existe pas encore en C++ est refusée au chargement avec une
   erreur explicite.
3. **Chargement dynamique.** `mcu::create(name)` construit les puces intégrées à partir de leur JSON, embarqué dans le
   binaire. `siliconforge --mcu-file x.json`, un répertoire de puces (`--mcu-path`) et l'interface permettent d'en
   charger d'autres sans recompiler. Les erreurs indiquent le chemin JSON fautif (`/peripherals/2/vector`).
4. **Une seule source pour l'interface.** Le brochage est envoyé à la page à partir du JSON ; `ui/atmega328p.js` ne le
   duplique plus.
5. **Import depuis les formats constructeurs.** Un convertisseur produit le squelette JSON à partir des fichiers ATDF
   de Microchip (registres, bits, vecteurs, brochages). CMSIS-SVD jouera ce rôle pour ARM. Les timings, absents de ces
   formats, sont ajoutés à la main et vérifiés par la qualification ([ADR 0006](0006-qualification-coeur.md)).
6. **Bibliothèque JSON** obtenue par CPM ; nlohmann/json est le choix par défaut, à confirmer dans la story #12.
7. **Non-régression** : `atmega328p.json` doit reproduire exactement le comportement actuel, et tous les tests existants
   passent sans modification.

## Conséquences

- Ajouter une puce d'une famille connue, c'est écrire (ou convertir) un fichier JSON. Cela complète
  l'[ADR 0001](0001-architecture-multi-cible.md) : `mcu/<puce>` devient en général un fichier de données, et du code
  seulement pour un câblage inhabituel.
- La table de décodage est construite au chargement avec les cycles du JSON. Le coût est payé une fois, sans
  conséquence sur la vitesse d'émulation (objectif ± 3 %).
- Le format devient une interface publique : ses évolutions passent par `formatVersion` et le schéma.
- Les variantes de cœur (AVRe, AVRe+, AVRxm, AVRxt) diffèrent par leur table de cycles et leur jeu d'instructions, et
  non par du code dupliqué.

## Alternatives écartées

- **Tout en C++ (état actuel)** : chaque puce impose une recompilation, et rien ne permet de relire ses
  caractéristiques à côté de la documentation.
- **Décrire aussi la sémantique des instructions en données** (langage de description d'ISA) : c'est trop lent et trop
  complexe pour le gain attendu. On n'y gagnerait de nouvelles architectures qu'au prix de la vitesse.
- **Utiliser l'ATDF ou le SVD directement** : ces formats XML, propres à chaque fabricant, ne contiennent ni les
  timings des instructions ni ceux des périphériques. Ils restent des sources d'import.
- **YAML ou TOML** : plus agréables à écrire, mais JSON est le format natif de la page web et des outils de schéma.
