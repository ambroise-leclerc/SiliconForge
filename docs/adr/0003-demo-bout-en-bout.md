# ADR 0003 — Démo de bout en bout auto-testée : le blink réel

- Statut : proposée
- Date : 2026-10-28
- Épique : [#2](https://github.com/ambroise-leclerc/SiliconForge/issues/2)

## Contexte

La promesse de SiliconForge tient en une phrase : le code compilé pour l'AVR qui allume et éteint une broche fait
réellement clignoter la LED à l'écran, et on peut le **tester automatiquement**. Chaque maillon existe :

- `firmware/src/blink.c`, compilé par avr-gcc ;
- le cœur AVR et le GPIO, dont la période est vérifiée au cycle près par `tests/integration/FirmwareTests.cpp` ;
- `WebBridge`, qui pousse l'état des broches environ 30 fois par seconde ;
- la vue breadboard (PR #1), dont la LED rouge suit D13 (`ui/breadboard.js`, attribut `data-level`).

Aucun test ne vérifie la chaîne complète. Les tests C++ s'arrêtent à la broche. Les specs d'IHM (`tests/ui/`) ne
regardent pas la LED, tournent à la main avec un runner provisoire et ignorent le temps réel. Une régression du
cadencement de `Simulation`, de la cadence d'envoi ou du rendu passerait inaperçue.

## Décision

1. **La démo de référence est une démo testée.** `blink.hex` sur le circuit de démonstration du breadboard est le
   scénario qui doit toujours fonctionner. Il est vérifié par des specs qui observent ce que voit l'utilisateur : la
   LED SVG, et non seulement l'état de la broche.
2. **Deux niveaux d'assertion :**
   - **En temps simulé**, déterministe : la LED alterne allumée/éteinte avec une demi-période de 0,5 s ± 1 %, mesurée sur
     `state.cycles` des trames où le rendu change. La spec s'exécute à vitesse maximale et prouve que la chaîne est
     juste.
   - **En temps réel**, à ×1 : la période mesurée à l'horloge murale vaut 1 s ± 10 %. Cela prouve que le clignotement
     est réel pour un humain : cadencement de `Simulation` et trames sans perte.
3. **Dans la vraie fenêtre.** En CI, les specs tournent dans le frontend natif (WebKitGTK) sous Xvfb, au moyen de
   CTest. Cela dépend de [WebFront#402](https://github.com/ambroise-leclerc/WebFront/issues/402) ; en attendant, un
   runner Playwright headless versionné dans `tests/ui/`, avec Jasmine embarqué, joue ce rôle.
4. **Depuis les sources.** Quand avr-gcc est présent, le build compile `firmware/` et la démo utilise ces binaires.
   `firmware/prebuilt/` reste le repli, et la CI vérifie qu'il est à jour. Un test change la durée du délai et constate
   la nouvelle période : « je modifie mon code, je vois la LED changer ».
5. **Une démo montrable.** La CI produit une courte animation de la fenêtre (LED qui clignote, LED en PWM) pour le
   README.

## Conséquences

- La démo devient un test de non-régression de toute la pile. Une PR qui casse le rendu, le pont ou le cadencement
  échoue en CI.
- Le test temps réel est sensible à la charge des machines. Sa tolérance est large et documentée, avec une seule
  relance autorisée.
- La CI installe WebKitGTK et Xvfb ; le job d'IHM est plus long que les tests C++ et porte un label CTest à part
  (`ui`).
- SiliconForge éprouve ainsi les tests d'IHM de WebFront pour une application externe : les manques remontent en issues
  WebFront.

## Alternatives écartées

- **Tester seulement la broche en C++** : c'est déjà fait et cela ne prouve pas ce que voit l'utilisateur.
- **Comparer des captures d'écran** : fragile (polices, thèmes, moteurs) et lent. On lit plutôt l'état du DOM du
  composant (`data-level`), qui est ce que le rendu affiche.
- **Seulement le navigateur headless** : il ne couvre pas la fenêtre native, qui est l'interface par défaut.
