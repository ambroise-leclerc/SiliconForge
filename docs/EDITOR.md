# Éditeur des sources

Le bouton **Sources** de la barre d’outils ouvre, dans la fenêtre et à côté du [terminal agentique](TERMINAL.md), les
fichiers du répertoire du projet : on y lit, modifie et enregistre le code, et on y voit les différences Git avec le
dernier commit. L’agent écrit dans ce répertoire depuis son terminal ; la personne relit et corrige au même endroit,
sans perdre ni écraser ce que l’autre vient de faire. La décision est décrite dans l’[ADR 0009](adr/0009-poste-agentique.md).

> **Écart avec l’ADR.** L’ADR parle d’une « fenêtre distincte ». WebFront ne fournit qu’une fenêtre native par
> application : l’éditeur est un panneau de cette fenêtre, affiché côte à côte avec le terminal. Une fenêtre séparée
> demanderait une fonction de WebFront qui n’existe pas.

## Comportement

- **Liste.** Les fichiers du répertoire de travail de SiliconForge. Dans un dépôt Git, ce sont les fichiers suivis et
  les fichiers non suivis non ignorés ; ceux qui diffèrent du dernier commit viennent en premier, avec leur état :
  `M` modifié, `A` ajouté, `D` supprimé, `R` renommé, `U` en conflit, `?` non suivi. Sans Git (programme absent ou
  dossier hors dépôt), tous les fichiers sont listés sauf `.git`, `build`, `node_modules` et `.cache`, sans état.
  **Changed only** (coché par défaut) ne garde que les fichiers modifiés ; le champ **Filter files** filtre par nom.
  La liste est limitée à 5 000 fichiers.
- **Édition.** Un tampon par fichier ouvert : passer à un autre fichier ou masquer le panneau garde les modifications
  (repérées par `●` dans la liste). **Save** ou `Ctrl+S` enregistre. Un fichier CRLF reste CRLF ; un fichier qui mêle
  les fins de ligne s’affiche mais n’est pas modifiable, pour que l’enregistrement ne les altère pas. Seuls les
  fichiers texte UTF-8 de moins de 1 Mio s’ouvrent ; un fichier binaire ou trop gros ne montre que ses différences.
