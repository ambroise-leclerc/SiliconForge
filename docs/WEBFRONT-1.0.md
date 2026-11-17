# Passage à WebFront 1.0

SiliconForge épingle le tag [`v1.0.0`](https://github.com/ambroise-leclerc/WebFront/releases/tag/v1.0.0) de WebFront
(commit `e36da2f7f52f1ef569c3ace28671050ed71df16f`, publié le 17 novembre 2026). L'adoption a été faite sur la candidate
`6ccb7e17`, commit de `develop` dont la 1.0 ne diffère que par la documentation, l'exemple `host-loop`, la version
annoncée et le gel de compatibilité (bibliothèque 1.0.0, environnement développeur 1.0.0, API 1).
`CPM_WebFront_SOURCE` permet toujours une surcharge locale. Ce document succède au [bilan 0.9](WEBFRONT-0.9.md), qui
devient historique.

## Ce qui change pour nous depuis la 0.9

| Changement de WebFront | Effet sur SiliconForge |
| --- | --- |
| WebKitGTK démarre en mode développement, bac à sable désactivé, sauf `WEBFRONT_DISTRIBUTION=ON` (WebFront#596) | La CI n'a plus à poser `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS` : la variable est retirée du workflow. Un paquet distribué devra choisir `webfront_use_distribution(siliconforge)` ; nous ne distribuons pas encore de paquet. |
| Noms de commandes réservés au terminal (`ls`, `cat`…) (WebFront#575) | Sans effet : l'application n'enregistre aucune `webfront::Commands`. |
| `app.start()`, `app.requestClose()`, `webfront::UIThread` (WebFront#567) | Non adoptés : la simulation a déjà son fil et `show()` garde le fil principal, ce que GTK et AppKit imposent. |
| Catch2 remplacé par `Bdd.hpp` dans les tests de WebFront | Sans effet : nos tests gardent leur propre Catch2. |

La compilation avec GCC 16.1.0 et `-Werror` passe sans modification du code existant.

## Adopté dans cette version

- **Routes dynamiques (`app.addRoute`, WebFront#454).** Les modules de rendu des composants
  (`components/<type>/render.js`) sont embarqués dans le binaire avec leur `component.json` ; `WebBridge` les sert sous
  `/components/<type>/render.js`. La page les charge à la demande, sans `eval` ni copie dans `ui/`. Le pont de test
  (`TestApp`) n'a pas de serveur HTTP : la route n'est enregistrée que si l'application en offre une.
- **Tuples et tableaux typés (WebFront#457).** `sf.signals(epoch, curseur)` renvoie la capture des instruments en un
  tuple de quatre nombres et quatre tableaux typés (`Float64Array` des cycles, `Uint16Array` des broches,
  `Float32Array` des tensions, `Uint8Array` des états), 32 768 échantillons au plus par appel. C'est le premier état
  volumineux qui ne passe pas par du JSON. L'état des broches et des composants reste un fragment JSON par trame : il
  est petit et aucune mesure ne justifie de le changer.
- **Erreurs typées (WebFront#458).** Nouveaux codes : `siliconforge.unknownCircuit`, `.invalidCircuit` (le message
  commence par le chemin JSON), `.unknownComponent`, `.invalidValue`, `.unknownExample`.
- **Sélecteur de fichiers (WebFront#451).** Le chargement d'un circuit suit le parcours du firmware et de la
  description de MCU ; `UI-Circuits` le couvre dans la fenêtre native, avec la même limite : la boîte de dialogue
  système n'est pas pilotée.
- **Téléchargements (WebFront#452).** « Export VCD » enregistre la capture par un lien `download` sur un `Blob`.
  Vérifié à la main dans la fenêtre WebKitGTK : le fichier `siliconforge-capture.vcd` arrive sans dialogue dans le
  dossier de téléchargement de l'utilisateur. Les specs contrôlent le texte produit, pas le fichier écrit, pour ne
  rien déposer dans le dossier personnel de qui lance les tests.
- **Options de fenêtre.** Titre « SiliconForge » et taille initiale de 1480 × 960.
- **Captures (`webFrontCapture`, WebFront#468).** `UI-Tour` photographie la vraie fenêtre et en enregistre une courte
  séquence ; `tools/captures2gif` l'assemble en GIF. Voir [#10](https://github.com/ambroise-leclerc/SiliconForge/issues/10).

## Évalué et non retenu

| Sujet | Décision | Raison |
| --- | --- | --- |
| `app.broadcast("siliconForge.onState")` | Reporté | La trame porte le flux série à partir d'un décalage propre à chaque client. Il faut d'abord séparer ce flux (numéro de séquence commun et relecture à la demande) avant d'encoder une trame commune. |
| Rappels asynchrones (`ResultTask`) | Écarté | Le chargement d'un circuit et la résolution du modèle électrique se mesurent en microsecondes. |
| `enableAutomation`, `fs::Storage`, lectures liées au client, `UIExecutor`, `NativeWindow` | Non retenu | Aucun besoin nouveau depuis le bilan 0.9. `fs::Storage` reviendra avec l'édition et l'enregistrement de circuits. |

## Validation

Commandes : `cmake -S . -B build -DSILICONFORGE_WARNINGS_AS_ERRORS=ON`, `cmake --build build`, `ctest`, sous GCC 16.1.0
et Linux.

| Configuration | Résultat |
| --- | --- |
| WebKitGTK 2.52 (fenêtre native, Xvfb pour les specs) | 94 tests : 86 rapides et 8 suites Jasmine (`UI-Uart`, `UI-Blink`, `UI-Led`, `UI-LedRealtime`, `UI-Files`, `UI-Instruments`, `UI-Circuits`, `UI-Tour`), ces dernières rejouées trois fois de suite sans échec |
| Sans moteur embarqué (`-DWEBFRONT_EMBED_WEBKITGTK=OFF -DWEBFRONT_AUTODETECT_NATIVE=OFF`) | 86 tests rapides ; aucune suite Jasmine |
| CEF seul, Windows (WebView2), macOS (WKWebView) | **Non vérifiés** |
| Workflow GitHub (Ubuntu 26.04, GCC 16) | Vérifié sur la pull request #46 : les suites Jasmine passent sans la variable de bac à sable, et l'étape d'animation (`python3-pil`) produit son artefact |
