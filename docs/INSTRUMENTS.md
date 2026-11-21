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

## Les appareils du banc

Le panneau du bas est un établi : une barre choisit l’appareil affiché. Un clic passe de l’un à l’autre ; Maj + clic
ou l’interrupteur **Side by side** en montrent plusieurs à la fois (alimentation, charge et multimètre ensemble, ou
l’un d’eux avec l’analyseur). L’analyseur logique et l’oscilloscope sont deux faces de la même capture et s’excluent.
Le choix est conservé d’une session à l’autre.

| Appareil | Face avant |
| --- | --- |
| Analyseur logique, oscilloscope | Les pistes décrites plus haut |
| Supply (`bench-supply`) | Afficheurs tension et courant, voyants CV / CC, consigne, limite, bouton **Output** |
| Generator (`signal-generator`) | Afficheurs tension et courant, voyants ON / OVL, forme d’onde, fréquence, amplitude, décalage, rapport cyclique, période des échantillons, bouton **Output**, bornes publiées |
| Load (`variable-load`) | Afficheurs V, A et W, résistance et valeurs prédéfinies |
| Multimeter (`multimeter`) | Tension continue entre `V` et `COM`, sans courant prélevé |

Où chaque appareil est branché se lit sur la plaque, avec les mêmes symboles et couleurs que les fiches de sa face
avant, qui indiquent aussi le trou :

| Symbole | Couleur | Borne |
| --- | --- | --- |
| **+** | rouge | sortie positive de l’alimentation |
| **−** | noir | sortie négative |
| **V** | jaune | entrée tension du multimètre, sortie du générateur |
| masse (trois traits) | gris | `COM` du multimètre, `common` du générateur |
| **Ω** | bleu | bornes de la charge |

Un fil de la couleur de la borne relie chaque repère à l’étiquette de l’appareil (son nom et sa mesure en direct) ;
`view.offset` du composant la déplace. Les voies de l’analyseur ou de l’oscilloscope sont repérées sur les pattes de
la puce par un anneau de la couleur de leur trace, numéroté comme la piste.

Le circuit `bench-demo` réunit la démonstration Arduino, l’alimentation, la charge et le multimètre :
`siliconforge --circuit bench-demo firmware/prebuilt/demo.hex`.

Le circuit `generator-demo` remplace le potentiomètre de A0 par un générateur de signaux :
`siliconforge --circuit generator-demo firmware/prebuilt/demo.hex`. Le firmware de démonstration ne lit pas A0 : la
broche porte l’onde pour l’ADC d’un firmware qui la lit, et pour l’oscilloscope.

Pas encore : analyseur de signaux (il faut une capture de tensions et plus seulement de fronts) et générateur
d’impulsions déclenché par un événement du MCU.
