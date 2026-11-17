# ADR 0007 — Instruments : capture au cycle près

- Statut : acceptée
- Date : 2026-11-17

## Contexte

L'interface montrait l'état des broches trente fois par seconde, avec pour chacune la fraction de la trame passée à
l'état haut. C'est suffisant pour une LED, pas pour comprendre un firmware : un électronicien veut voir la forme d'un
signal PWM, mesurer une période, lire une trame série. Sur une carte réelle il branche un analyseur logique ou un
oscilloscope.

## Décision

1. **La capture est faite en C++, par événements.** Une broche prévient ses observateurs de signal
   (`Pin::subscribeSignal`) à chaque changement d'état ou de tension appliquée. `instruments::SignalRecorder` range ces
   changements, horodatés en cycles, dans un anneau unique de 2¹⁸ entrées numérotées. Un changement coûte une écriture :
   la capture est toujours active.
2. **La page tire la capture, elle ne la reçoit pas dans la trame d'état.** `sf.signals(epoch, curseur)` renvoie les
   changements suivants en tableaux typés. La page garde son curseur ; un numéro d'époque change à chaque
   réinitialisation, et le premier numéro encore détenu lui dit si l'anneau l'a dépassée.
3. **Les mesures et le dessin sont dans la page** (`ui/instruments.js`) : ils dépendent de la fenêtre affichée et ne
   changent rien à la simulation. Le banc de test C++ conserve ses mesures propres.
4. **L'USART dessine ses trames sur TXD.** Le modèle restait au niveau de la trame ; il planifie maintenant un
   événement par changement de niveau, soit au plus dix par octet.

## Conséquences

- Les mesures ne dépendent ni de la vitesse d'émulation ni de la cadence des trames ; elles sont testables de façon
  déterministe (`UI-Instruments`).
- La mémoire est bornée des deux côtés (anneau en C++, 200 000 changements par broche dans la page) ; une capture
  longue à haute fréquence perd son début, et l'interface le signale.
- Les tensions sont idéales pour les sorties. Un modèle analogique (temps de montée, filtrage RC) relèverait du modèle
  électrique de l'[ADR 0005](0005-composants-et-circuits.md), qui est résistif.
- D'autres décodeurs (SPI, I²C) attendent l'émulation de ces périphériques.

## Alternatives écartées

- **Échantillonner à fréquence fixe** : il faudrait choisir entre une mémoire énorme et des fronts déplacés ; on
  perdrait l'exactitude au cycle, qui est l'avantage d'un émulateur sur un instrument réel.
- **Pousser les changements dans la trame d'état** : une rafale (PWM à 62,5 kHz en vitesse maximale) aurait retardé
  les broches et le flux série de tous les clients, y compris ceux qui n'affichent pas les instruments.