- **Coloration syntaxique.** Le texte est coloré par [Prism](https://prismjs.com) selon le nom du fichier : C et C++
  (`.c`, `.h`, `.cpp`, `.hpp`, `.ino`...), Python, JSON, Bash, CMake (`CMakeLists.txt`, `.cmake`), JavaScript, CSS,
  HTML et XML, Markdown, YAML, Makefile, TOML, INI et Rust ; un autre fichier reste en texte simple. Au-delà de
  200 000 caractères le texte n'est plus coloré, pour que la frappe reste fluide. Le `<textarea>` reste le champ
  d'édition (curseur, sélection, copier-coller, accessibilité) : son texte est transparent et une copie colorée est
  dessinée dessous, défilant avec lui. Les couleurs suivent le thème clair ou sombre.
- **Différences.** L’onglet **Changes** montre le diff unifié du fichier enregistré contre le dernier commit
  (`git diff HEAD`), sans les modifications non enregistrées du tampon. Un fichier non suivi s’affiche entièrement
  ajouté ; un fichier supprimé s’affiche en diff seul.
- **Modification externe.** La page compare le fichier à son disque toutes les 1,5 s, et la liste toutes les 3 s.
  Un tampon propre se recharge tout seul. Un tampon modifié n’est jamais remplacé : il est signalé, et l’enregistrement
  est alors refusé par l’application (voir ci-dessous).
- **Deux versions avant toute écriture.** L’application n’écrit un fichier que s’il a encore la version lue par la
  page (taille et empreinte du contenu, pas la date). Sinon rien n’est écrit et la page montre côte à côte **Your
  version** et **On the disk now**, avec trois choix :
  - **Overwrite with my version** écrit le texte local par-dessus la version du disque qui vient d’être lue (refusé
    de nouveau si le fichier a encore changé entre-temps) ;
  - **Use the disk version** met la version du disque dans l’éditeur et **garde le texte local** : **Restore my text**
    le remet dans le tampon, prêt à être enregistré, et il est conservé dans le stockage du navigateur de la page
    jusqu’à sa restauration ou sa suppression (**Discard it**) ;
  - **Cancel** ne décide rien : le tampon reste tel quel, le fichier aussi.
  Si le fichier a disparu ou n’est plus du texte, il n’y a pas de version à comparer : le texte local reste dans
  l’éditeur et dans le volet de gauche, d’où on peut le copier.
- **Écriture.** Le fichier est écrit à côté (`<nom>.sf-save`) puis renommé par-dessus : l’agent ne lit jamais un demi-
  fichier, et les permissions sont conservées. Entre la vérification de version et le renommage, il reste une courte fenêtre
  pendant laquelle un autre programme pourrait écrire ; son écriture serait alors perdue.
- **Git sans gêner l’agent.** Les commandes Git de l’éditeur sont en lecture seule et ne prennent pas le verrou de
  l’index (`--no-optional-locks`) : un `git commit` de l’agent ne rencontre jamais un `index.lock` laissé par l’éditeur.

## Sécurité

La page nomme un fichier par un chemin relatif au répertoire du projet. L’application refuse les chemins absolus, les
`..`, tout composant `.git`, et les liens qui sortent du répertoire ou mènent à `.git` (le chemin est résolu avant
d’être comparé au répertoire). Aucun chemin de la page n’est passé à un shell sans guillemets, et Git est lancé sans
programme de diff ni filtre externe (`--no-ext-diff --no-textconv`). Comme le terminal, l’éditeur donne accès aux
fichiers de l’utilisateur à toute page de l’origine de l’application : le port reste local.

## Dans la page

`window.siliconForge.editor` (voir [`ui/editor.js`](../ui/editor.js)) : `files`, `open(path)`, `text`, `save()`,
`language`, `state` (`{path, dirty, stale, deleted, error, view, conflict, recovered}`), `conflict` (`{mine, disk}`), `keepMine()`,
`useDisk()`, `cancel()`, `restore()`, `view("edit" | "diff")`, `poll()`, `refresh()`, `show(visible)`.

Côté C++ ([`Project.hpp`](../apps/siliconforge/Project.hpp)), les fonctions renvoient du JSON :

| Fonction | Réponse |
| --- | --- |
| `sf.files.list()` | `{name, git, truncated, files: [{path, status}]}` |
| `sf.files.read(path)` | `{path, content, version, lineEndings}` |
| `sf.files.version(path)` | `{version}`, `null` si le fichier a disparu ou n’est plus du texte |
| `sf.files.write(path, content, version)` | `{version}` ; refusé avec `siliconforge.fileChanged` si le fichier n’est plus à cette version |
| `sf.files.diff(path)` | `{git, diff, truncated}` |

Codes d’erreur : `siliconforge.unknownFile`, `notText`, `tooLarge`, `fileChanged`, `notEditable`, `writeFailed`.

## Qualification

- `ctest -R "siliconforge_bridge_tests"` (cas `[project]`) : accès aux fichiers, chemins refusés (dont les liens),
  version et refus d’écraser, fins de ligne et permissions, fichiers binaires ou trop gros, liste et diff Git,
  dossier d’un dépôt, absence de verrou de l’index.
- `ctest -R UI-Editor` (fenêtre native ; Linux et Windows) : liste, ouverture, modification et enregistrement ; rechargement d’un
  tampon propre ; tampon modifié signalé sans être remplacé ; les deux versions, **Cancel**, **Use the disk version**
  puis **Restore my text**, **Overwrite with my version** ; texte coloré selon le langage, aligné sur le champ de saisie et défilant avec lui ; chemins refusés ; changements laissés par un shell du
  terminal (fichier modifié, nouveau, supprimé) vus dans la liste et en diff. Le dossier du test est refait avant
  chaque exécution (fixture `UI-Editor-Project`) et devient un dépôt Git si `git` est installé.

Windows 11 10.0.22631, x86-64, WebView2, `cmd.exe` (ConPTY), MSVC, 2026-11-19 : `ctest -R UI-Editor` passe (10 cas sur 10)
et les tests C++ du projet passent (145 tests hors fenêtre). L’agent y est un `cmd.exe` qui lance PowerShell pour écrire
des fins de ligne LF, puis `del`. Sont vérifiés en plus des cas de Linux : un fichier `.git` atteint par son nom court
`GIT~1` (présent sur ce volume), par `.git.` ou `.git ` ; un chemin à lecteur (`C:/…`), à antislash, un flux de données
(`main.cpp::$DATA`) : tous refusés. La fenêtre exige la session graphique de l’utilisateur ; la CI Windows exclut le
label `ui` : le test se lance à la main.

macOS : `UI-Editor` est câblé comme sur Windows (WKWebView, shell `sh`) mais n’a pas été exécuté ; il reste à qualifier.
Les tests C++ y tournent en intégration continue.
