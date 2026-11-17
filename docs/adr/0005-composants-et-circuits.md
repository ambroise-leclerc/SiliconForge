# ADR 0005 — Composants et circuits décrits en JSON

- Statut : acceptée
- Date : 2026-10-28
- Épique : [#4](https://github.com/ambroise-leclerc/SiliconForge/issues/4)

## Contexte

La vue breadboard (PR #1) branche sur la puce une LED, des résistances, un bouton, un potentiomètre et un quartz.
Le circuit est décrit dans la description JSON du MCU (section `breadboard`, transmise par `sf.chip()`) et chaque type de composant est un constructeur
codé dans `ui/breadboard.js`. Le comportement vit dans la page : la luminosité de la LED vient du rapport cyclique de
la broche, et le bouton force directement D2.

Cette approche ne passera pas à l'échelle. On voudra ajouter des afficheurs 7 segments, des écrans
(SSD1306, ST7735), d'autres circuits. Surtout, les tests C++ (`Bench`) ne voient pas le circuit : ils ne peuvent pas
dire « la LED est allumée » ni « j'appuie sur le bouton ».

## Décision

1. **Un composant est décrit par des données et un rendu**, dans `components/<type>/` :
   - `component.json`, validé par `schemas/component.schema.json` : bornes, propriétés typées avec leurs unités
     (résistance, couleur, tension directe, valeur maximale), empreinte au pas du breadboard, type de modèle de
     comportement ;
   - `render.js` : le rendu SVG ou canvas, qui reçoit l'état du composant et produit les interactions de l'utilisateur.
2. **Un circuit est un fichier JSON**, `circuits/<nom>.json`, validé par `schemas/circuit.schema.json` : la puce et sa
   position, les composants posés sur des trous avec leurs propriétés, les fils. Le circuit actuel devient
   `circuits/arduino-demo.json` ; il est chargé au démarrage (`--circuit`) ou depuis l'interface.
3. **Le modèle électrique est en C++** (`core/circuit`), pas dans la page :
   - les nets sont construits à partir des trous et des fils, et leurs niveaux logiques sont résolus ;
   - le pont diviseur du potentiomètre fixe la tension de la broche ;
   - le courant de la LED, I = (V − Vf) / R, donne sa luminosité, moyennée sur la PWM ;
   - le bouton relie deux nets ;
   - un court-circuit entre deux sorties opposées est signalé.

   `WebBridge` envoie l'état des composants ; la page ne fait que le rendre et transmettre les interactions
   (`sf.component.*`). Le `Bench` voit le même circuit que l'utilisateur.
4. **Le format anticipe les composants visuels riches.** Un afficheur 7 segments est un composant à 8 LED. Un écran
   aura un modèle C++ du contrôleur (protocole SPI ou I²C sur les broches, mémoire d'image) et enverra son framebuffer
   en `Uint8Array` vers un canvas. Le schéma prévoit des bornes de bus et un état binaire volumineux.

## Mise en œuvre

- Format 1 des deux fichiers : [guide](../CIRCUITS.md). Le « type de modèle de comportement » est une liste
  d'éléments (`wire`, `resistor`, `led`, `switch`, `potentiometer`) reliant des bornes, pour qu'un afficheur
  7 segments soit décrit par huit éléments `led` sans code nouveau.
- Dans un circuit, un composant ne nomme pas la broche du MCU : elle se déduit des trous et des fils.
- Les bornes de bus (`role: "bus"`) et l'état binaire volumineux (`type: "bytes"`) sont acceptés par le schéma et
  n'ont pas encore de modèle.

### Réalisation (17 novembre 2026)

- **Modèle électrique (#19)** : `circuit::Circuit`. Les trous et les fils forment des nets ; rails, sorties du MCU et
  sondes sont des sources idéales ; résistances, potentiomètres, LED (seuil et 5 Ω en série) et résistances de rappel
  (35 kΩ) relient les nets. Les tensions sont résolues par analyse nodale à chaque changement, et chaque solution est
  mémorisée par état des sorties : une PWM coûte deux résolutions, pas une par front. Un net qu'aucune source
  n'atteint laisse l'entrée du MCU flottante.
- **Rendu par type (#20)** : `components/<type>/render.js`, embarqué et servi par une route de `WebBridge`.
  `ui/breadboard.js` ne connaît aucun type. L'état arrive dans chaque trame (`components`), les interactions repartent
  par `sf.component.set`.
- **Chargement (#21)** : `--circuit`, liste et fichier dans l'interface. Un circuit nomme son MCU ; une autre puce
  reçoit une plaque nue. La section `breadboard` des descriptions de MCU n'est plus lue.
- **Afficheur 7 segments (#22)** : huit éléments `led` et un `wire` entre ses deux communs, sans code C++ nouveau.
  L'anode commune est une propriété (`commonAnode`) qui retourne les LED par `swapWhen`.
- **Écrans à venir** : un contrôleur d'écran aura son modèle C++ et un état de type `bytes`. Sa mémoire d'image
  suivra le chemin ouvert par `sf.signals` : une fonction du pont qui renvoie un `Uint8Array` (tableau typé, sans
  limite de 64 Kio depuis WebFront 0.9), tirée par le module de rendu quand la trame d'état annonce une nouvelle
  révision, plutôt que poussée dans le JSON de chaque trame. Un rappel asynchrone (`ResultTask`) reste possible si la
  lecture devient longue.

## Conséquences

- Les tests de firmware pourront porter sur le circuit : `bench.component("led-d13").brightness()`,
  `bench.press("button-d2")`, et plus tard le texte d'un écran. C'est la condition des tests d'IHM sur écran simulé
  annoncés dans la vision du projet.
- `ui/breadboard.js` ne connaît plus aucun type de composant. Ajouter un composant revient à ajouter un répertoire
  dans `components/`, plus un modèle C++ quand le comportement est nouveau.
- Le modèle électrique reste volontairement simple (logique et résistif, sans simulation SPICE). Les composants dont le
  comportement dépend d'effets analogiques fins sont hors périmètre.
- La cadence d'envoi doit absorber des états plus gros (écrans).
  [WebFront#401](https://github.com/ambroise-leclerc/WebFront/issues/401) est livré depuis la 0.6 et disponible dans
  notre dépendance 0.7 (`broadcast`, contre-pression). Son adoption devra préserver le flux série par client ;
  voir le [bilan de migration](../WEBFRONT-0.7.md).

## Alternatives écartées

- **Garder le comportement en JavaScript** : les tests C++ seraient aveugles au circuit, et le comportement serait
  dupliqué le jour où l'on en aurait besoin côté C++.
- **Un simulateur électrique complet (SPICE)** : il serait beaucoup trop lent pour suivre l'émulation à plus de
  100 MHz, et inutile pour des LED, des boutons et des afficheurs.
- **Le format Fritzing (.fzp/.fzz)** : il est riche pour le dessin mais ne décrit aucun comportement, et il serait lourd
  à intégrer. Une importation des graphismes pourra être envisagée plus tard.
