# ADR 0005 — Composants et circuits décrits en JSON

- Statut : acceptée
- Date : 2026-10-28
- Épique : [#4](https://github.com/ambroise-leclerc/SiliconForge/issues/4)

## Contexte

La vue breadboard (PR #1) branche sur la puce une LED, des résistances, un bouton, un potentiomètre et un quartz.
Le circuit est décrit dans `ui/atmega328p.js` (section `breadboard`) et chaque type de composant est un constructeur
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

## Conséquences

- Les tests de firmware pourront porter sur le circuit : `bench.component("led-d13").brightness()`,
  `bench.press("button-d2")`, et plus tard le texte d'un écran. C'est la condition des tests d'IHM sur écran simulé
  annoncés dans la vision du projet.
- `ui/breadboard.js` ne connaît plus aucun type de composant. Ajouter un composant revient à ajouter un répertoire
  dans `components/`, plus un modèle C++ quand le comportement est nouveau.
- Le modèle électrique reste volontairement simple (logique et résistif, sans simulation SPICE). Les composants dont le
  comportement dépend d'effets analogiques fins sont hors périmètre.
- La cadence d'envoi doit absorber des états plus gros (écrans). Le besoin de diffusion efficace rejoint
  [WebFront#401](https://github.com/ambroise-leclerc/WebFront/issues/401).

## Alternatives écartées

- **Garder le comportement en JavaScript** : les tests C++ seraient aveugles au circuit, et le comportement serait
  dupliqué le jour où l'on en aurait besoin côté C++.
- **Un simulateur électrique complet (SPICE)** : il serait beaucoup trop lent pour suivre l'émulation à plus de
  100 MHz, et inutile pour des LED, des boutons et des afficheurs.
- **Le format Fritzing (.fzp/.fzz)** : il est riche pour le dessin mais ne décrit aucun comportement, et il serait lourd
  à intégrer. Une importation des graphismes pourra être envisagée plus tard.
