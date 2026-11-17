# Instruments : analyseur logique et oscilloscope

Le panneau **Instruments** montre les broches du microcontrôleur émulé comme le feraient un analyseur logique et un
oscilloscope branchés sur la carte. La décision est décrite dans l'[ADR 0007](adr/0007-instruments.md).

## Une capture au cycle près

Rien n'est échantillonné. Chaque changement d'une broche est enregistré avec le cycle d'horloge où il se produit
(`instruments::SignalRecorder`, 262 144 changements conservés) : un front est là où le firmware l'a mis, quelle que
soit la vitesse de l'émulation. À 16 MHz la résolution est de 62,5 ns.

Conséquence : les mesures sont celles du firmware, pas d'une approximation. Sur `demo.hex`, la LED D13 ne clignote pas
à 1 Hz exactement mais avec une période de 1,00058 s : `_delay_ms(10)` répété cinquante fois, plus le code exécuté
entre deux attentes. L'instrument le montre.

L'USART émet désormais ses trames bit par bit sur TXD, ce qui permet de les décoder.

## Utilisation

| Action | Effet |
| --- | --- |
| Onglets **Logic analyser** / **Oscilloscope** | Pistes numériques, ou tensions (utile pour une entrée analogique comme A0) |
| Molette, boutons − et + | Base de temps, de 200 ns à 6 s par division |
| Glisser | Remonter dans la capture ; le bouton **Live** / **Held** reprend le suivi |
| **Trigger** et son front, **Single** | Fige l'affichage sur le dernier front complet de la broche choisie ; **Single** s'arrête dessus |
| Clic, Maj + clic | Curseurs A et B : Δt et 1/Δt. Double-clic ou « clear » les efface |
| **UART** et débit | Décode les trames 8N1 de la broche ; « auto baud » déduit le débit de la plus courte impulsion vue trois fois |
| **Channels** | Choix des broches. En automatique : celles du circuit et celles qui bougent, dix au plus |
| **Export VCD** | Fichier Value Change Dump pour GTKWave, PulseView ou Surfer ; une unité de temps vaut un cycle |

À droite de chaque piste : fréquence et rapport cyclique mesurés sur la fenêtre affichée, puis la période (analyseur) ou
la tension moyenne et crête à crête (oscilloscope).

## Limites

- Les sorties sont idéales : 0 V ou la tension d'alimentation, sans temps de montée ni rebond. L'oscilloscope montre
  des tensions vraies pour les entrées alimentées par le circuit (pont diviseur d'un potentiomètre).
- Le décodeur ne traite que l'UART 8N1 au repos haut ; pas de SPI ni d'I²C, que le cœur n'émule pas encore.
- La page garde 200 000 changements par broche ; au-delà, la moitié la plus ancienne est abandonnée. Si la page prend
  du retard sur la mémoire de capture, « capture truncated » s'affiche.
- Le déclenchement porte sur un front d'une broche, sans niveau réglable ni condition combinée.

## Depuis une spec ou un outil d'automatisation

`window.siliconForge.instruments` :

| Appel | Résultat |
| --- | --- |
| `sync()` | Promesse résolue quand la page détient toute la capture |
| `samples(broche)` | `{t, v, s}` : instants en secondes, tensions, états (0 bas, 1 haut, 2 flottant) |
| `measure(broche, de, à)` | `{frequency, period, duty, vmin, vmax, vavg, edges, high}` ; `null` quand la grandeur n'est pas mesurable |
| `decodeUart(broche, débit, de, à)` | `{baud, bytes: [{t, end, value, ok}], text}` ; débit 0 pour le déduire |
| `vcd(index)` | Le texte VCD des broches données |
| `set({...})`, `view` | Modifier ou lire la vue : `mode`, `span`, `live`, `right`, `trigger`, `uart`, `cursors` |

En C++, le banc de test garde ses propres mesures (`bench.period`, `bench.dutyCycle`, `bench.transitions`).
