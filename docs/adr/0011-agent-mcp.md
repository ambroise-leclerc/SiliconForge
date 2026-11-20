# ADR 0011 — Laisser l’agent agir sur la fenêtre par MCP

- Statut : proposée
- Date : 2026-11-19

## Contexte

Dans la Foundry ([ADR 0008](0008-agentic-foundry.md)), l’agent lancé dans le terminal de la fenêtre
([ADR 0009](0009-poste-agentique.md)) n’agit que par des commandes : il compile et lance `siliconforge-bench`, qui
utilise un MCU à part ([ADR 0010](0010-banc-programmable.md)). L’émulation affichée ne bouge pas, et la personne ne
voit ce que fait l’agent qu’en lisant le terminal. L’essai à la main de #61 l’a montré : pour suivre l’agent, il faut
que ses actions se voient sur la carte, et qu’il puisse charger un firmware, appuyer sur un bouton et mesurer comme
la personne le fait.

Les quatre clients visés (Claude Code, Codex, OpenCode, Mistral Vibe) savent utiliser des outils exposés par un
serveur MCP, en stdio ou en HTTP. [Spécification MCP 2025-06-18](https://modelcontextprotocol.io/specification/2025-06-18)

## Décision

1. L’application sert un serveur MCP sur son propre port, à `http://127.0.0.1:<port>/mcp/`, avec le transport
   Streamable HTTP réduit à l’essentiel : JSON-RPC 2.0 en POST, réponse `application/json`, sans flux d’événements.
   Les routes POST à corps borné viennent de WebFront ([WebFront #619](https://github.com/ambroise-leclerc/WebFront/issues/619)).
2. Les outils agissent sur l’**émulation affichée** et appellent les mêmes opérations que la page : charger un
   firmware du projet, lancer, suspendre, réinitialiser, actionner un composant, appliquer une tension, écrire et lire
   la liaison série, lire les broches, mesurer une broche avec les règles du banc, lancer et ouvrir un essai du banc.
3. Chaque appel apparaît dans le journal « Agent » de l’onglet Bench, avec ses arguments et son résultat. La carte, les
   instruments et le panneau Bench montrent l’effet par leurs mises à jour habituelles.
4. La personne garde la main : l’interrupteur « Let the agent drive » coupe les outils qui modifient l’émulation.
   L’agent reçoit alors un refus explicite, et les lectures restent possibles.
5. Le terminal indique le serveur à chaque client, à son lancement : `--mcp-config` pour Claude Code, `-c
   mcp_servers.…` pour Codex, `OPENCODE_CONFIG_CONTENT` pour OpenCode. Mistral Vibe ne lit ses serveurs que dans son
   fichier de configuration : on l’y inscrit une fois, avec un jeton fixé par `SILICONFORGE_MCP_TOKEN`.

## Sécurité

- Le serveur n’existe que si l’interface écoute sur une adresse de bouclage, comme le terminal : une interface servie
  à d’autres machines (`--address 0.0.0.0`) n’a pas de serveur MCP.
- Chaque requête présente `Authorization: Bearer <jeton>`. Le jeton, de 128 bits, est tiré au hasard à chaque
  lancement, sauf si l’utilisateur le fixe. Il passe aux clients par l’environnement, que seul son propriétaire peut
  lire, et non par leurs arguments, que tout utilisateur de la machine peut lire.
- Une requête qui porte un en-tête `Origin` autre qu’une adresse de bouclage est refusée, pour qu’une page web ouverte
  ailleurs ne puisse pas atteindre le serveur par un nom DNS rebondi vers 127.0.0.1.
- `load_firmware` ne lit que des fichiers du projet, avec les contrôles de chemin de l’éditeur (pas de `..`, pas de
  `.git`, pas de lien qui sort du répertoire).

## Alternatives et conséquences

Un pont en stdio, un exécutable lancé par le client qui relaierait vers l’application par un canal local, aurait évité
de modifier WebFront. Mais il aurait fallu une pièce de plus, un canal IPC par plateforme et une découverte de la
fenêtre. Le serveur HTTP dans l’application réutilise son port, son fil de travail et ses contrôles d’accès.

Les outils et la page partagent une émulation : la personne et l’agent peuvent agir en même temps, comme deux
personnes devant le même banc. L’interrupteur règle ce partage. Le serveur ne sert pas encore de ressources ni de
notifications, et ne pousse rien vers le client : l’agent relit l’état avec `status` et `read_pins`.
