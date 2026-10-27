# ADR 0002 — Modèle temporel : cycles par instruction et périphériques événementiels

- Statut : acceptée
- Date : 2026-10-27

## Contexte

Le but est d'exécuter directement les binaires produits par la chaîne de compilation, y compris les boucles d'attente
calibrées au cycle (`_delay_ms`), les protocoles cadencés (USART) et la PWM, tout en restant nettement plus rapide que
le temps réel pour que les tests automatiques soient rapides.

## Décision

- **Précision au cycle, à la granularité de l'instruction.** Chaque instruction renvoie son nombre de cycles documenté
  (branchement pris ou non, saut d'une instruction d'un ou deux mots, accès mémoire multi-cycles). Les interruptions
  coûtent 4 cycles de réponse (+4 au réveil), et l'instruction qui suit `SEI` ou `RETI` s'exécute toujours avant une
  interruption en attente. On ne modélise pas les phases internes d'une instruction.
- **Périphériques événementiels.** Plutôt que d'avancer chaque périphérique à chaque cycle, chacun calcule l'instant
  de son prochain changement observable (comparaison, débordement, fin de trame, fin de conversion) et le place dans
  l'ordonnanceur (`Scheduler`). Le cœur ne consulte l'ordonnanceur qu'entre deux instructions. Lorsqu'un registre
  est lu (ex. `TCNT0`), le périphérique se met à jour paresseusement jusqu'au cycle courant.
- **Sommeil.** En mode `SLEEP`, le temps saute directement au prochain événement : un firmware qui dort entre deux
  interruptions s'émule des dizaines de fois plus vite que le temps réel.
- **Déterminisme.** Aucune source de temps réel n'intervient dans l'émulation : à firmware et stimuli identiques,
  l'exécution est identique, ce qui rend les tests reproductibles. Le cadencement temps réel n'est appliqué que par
  l'application (`Simulation`), en dehors du modèle.

## Conséquences

- Les mesures du banc de test (période, rapport cyclique) sont exactes au cycle : le firmware `blink` mesure 1 Hz à
  mieux que 0,1 %, la PWM 25 % donne exactement (OCR0A + 1) / 256.
- Un événement prévu au cycle exact de fin d'une exécution est traité avant de rendre la main, pour que l'état
  observé corresponde à l'instant atteint.
- Les effets intra-instruction (ex. lecture d'un registre au 2ᵉ cycle d'une instruction de 2 cycles) sont arrondis au
  début de l'instruction ; c'est suffisant pour le logiciel, pas pour valider du matériel.
